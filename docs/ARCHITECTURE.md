# Architecture & Design Rationale

Companion to `architecture-diagram.svg` and `workflow-diagram.svg`. This document
explains *why* the system is built the way it is, focusing on the decisions that were
genuinely contested.

---

## 1. Why nine tasks instead of one loop

The training build ran two tasks, with MQTT, OLED rendering and alarm generation all on
the same one. That structure has a specific failure mode: `mqtt.connect()` blocks. When
the broker is slow, the alarm buzzer stops, because the code that toggles it is queued
behind a TCP handshake.

For a patient monitor that is the worst possible coupling — the alarm goes quiet exactly
when the network problem means nobody is watching the dashboard either.

The split follows one rule: **anything with an obligation to the physical world must not
share a task with anything that can block on the network.**

| | Core 0 | Core 1 |
|---|---|---|
| **Can block on network** | `mqttTask`, `connectivityTask` | — |
| **Compute only** | `processingTask` | — |
| **Physical timing obligation** | — | `alertTask`, `vitalsSensorTask`, `advancedVitalsTask`, `actuatorTask`, `envSensorTask`, `displayTask` |

`processingTask` sits on core 0 because it is pure computation with a soft 1 Hz deadline;
putting it there leaves core 1 entirely for real-time work.

---

## 2. Priority assignment

| Prio | Task | Justification |
|------|------|---------------|
| 6 | `alertTask` | Alarm latency is the one number a clinician notices |
| 5 | `processingTask` | Risk score gates alarms, bed angle and sampling rate |
| 4 | `vitalsSensorTask` | Patient data; late data is stale data |
| 4 | `actuatorTask` | 25 ms servo/dosage ramp cadence — jitter is visible motion |
| 3 | `advancedVitalsTask` | BP/ECG are diagnostic context, not the primary alarm trigger |
| 3 | `envSensorTask` | Room conditions change over minutes |
| 3 | `mqttTask` | Best-effort; the offline buffer covers delay |
| 3 | `connectivityTask` | 1 Hz supervision is fast enough |
| 2 | `displayTask` | A 3 s stale OLED page harms nobody |

**Priority inversion** is avoided by keeping mutex hold times short: every critical
section is a struct copy or a handful of field assignments. No task holds `mState` across
a blocking call. `mMqtt` is held across network I/O, but only `mqttTask` (prio 3) and
`connectivityTask` (prio 3) contend for it — equal priority, so no high-priority task can
be blocked by a low-priority holder.

---

## 3. Shared state: one struct, one mutex

Everything shared lives in a single `SystemState` struct behind a single mutex.

The alternative — a mutex per variable — is tempting and wrong here. With per-variable
locks, `displayTask` could read a heart rate from sample *n* and an SpO₂ from sample
*n+1* and render a patient who never existed. The same applies to an MQTT payload.

The cost is coarse-grained locking. That cost is acceptable because every critical
section is a few dozen bytes of copying, measured in microseconds. Readers take a
whole-struct copy under the lock and then work from the copy:

```c
SystemState s;
lockState(); s = st; unlockState();
// ... work with s, lock is already released
```

This gives a consistent snapshot and the shortest possible hold time simultaneously.

---

## 4. Deadlock: prevented by construction

Four synchronisation objects exist (`mState`, `mI2C`, `mMqtt`, `semAlert`). Deadlock
requires circular wait, so the design eliminates the possibility of a cycle rather than
detecting one at runtime.

**Only one nesting exists in the entire codebase:** `mMqtt` → `mState`, occurring in
exactly two call paths:

- `mqttTask` → `publishAll()`
- `connectivityTask` → `drainOfflineBuffer()`

Both acquire in the same order. **No path anywhere acquires `mState` before `mMqtt`.**
`mI2C` is taken only by `displayTask` and never nested with anything. `semAlert` is a
signalling primitive, given and taken, never held.

With a total order on lock acquisition, the circular-wait condition cannot be satisfied,
so deadlock is impossible — not unlikely, impossible.

---

## 5. Queue semantics: overwrite vs. FIFO

| Queue | Depth | Semantics | Why |
|-------|-------|-----------|-----|
| `qVitals` | 1 | `xQueueOverwrite` | A slow consumer must get the *newest* vitals, never work through a backlog of stale ones |
| `qAdvanced` | 1 | `xQueueOverwrite` | Same, for BP/ECG. Also the task-to-task link: `advancedVitalsTask` peeks `qVitals` for heart rate to derive BP |
| `qEnv` | 1 | `xQueueOverwrite` | Same, for facility parameters |
| `qCommand` | 12 | FIFO | Every command must be executed; dropping one loses a doctor's instruction |
| `qWakeVitals` / `qWakeAdvanced` / `qWakeEnv` | 1 each | `xQueueOverwrite` | Pure edge signal; two pending tokens mean the same as one |
| `qOffline` | 120 | FIFO + manual ring | Chronological order matters for the resync; oldest dropped on overflow |

