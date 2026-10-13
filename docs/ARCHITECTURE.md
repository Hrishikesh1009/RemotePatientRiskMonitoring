# Architecture & Design Rationale

Companion to `architecture-diagram.svg` and `workflow-diagram.svg`. This document
explains *why* the system is built the way it is, focusing on the decisions that were
genuinely contested.

---

## 1. Why ten tasks instead of one loop

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
| **Physical timing obligation** | — | `alertTask`, `vitalsSensorTask`, `bloodPressureTask`, `ecgTask`, `actuatorTask`, `envSensorTask`, `displayTask` |

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
| 3 | `bloodPressureTask` | BP is diagnostic context, derived from heart rate, not the primary alarm trigger |
| 3 | `ecgTask` | ECG amplitude, same reasoning; its own task because Task 1 asks for each added parameter to be separately managed |
| 3 | `envSensorTask` | Room conditions change over minutes |
| 3 | `mqttTask` | Best-effort; the offline buffer covers delay |
| 3 | `connectivityTask` | 1 Hz supervision is fast enough |
| 2 | `displayTask` | A 1 s stale OLED harms nobody |

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
| `qBp` | 1 | `xQueueOverwrite` | Same, for blood pressure. Also the task-to-task link: `bloodPressureTask` peeks `qVitals` for heart rate to derive BP |
| `qEcg` | 1 | `xQueueOverwrite` | Same, for the ECG amplitude read by `ecgTask` |
| `qEnv` | 1 | `xQueueOverwrite` | Same, for facility parameters |
| `qCommand` | 12 | FIFO | Every command must be executed; dropping one loses a doctor's instruction |
| `qWakeVitals` / `qWakeBp` / `qWakeEcg` / `qWakeEnv` | 1 each | `xQueueOverwrite` | Pure edge signal; two pending tokens mean the same as one |
| `qOffline` | 120 | FIFO + manual ring | Chronological order matters for the resync; oldest dropped on overflow |

**Why four wake queues and not one.** This was a real bug in the first cut. A single
shared `qSamplingWake` looks correct — publish one token when the rate changes — but
`xQueueReceive` **consumes** the item. With several tasks waiting on it, only the first to
wake receives the token; the others sleep out their full period, up to 60 seconds.
The dashboard slider would appear to work while most of the system ignored it. One
queue per waiter is the fix; `notifySamplingChanged()` overwrites all four.

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
#define SAMPLING_SLICE_MS 100

static void interruptibleDelay(uint32_t ms, QueueHandle_t wakeQueue) {
  uint8_t  dummy;
  uint32_t waited = 0;
  while (waited < ms) {
    vTaskDelay(pdMS_TO_TICKS(SAMPLING_SLICE_MS));       // non-blocking: yields the CPU
    waited += SAMPLING_SLICE_MS;
    wdtFeed();
    if (xQueueReceive(wakeQueue, &dummy, 0) == pdTRUE)  // dynamic update arrived via queue
      return;                                           // rate changed -- resample now
  }
}
```

Each of the four sampling-driven tasks (`vitalsSensorTask`, `bloodPressureTask`,
`ecgTask`, `envSensorTask`) passes its **own** wake queue — see §5 for why a shared one
silently breaks.

This is literally the pattern Task 5 names: *non-blocking delays (`vTaskDelay`) and dynamic
updates through queues*. The wait is made of 100 ms `vTaskDelay()` slices, so the task is
descheduled and uses no CPU, and between slices it polls its own queue **without blocking**.
Worst-case watchdog starvation is one slice (100 ms) regardless of the configured period.
Because a queue token ends the wait, a rate change from the dashboard takes effect within
one slice instead of waiting out the remaining sleep — **measured 2026-10-13: a task asleep
on a 60 s period resampled ≈1 s of simulated time after a 5 s command** (see
`TEST_RESULTS.md` TC-11).

One twelve-line helper satisfies a Task 1 requirement and a Task 5 requirement that
initially looked contradictory. This is the piece of the design I'd point at first.

The sampling task also subtracts the time its own work took (the DS18B20 conversion alone
is ≈750 ms) from the period, so a "5 s" setting really is 5.0 s between samples — measured
as 5.08 s in simulated time, from the `[SAMPLE]` trace.

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
  if (++failures >= 3) break;           // give up this round; retried later on a backoff
  ...
}
```

`xQueueSendToFront` rather than `xQueueSend` is the whole point. If the link drops
mid-replay, the failed record returns to the head of the queue, so the next attempt
resumes exactly where it stopped. Using the normal tail-send would move that record
behind everything else and scramble the timeline — a subtle bug that would only show up
in exactly the flaky-network scenario the feature exists to handle.