**Why three wake queues and not one.** This was a real bug in the first cut. A single
shared `qSamplingWake` looks correct — publish one token when the rate changes — but
`xQueueReceive` **consumes** the item. With three tasks waiting on it, only the first to
wake receives the token; the other two sleep out their full period, up to 60 seconds.
The dashboard slider would appear to work while two thirds of the system ignored it. One
queue per waiter is the fix; `notifySamplingChanged()` overwrites all three.

The `qVitals` choice is the interesting one. A depth-8 FIFO would mean that after a
network stall, `processingTask` works through eight-cycle-old vitals before reaching the
present. For a risk monitor that's actively harmful — it would alarm on a condition the
patient recovered from a minute ago. Overwrite semantics guarantee the risk engine always
sees now.

---

## 6. The watchdog problem, and the helper that solved it

Task 1 requires a watchdog. Task 5 requires sampling periods up to 60 seconds. A task
sleeping 60 s starves a 20 s watchdog and triggers a panic reset.

Three options were considered:

1. **Raise the timeout above 60 s.** Rejected — a watchdog that takes 90 s to notice a
   hang is decorative.
2. **Exclude the sensor tasks from the watchdog.** Rejected — those are exactly the tasks
   whose failure means the patient is unmonitored.
3. **Chunk the sleep.** Chosen.

```c
static void interruptibleDelay(uint32_t ms, QueueHandle_t wakeQueue) {
  uint32_t left = ms;
  uint8_t  dummy;
  while (left > 0) {
    uint32_t slice = left > 1000 ? 1000 : left;
    if (xQueueReceive(wakeQueue, &dummy, pdMS_TO_TICKS(slice)) == pdTRUE) {
      wdtFeed();
      return;                     // rate changed -- resample immediately
    }
    wdtFeed();
    left -= slice;
  }
}
```

Each of the three sampling-driven tasks passes its **own** wake queue — see §5 for why a
shared one silently breaks.

The task blocks on a queue rather than spinning, so it uses zero CPU while waiting —
`vTaskDelay` semantics preserved, which is what Task 5 asks for. Worst-case watchdog
starvation is 1 second regardless of the configured period. And because it wakes on a
queue token, a rate change from the dashboard takes effect in milliseconds instead of
waiting out the remaining sleep.

One eleven-line helper satisfies a Task 1 requirement and a Task 5 requirement that
initially looked contradictory. This is the piece of the design I'd point at first.

---

## 7. Actuator safety: one path, one limiter

Both remotely controlled actuators — infusion dosage and bed angle — could in principle
be written as `servo.write(sliderValue)`. They are not.

The rule applied: **the command handler sets a target; only the actuator task moves the
output, and it always ramps.**

```
applyCommand()          actuatorTask (25 ms)
   sets target    ──▶   current chases target at a bounded rate  ──▶  hardware
```

Consequence: *every* path to the hardware is rate limited. Manual slider, preset button,
automatic condition-driven adjustment — all of them set a target and none of them can
write the output directly. There is no code path that bypasses the limiter, which means
no future change can accidentally introduce one without deleting the structure.

For dosage there is an extra stage — the confirmation gate — giving three variables:

```
dosageSetpoint ──▶ dosageTarget ──▶ dosageCurrent ──▶ hardware
  raw slider     confirm gate       rate limiter
```

**The gate fails downward.** A request above 80 mg/hr pins the target at 80 and waits
15 seconds. If nobody confirms, it stays at 80. Inaction cannot produce an overdose.

---

## 8. Three network states, not two

Up/down is the obvious model and it is insufficient. A third state, DEGRADED, covers the
case where the link is associated and the broker socket is open, but publishes are
failing.

That case is more dangerous than being plainly offline, because the dashboard keeps
displaying the last received value and looks alive. Nobody investigates a dashboard that
looks fine.

| Signal | State |
|--------|-------|
| `WiFi.status() != WL_CONNECTED` | OFFLINE / `FAULT_WIFI` |
| Wi-Fi up, `!mqtt.connected()` | OFFLINE / `FAULT_MQTT` |
| Link up, ≥2 consecutive publish failures | DEGRADED / `FAULT_MQTT` |
| Link up, patient sensor disconnected | DEGRADED / `FAULT_SENSOR` |
| All healthy | ONLINE |

Sensor health is folded into the same state machine deliberately. A monitor reporting
"Patient stable" from an unplugged probe is worse than one reporting nothing, so a
disconnected sensor degrades the *system* state, not just the reading.

---

## 9. Buffer policy: drop oldest, not newest

When `qOffline` fills, the implementation discards the **oldest** record:

```c
if (xQueueSend(qOffline, &r, 0) != pdTRUE) {
  BufferedRecord discard;
  xQueueReceive(qOffline, &discard, 0);   // drop oldest
  xQueueSend(qOffline, &r, 0);
}
```

The alternative — refusing new records once full — preserves the start of the outage and
loses the end. For a clinical risk monitor that is backwards: the data closest to the
present is what the clinician needs at handover.

120 records × 32 bytes ≈ 3.8 KB. At the 15 s default that is 30 minutes of history; at
5 s adaptive sampling, 10 minutes.

---

## 10. Resync: order preservation across a failed retry

```c
if (!pSysBacklog.publish(payload)) {
  xQueueSendToFront(qOffline, &r, 0);   // put it back at the FRONT
  break;
}
```

`xQueueSendToFront` rather than `xQueueSend` is the whole point. If the link drops
mid-replay, the failed record returns to the head of the queue, so the next attempt
resumes exactly where it stopped. Using the normal tail-send would move that record
behind everything else and scramble the timeline — a subtle bug that would only show up
in exactly the flaky-network scenario the feature exists to handle.

Replay is throttled to one message per 600 ms to stay inside Adafruit IO's rate limit;
without it, a 120-record burst would be throttled by the broker and the resync would fail
in a way that looks like a code bug.

---

## 11. Exponential backoff

1 → 2 → 4 → 8 → 16 → 32 → 60 s, capped.

At single-device scale this only saves a little power. It matters at ward scale: 200 beds
reconnecting simultaneously after a switch reboot produce a thundering herd that can keep
the broker down. Backoff with a cap is what stops the recovery from becoming the outage.

The cap at 60 s rather than unbounded growth means a device never becomes effectively
unreachable after a long outage.

---

## 12. Topic design as an access-control boundary

```
medical.*   facility.*   system.*   control.*
```

Four disjoint prefixes. This is not organisational tidiness — it is the mechanism by
which a facilities technician is prevented from receiving patient vitals.

On any broker supporting topic ACLs, a credential scoped to `<user>/feeds/facility/#`
cannot subscribe to `medical.heart-rate`. The restriction is enforced by the broker
before a byte reaches the client. Hiding widgets in a dashboard UI provides no such
guarantee — anyone with the credential can subscribe to `#` with a CLI client.

Adafruit IO's free tier does not expose per-topic ACLs, so on that platform the
separation is structural rather than enforced. The design is correct and portable; the
limitation is the platform's, and it is stated in the report rather than glossed over.

---

## 13. Memory

| Consumer | Bytes |
|----------|-------|
| Task stacks (9 tasks) | 43,008 |
| `qOffline` (120 × 32) | 3,840 |
| `SystemState` | ≈200 |
| Other queues | ≈300 |
| SSD1306 framebuffer (128×64/8) | 1,024 |
| **Total explicit** | **≈45 KB** |

ESP32 has ~320 KB DRAM, leaving ample headroom for the Wi-Fi/TCP stack (~40 KB) and
Arduino runtime. Measured free heap after boot: ≈180 KB, stable — the OLED page 5 heap
readout exists specifically so a leak would be visible during a long demo run.

---

## 14. What I would change with more time

1. **TLS.** Port 1883 is plaintext. `WiFiClientSecure` on 8883 with a pinned CA is a
   drop-in change to the client object and should be done before any real deployment.
2. **SNTP + RTC.** Buffered records are stamped with `millis()`, which resets on reboot.
   Real timestamps need wall-clock time.
3. **A redundant alarm path.** IEC 60601-1-8 expects the alarm system not to share a
   single point of failure with the monitoring system. Here, an MCU fault silences
   everything — the watchdog reset is a partial mitigation, not a solution.
4. **Real sensors.** MAX30102 (HR/SpO₂), AD8232 (ECG), MQ-135 (air quality). Only the
   simulation block inside `vitalsSensorTask` would change; every other layer is already
   sensor-agnostic.