Replay is throttled to one message per 600 ms to stay inside broker rate limits (Adafruit
IO's in particular); without it, a 120-record burst would be throttled by the broker and
the resync would fail in a way that looks like a code bug. If a replay does stall, the
records are kept and the supervisor retries on its own exponential schedule while the link
is healthy. **Measured 2026-10-13:** a 21-record backlog replayed 21 records, in order, at
exactly the 5 s sampling spacing (`TEST_RESULTS.md` TC-12).

---

## 11. Exponential backoff

1 → 2 → 4 → 8 → 16 → 32 → 60 s, capped.

At single-device scale this only saves a little power. It matters at ward scale: 200 beds
reconnecting simultaneously after a switch reboot produce a thundering herd that can keep
the broker down. Backoff with a cap is what stops the recovery from becoming the outage.

The cap at 60 s rather than unbounded growth means a device never becomes effectively
unreachable after a long outage.

**The streak resets when a layer recovers.** Recovery is two stages — Wi-Fi association,
then the broker session. Once Wi-Fi re-associates the failure streak is over, so the
backoff returns to 1 s and the broker is tried on the next tick. Without this the broker
attempt inherited the 60 s delay earned by the Wi-Fi failures: measured, a restored link
stayed OFFLINE for ≈65 s; with the reset it is back ONLINE in ≈10 s.

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

The shipped default broker (`broker.hivemq.com`) is public and has no ACLs, and Adafruit
IO's free tier does not expose per-topic ACLs either, so on both the separation is
structural rather than enforced. The design is portable to a broker that does; ACL
configuration is infrastructure outside this project, and the limitation is stated in
`TASK_MAPPING.md` and the report rather than glossed over.

---

## 13. Memory

| Consumer | Bytes |
|----------|-------|
| Task stacks (10 tasks) | 46,080 |
| `qOffline` (120 × 32) | 3,840 |
| `SystemState` | ≈200 |
| Other queues | ≈300 |
| SSD1306 framebuffer (128×64/8) | 1,024 |
| **Total explicit** | **≈48 KB** |

ESP32 has ~320 KB DRAM, leaving ample headroom for the Wi-Fi/TCP stack (~40 KB) and
Arduino runtime. Measured free heap after boot: **166 KB**, and **165 KB** after 506 s of simulated
operation that included three outages and their replays (`docs/evidence/`) — the OLED page 5 heap
readout exists specifically so a leak would be visible during a long demo run.

---

## 14. Fault injection: making Task 6 demonstrable

Wokwi has no control for "pull the network cable", and editing `WIFI_SSID` and restarting
wipes the RAM buffer — so the cycle the task is about (go offline, buffer, come back,
replay) cannot be shown that way. Two slide switches fix that:

| Switch | GPIO | HIGH means | Effect on the real link |
|--------|------|-----------|-------------------------|
| `swWifiCut` | 23 | Wi-Fi cut | `WiFi.disconnect()`, and the Wi-Fi recovery attempt fails while held |
| `swBrokerCut` | 39 (VN) | broker unreachable | `mqtt.disconnect()`, and the broker recovery attempt fails while held |

They act on the *real* connection, so detection, buffering, the buzzer pattern and the
backoff are the production code paths, not a flag set for the demo. Releasing a switch is
treated as a link-up event (one immediate retry, as a PHY interrupt would trigger).
Every injection is written to the serial log (`[FAULT] ...`) so it can never be mistaken
for a real outage in a log.

## 15. Keepalive, publish cadence and the synthetic patient

Four smaller decisions that came out of measuring the system rather than reading it:

- **Keepalive only when idle.** `mqtt.ping()` is sent only after 10 s with nothing sent.
  An earlier revision pinged on every 200 ms pass of the MQTT loop (≈5 PINGREQ/s, each
  holding the client lock for up to 500 ms); combined with a separate publish timer, the
  measured publish cadence was 2–3× the configured sampling period. The library's own
  keepalive is 300 s, so the session never lapses and the ping exists only to notice a
  silently dead connection (3 unanswered pings ⇒ drop and reconnect).
  A burst of 12 distinct commands was applied 12 out of 12 (`TEST_RESULTS.md` TC-15).
- **Publish per sample.** `mqttTask` publishes when a *new* vitals sample exists, not on a
  second free-running timer. A separate timer is never in phase with the sampling task, so
  it re-sends, skips or drifts whenever the rate changes. Dashboard cadence is now exactly
  the sampling rate, and the aggregate's `n` is the true sample count.
- **A synthetic patient that stays stable.** Heart rate and SpO₂ are software-generated
  (no part exists in Wokwi). The first revision was an unbounded random walk that wandered
  into the 150–175 bpm band and stayed there. They now mean-revert to 78 bpm / 97 % with
  rare episodes, and `control.sim-hr` / `control.sim-spo2` can pin them to a chosen value so
  the risk engine, adaptive sampling and automatic bed can be shown on demand.
- **A stalled link must not reset the chip.** A publish round is ~15 blocking TCP writes
  made while `mqttTask` holds the client lock. When the broker (or Wokwi's gateway) stops
  reading, one write can block ~10 s; with the watchdog fed once per pass, two or three of
  them tripped the 20 s task watchdog and rebooted the chip — losing the RAM buffer and
  silencing the alarms for the reboot (BUG_LOG B-38, seen once in a long simulation
  session). Now the watchdog is fed **before every write**, a round **ends at the first
  failure**, and after a failed round the session is **probed every second**; three
  unanswered probes drop it, the supervisor reports OFFLINE/MQTT and buffering starts.
  The principle is the same as §1: nothing that can wait on the network may be allowed to
  starve a supervisor or be the reason the whole device resets. (A stall cannot be produced
  on demand in Wokwi, so this path is reviewed and regression-tested, not exercised.)

---

## 16. What I would change with more time

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
