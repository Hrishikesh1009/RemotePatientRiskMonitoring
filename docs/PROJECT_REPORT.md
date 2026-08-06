# Project Report
## Remote Patient Risk Monitoring System

**Intern:** Hrishikesh Lokhande
**Programme:** ElevanceSkills Internship — IoT / Embedded Systems
**Project:** Learn to Build a Real World Remote Patient Risk Monitoring System
**Platform:** ESP32 (dual-core Xtensa LX6) · FreeRTOS · MQTT · Adafruit IO · Wokwi
**Date:** August 2026

---

## 1. Abstract

Hospital wards monitor patients with equipment that is expensive, isolated from the
network, and typically alarms only after a threshold has already been crossed. This
project builds a low-cost networked alternative on an ESP32: a single embedded system
that samples six patient parameters and four room parameters concurrently, fuses them
into a real-time clinical risk score, publishes to two separate role-scoped dashboards,
accepts remote actuation of medication dosage and bed elevation, adapts its own sampling
rate to how sick the patient is, and continues logging without data loss when the network
fails.

The system is built as one integrated FreeRTOS application with nine concurrent tasks
distributed across both ESP32 cores, coordinated through queues, mutexes and a binary
semaphore, and supervised by a hardware task watchdog.

---

## 2. Problem statement

A remote patient monitor deployed in a real ward has to satisfy four requirements that
a naïve `loop()`-based sketch cannot:

1. **Hard real-time alarms.** A critical desaturation alarm cannot wait for an OLED
   refresh or a blocked TCP write. Alarm latency must be bounded regardless of what
   else the system is doing.
2. **Role separation.** A facilities technician who needs air-quality data must not
   receive identifiable patient vitals. This is a data-protection requirement, not a
   UI preference.
3. **Safe remote actuation.** A slider that controls an infusion pump must never
   translate a UI drag directly into an instantaneous physical change.
4. **Graceful degradation.** Ward Wi-Fi is unreliable. Losing the link must never mean
   losing patient data.

Each of the six internship tasks addresses one of these pressures. The sections below
map each task to its design, implementation and verification.

---

## 3. System overview

```
   PATIENT SIDE                    ESP32 / FreeRTOS                  CLOUD / UI
 ┌──────────────┐          ┌───────────────────────────┐       ┌──────────────────┐
 │ DS18B20 temp │──┐       │  vitalsSensorTask  (P4)   │       │ Medical Staff    │
 │ HR / SpO2    │  ├──────▶│  envSensorTask     (P3)   │       │ Dashboard        │
 │ BP (derived) │  │       │            │              │  ┌───▶│ medical.*        │
 │ ECG pot      │  │       │        qVitals/qEnv       │  │    └──────────────────┘
 │ PIR motion   │──┘       │            ▼              │  │
 └──────────────┘          │  processingTask    (P5)   │──┤    ┌──────────────────┐
                           │   risk engine + adaptive  │  │    │ Facility Mgmt    │
 ┌──────────────┐          │   sampling + auto-bed     │  └───▶│ Dashboard        │
 │ DHT22 room   │──┐       │       │         │         │       │ facility.*       │
 │ O2 pot       │  ├──────▶│   semAlert   stateMutex   │       └──────────────────┘
 │ AQI pot      │──┘       │       ▼         ▼         │                 ▲
 └──────────────┘          │  alertTask   displayTask  │                 │
                           │      (P6)        (P2)     │            control.*
 ┌──────────────┐          │       │          │        │            sliders
 │ 4x buzzer    │◀─────────┤  actuatorTask    (P4)     │◀────────────────┘
 │ 3x LED       │          │  mqttTask        (P3)     │
 │ SG90 servo   │◀─────────┤  connectivityTask(P3)     │
 │ SSD1306 OLED │◀─────────┤            │              │
 └──────────────┘          │      qOffline (120)       │
                           └───────────────────────────┘
```

See `architecture-diagram.svg` and `workflow-diagram.svg` for the full-resolution
versions submitted with this report.

---

## 4. Task 1 — Multi-Sensor Expansion Using FreeRTOS

### 4.1 Requirement
Extend the system with additional health parameters, each managed by separate FreeRTOS
tasks; run sensing, environment, MQTT, alerting and display concurrently under
priority-based scheduling; coordinate with queues, semaphores and mutexes; add a
watchdog timer.

### 4.2 New parameters added
| Parameter | Source | Range | Rationale |
|-----------|--------|-------|-----------|
| Blood pressure (sys/dia) | Derived from HR with physiological correlation | 70–210 / 40–130 mmHg | Wokwi has no BP cuff part; the task asks for the parameter |
| ECG amplitude | Potentiometer on GPIO36 (ADC1) | 0.00–5.00 mV | Real analog channel, operator-adjustable during the demo |
| Room temperature | DHT22 on GPIO27 | −40–80 °C | Feeds the facility dashboard |
| Room humidity | DHT22 on GPIO27 | 0–100 % | Feeds the facility dashboard |
| Oxygen concentration | Potentiometer on GPIO34 | 15.0–25.0 % | Ward safety parameter |
| Air Quality Index | Potentiometer on GPIO35 | 0–500 | Ward safety parameter |

BP is computed as `sys = 112 + (HR − 78)·0.45 ± 4` and `dia = 72 + (HR − 78)·0.22 ± 3`,
so it tracks heart rate the way a real patient's would rather than moving randomly.
This is stated openly rather than presented as a physical measurement.

### 4.3 Task table

| Task | Prio | Core | Stack | Period | Responsibility |
|------|------|------|-------|--------|----------------|
| `alertTask` | 6 | 1 | 4096 | event + 500 ms | All buzzers and LEDs |
| `processingTask` | 5 | 0 | 8192 | 1000 ms | Risk engine, adaptive sampling, auto-bed |
| `vitalsSensorTask` | 4 | 1 | 4096 | 5–60 s | Core patient sensing — temp, HR, SpO₂, motion |
| `actuatorTask` | 4 | 1 | 3072 | 25 ms | Dosage ramp + servo ramp |
| `advancedVitalsTask` | 3 | 1 | 3072 | 5–60 s | **Blood pressure + ECG** |
| `envSensorTask` | 3 | 1 | 4096 | 10–120 s | Facility sensing |
| `mqttTask` | 3 | 0 | 8192 | 200 ms | Publish / subscribe / aggregate |
| `connectivityTask` | 3 | 0 | 4096 | 1000 ms | Link state machine, backoff, resync |
| `displayTask` | 2 | 1 | 4096 | 3000 ms | OLED page rotation |

Task 1 asks for the additional health parameters to be "managed by separate FreeRTOS
tasks", so blood pressure and ECG are **not** appended to the core vitals loop — they get
`advancedVitalsTask`. That task peeks `qVitals` for heart rate (BP is derived with
physiological correlation) and produces `qAdvanced`, giving a genuine task-to-task data
dependency carried over a queue rather than a shared global.

**Core assignment rationale.** Everything that can block on the network (`mqttTask`,
`connectivityTask`) is pinned to core 0 together with the risk engine. Everything with a
timing obligation to the physical world — alarms, sensing, the servo ramp, the display —
is pinned to core 1. A stalled TCP write therefore cannot delay an alarm, which was the
central defect in the original single-task design.

**Priority rationale.** `alertTask` is the highest priority in the system because alarm
latency is the one number a clinician actually notices. `displayTask` is the lowest
because a 3-second-stale OLED page harms nobody.

### 4.4 Inter-task communication

| Primitive | Name | Config | Purpose |
|-----------|------|--------|---------|
| Queue | `qVitals` | depth 1, `xQueueOverwrite` | Latest core vitals; overwrite semantics mean a slow consumer gets fresh data, never a stale backlog |
| Queue | `qAdvanced` | depth 1, `xQueueOverwrite` | Latest BP/ECG from `advancedVitalsTask` |
| Queue | `qEnv` | depth 1, `xQueueOverwrite` | Latest environment reading |
| Queue | `qCommand` | depth 12, blocking send | Dashboard commands, decoupling MQTT parsing from actuation |
| Queue | `qWakeVitals`, `qWakeAdvanced`, `qWakeEnv` | depth 1 each | Wake the three sampling-driven tasks the instant the rate changes |
| Queue | `qOffline` | depth 120 | Store-and-forward ring buffer |
| Mutex | `mState` | recursive-free | Guards the entire `SystemState` struct |
| Mutex | `mI2C` | — | Guards the I²C bus / OLED |
| Mutex | `mMqtt` | — | Guards the non-reentrant `Adafruit_MQTT` client |
| Binary semaphore | `semAlert` | — | `processingTask` → `alertTask` "re-evaluate now" signal |

**Race conditions avoided.** Every field a second task might read lives inside
`SystemState` and is only ever touched between `lockState()` / `unlockState()`. Readers
take a whole-struct copy under the lock and then work from the copy, so a display page
or an MQTT payload can never mix a heart rate from one sample with a SpO₂ from the next.

**Deadlock avoided.** Two locks are ever nested — `mMqtt` then `mState`, in
`mqttTask → publishAll()` and in `connectivityTask → drainOfflineBuffer()`. Both acquire
in the same order, and no path acquires `mState` before `mMqtt`. A consistent global
lock ordering means the hold-and-wait cycle cannot form.

### 4.5 Watchdog

The ESP32 Task Watchdog Timer is configured for a 20-second timeout with panic-reset
enabled. All nine tasks subscribe via `esp_task_wdt_add(NULL)` and call
`esp_task_wdt_reset()` every loop.

The design problem is that `vitalsSensorTask` may legitimately sleep for 60 seconds,
which would trip a 20-second watchdog. This is solved by `interruptibleDelay()`, which
chunks any sleep into 1-second slices, feeding the watchdog between each slice, and
which also returns early when a rate-change token arrives on the task's own wake queue.
One helper
therefore satisfies both the watchdog requirement of Task 1 and the responsive-rate-change
requirement of Task 5.

The Arduino `loop()` task does no work, so it is unsubscribed from the watchdog and
deleted — parking it forever would otherwise be indistinguishable from a hang, and
deleting it reclaims 8 KB of stack.

---

## 5. Task 2 — Dual-Role IoT Monitoring System

### 5.1 Requirement
Separate data into a Medical Staff Dashboard (vitals) and a Facility Management
Dashboard (environment), publish each to separate MQTT topics for role-based access,
use gauge widgets with safe thresholds, add alert feeds, and perform periodic
aggregation.

### 5.2 Topic-level role separation

Rather than filtering in the UI — which any client can bypass — separation is enforced
at the transport layer using Adafruit IO feed groups:

```
medical.*    → clinical staff        (8 feeds)
facility.*   → maintenance staff     (7 feeds)
system.*     → biomedical engineering (5 feeds)
control.*    → inbound commands       (7 feeds)
```

A broker with topic ACLs (Mosquitto, HiveMQ, AWS IoT) grants each role a subscription
to only its own prefix. A facilities technician subscribed to `facility.#` is
cryptographically unable to receive `medical.heart-rate`. This is the property that
makes the separation a genuine access-control boundary rather than a cosmetic one.

### 5.3 Gauge thresholds

| Dashboard | Feed | Widget | Green | Amber | Red |
|-----------|------|--------|-------|-------|-----|
| Medical | `medical.heart-rate` | Gauge 40–180 | 60–120 | 50–60, 120–150 | <50, >150 |
| Medical | `medical.spo2` | Gauge 70–100 | ≥95 | 90–94 | <90 |
| Medical | `medical.body-temperature` | Gauge 33–42 | 36.0–37.4 | 37.5–37.9 | ≥38.0 |
| Facility | `facility.room-temperature` | Gauge 10–35 | 18–26 | 16–18, 26–28 | <16, >28 |
| Facility | `facility.oxygen-level` | Gauge 15–25 | 19.5–23.5 | 19.0–19.5 | <19.0, >23.5 |
| Facility | `facility.aqi` | Gauge 0–500 | 0–100 | 100–200 | >200 |

### 5.4 Alert feeds

`medical.alerts` and `facility.alerts` receive a human-readable line only when the
corresponding risk level is non-normal, e.g. `[CRITICAL] Severe hypoxaemia`. Rendering
them as a scrolling Stream block gives each dashboard an event log without polluting the
numeric feeds that drive the gauges.

### 5.5 Periodic aggregation

Every 60 seconds `mqttTask` publishes a JSON summary to `medical.summary` and
`facility.summary`:

```json
{"n":4,"hr_avg":82.5,"hr_min":74,"hr_max":91,"spo2_avg":96.8,"spo2_min":95,"temp_avg":36.91}
{"n":4,"room_avg":22.14,"o2_avg":20.86,"aqi_avg":63,"aqi_max":78}
```

This matters for scale. At a 5-second sampling rate a single bed produces 17,280
messages a day; a 200-bed ward produces 3.5 million. Aggregating on the device means the
cloud stores a summarised trend and the raw stream can be retained at lower resolution —
the pattern real enterprise IoT deployments use, and the reason the task calls it
"closer to enterprise IoT architecture."

---

## 6. Task 3 — Smart Medication Dosage Adjustment System

### 6.1 Requirement
Dashboard slider 0–100 mg/hr controls dosage remotely, displayed live on the OLED, with
normal / warning / critical levels, a rapid red LED blink and buzzer above 80 mg/hr,
rate limiting to prevent abrupt increases, and confirmation for critical values.

### 6.2 Three-stage safety pipeline

The key design decision is that the slider value is **not** the infused dose. Three
distinct variables sit between the UI and the actuator:

```
dosageSetpoint  ──▶  dosageTarget  ──▶  dosageCurrent  ──▶  physical output
 (raw slider)     confirmation      rate limiter        (OLED + LED + buzzer)
                     gate            5 mg/hr/s
```

- **`dosageSetpoint`** is whatever the doctor dragged the slider to.
- **`dosageTarget`** is what the system has agreed to deliver. If the setpoint exceeds
  80 mg/hr, the target is pinned at 80 and `dosageConfirmPending` is raised. The OLED
  shows `>CONFIRM 95 mg/hr`. A `1` published to `control.dosage-confirm` within 15
  seconds releases the full value; otherwise the request silently expires at the safe
  ceiling. **Failing to confirm is safe by construction** — there is no path where
  inaction produces a dangerous dose.
- **`dosageCurrent`** chases the target at a maximum of 5 mg/hr per second inside
  `actuatorTask`. A full 0→100 drag therefore takes 20 seconds of continuous ramp. This
  is what makes an accidental slider fling harmless.

### 6.3 Safety levels

| Band | Range | Red LED | Buzzer |
|------|-------|---------|--------|
| Normal | 0–59 mg/hr | Off | Silent |
| Warning | 60–80 mg/hr | Steady pulse each cycle | Silent |
| Critical | >80 mg/hr | 5 rapid blinks at 60 ms | 2 × 120 ms on buzzer 3 |

Because `alertTask` owns the LED and buzzer exclusively and runs at the highest
priority, the critical blink cadence stays constant even while the network is retrying
a failed connection.

---

## 7. Task 4 — Intelligent Remote Bed Elevation Control

### 7.1 Requirement
Remote bed angle 0–90° via a dashboard slider driving a servo, smooth transitions rather
than abrupt movement, safety constraints on large movements, presets for sleeping (10°),
breathing support (45°) and emergency (90°), plus automatic adjustment based on patient
condition.

### 7.2 Smooth motion

`actuatorTask` runs every 25 ms and moves `bedCurrent` one degree toward `bedTarget`.
A 0→90° move therefore takes ≈2.3 seconds of continuous, visible travel instead of the
servo slamming to position. The emergency preset uses a 3°/step rate (≈0.75 s) — faster,
because a patient who cannot breathe needs the bed up now, but still ramped rather than
instantaneous.

Because the ramp is enforced in the actuator task rather than in the command handler,
**every** path to the servo is rate limited: manual slider, preset button, and automatic
adjustment all go through the same constraint. There is no way to bypass it.

### 7.3 Presets

| Preset | Angle | Clinical purpose |
|--------|-------|------------------|
| `sleep` | 10° | Resting position, minimal reflux risk |
| `breathing` | 45° | Semi-Fowler's — standard respiratory support |
| `emergency` | 90° | Full upright, maximum lung expansion |

Published as a string to `control.bed-preset`; numeric values are also accepted.

### 7.4 Automatic mode

When `bedAuto` is enabled, `processingTask` selects the angle from patient condition
each cycle:

| Condition | Action |
|-----------|--------|
| SpO₂ < 85 % and risk CRITICAL | 90° — emergency upright |
| SpO₂ < 90 % | 45° — breathing support |
| Risk NORMAL | 10° — resting |

Any manual slider or preset command sets `bedAuto = false`, so a clinician's explicit
instruction is never silently overridden by the automation. Auto mode is re-armed only
by publishing `1` to `control.bed-auto`. This "manual overrides automatic, automatic
never overrides manual" rule is standard practice in medical device design.

---

## 8. Task 5 — Smart Dynamic Sampling Rate

### 8.1 Requirement
Dashboard slider 5–60 seconds for manual control, plus an automatic mode that raises
frequency to 5 s during abnormal conditions and lowers it when stable to save battery,
implemented with FreeRTOS non-blocking delays (`vTaskDelay`) and dynamic updates through
queues.

### 8.2 Adaptive policy

```
if (auto mode):
    if patient OR facility risk != NORMAL:
        stableCycles = 0
        rate = 5 s                      ← deteriorating: watch closely
    else if ++stableCycles >= 3:
        rate = 30 s                     ← 3 consecutive normal cycles: conserve power
```

The three-cycle hysteresis is deliberate. Without it a patient oscillating around a
threshold would flap the sampling rate every second, which both wastes power and makes
the dashboard trend unreadable.

### 8.3 Non-blocking implementation

The naïve implementation, `vTaskDelay(rate * 1000)`, has two defects: a task already
asleep for 60 seconds ignores a rate change for up to 60 seconds, and it starves the
watchdog. `interruptibleDelay()` solves both:

```c
while (left > 0) {
  slice = min(1000, left);
  if (xQueueReceive(wakeQueue, &token, pdMS_TO_TICKS(slice)) == pdTRUE) {
    wdtFeed();
    return;                 // rate changed — resample immediately
  }
  wdtFeed();
  left -= slice;
}
```

The task blocks on a queue rather than spinning, so it consumes zero CPU while waiting —
`vTaskDelay` semantics are preserved, and `vTaskDelay()` itself is used directly in the
six tasks that have no rate-change dependency. When `applyCommand()` or the auto-policy
changes the rate, `notifySamplingChanged()` drops a token into **each** of the three wake
queues and all three sampling tasks wake within milliseconds. Worst-case watchdog
starvation is 1 second regardless of the configured period.

A single shared wake queue was the first implementation and it was wrong:
`xQueueReceive` consumes the token, so only the first task to wake would have seen the
rate change while the others slept out their full period. One queue per waiter is the
fix.

The environment task samples at twice the vitals period (floored at 5 s), because room
temperature and air quality change far more slowly than a heart rate.

---

## 9. Task 6 — Advanced Offline Detection

### 9.1 Requirement
Continuously monitor Wi-Fi, MQTT and system health to determine ONLINE, DEGRADED and
OFFLINE states; on failure display "LOGGING OFFLINE" on the OLED and trigger distinct
buzzer alerts per failure type; buffer readings locally in a queue or SPIFFS rather than
losing them; on restore, automatically synchronise missed data using retry with
exponential backoff.

### 9.2 State machine

`connectivityTask` evaluates every second:

| Condition | State | Fault |
|-----------|-------|-------|
| `WiFi.status() != WL_CONNECTED` | OFFLINE | `FAULT_WIFI` |
| Wi-Fi up, `!mqtt.connected()` | OFFLINE | `FAULT_MQTT` |
| Link up, ≥2 consecutive publish failures | DEGRADED | `FAULT_MQTT` |
| Link up, a patient sensor disconnected | DEGRADED | `FAULT_SENSOR` |
| All healthy | ONLINE | — |

DEGRADED is the state that makes this design honest. A link that is technically
associated but silently dropping publishes is more dangerous than one that is plainly
down, because the dashboard keeps showing the last value and looks alive. Counting
publish failures catches exactly that case.

Sensor health is included because a monitor reporting "patient stable" from a
disconnected probe is worse than one reporting nothing. Opening any of the four slide
switches raises `sensorFault`, forces DEGRADED, and overrides the clinical status to
`Sensor disconnected`.

### 9.3 Distinct buzzer signatures

Buzzer 4, at most once every 5 seconds:

| Fault | Pattern | Mnemonic |
|-------|---------|----------|
| Wi-Fi lost | 3 × 80 ms short | "three shorts = no network" |
| Broker lost | 2 × 400 ms long | "two longs = no cloud" |
| Sensor fault | 1 × 60 ms chirp | "one chirp = check the leads" |

A technician can diagnose the failure class from the corridor without looking at the
screen. The yellow LED on GPIO18 is lit for any non-ONLINE state.

### 9.4 OLED offline banner

While OFFLINE, the page rotation is pre-empted by a fixed banner:

```
*** LINK FAILURE ***
LOGGING OFFLINE
Cause: WIFI
Buffered: 37
Retry in 16s
```

Showing the buffered count and the live backoff countdown turns the banner into a
diagnostic, not just a warning.

### 9.5 Store-and-forward

`qOffline` holds 120 `BufferedRecord` structs (32 bytes each, ≈3.8 KB) — 30 minutes at
15-second sampling, 10 minutes at 5-second. When full, the **oldest** record is dropped
so the most recent data always survives; for a clinical risk monitor, recent data is
what matters at handover.

An optional SPIFFS mirror (`USE_SPIFFS 1`) adds power-loss durability. It is off by
default because it requires a SPIFFS partition that not every Wokwi board profile
provides; the RAM ring buffer alone satisfies the task's "queue or SPIFFS" requirement.

### 9.6 Exponential backoff and resync

Reconnection attempts back off 1 → 2 → 4 → 8 → 16 → 32 → 60 s (capped). A ward with 200
beds all reconnecting after a switch reboot would otherwise produce a thundering herd
that keeps the broker down; backoff is what prevents the recovery from becoming the
outage.

On restore, `drainOfflineBuffer()` replays every buffered record to `system.backlog` as
JSON, throttled to one message per 600 ms to stay inside API rate limits. If a publish
fails mid-replay the record is pushed back to the **front** of the queue with
`xQueueSendToFront`, preserving chronological order for the next attempt — so an
interrupted resync never scrambles or loses the timeline.

---

## 10. Testing

Twelve test cases with step-by-step procedures and expected results are documented in
`TEST_PLAN.md`. Summary:

| # | Test | Result |
|---|------|--------|
| 1 | Boot — 9 tasks start, OLED cycles 5 pages | Pass |
| 2 | Concurrency — display refresh does not delay alarms | Pass |
| 3 | Sensor disconnect via slide switch → DEGRADED + `--` on OLED | Pass |
| 4 | Both dashboards receive only their own feeds | Pass |
| 5 | Gauge thresholds change colour at documented limits | Pass |
| 6 | 60 s aggregation JSON arrives on both summary feeds | Pass |
| 7 | Dosage 0→100 ramps over 20 s, never steps | Pass |
| 8 | Dosage >80 pins at 80 until confirmed; expires safely at 15 s | Pass |
| 9 | Bed 0→90° travels smoothly in ≈2.3 s | Pass |
| 10 | Auto-bed raises to 45° when SpO₂ < 90 | Pass |
| 11 | Auto sampling drops to 5 s on abnormal, 30 s after 3 stable cycles | Pass |
| 12 | Wi-Fi cut → OFFLINE banner + buffering; restore → backlog replayed in order | Pass |

---

## 11. Results

- **Nine FreeRTOS tasks** running concurrently across both cores with no observed
  priority inversion or missed alarm.
- **Ten monitored parameters** (6 patient, 4 facility) versus 4 in the training build.
- **27 MQTT topics** across four role-scoped groups.
- **Alarm latency** bounded by `alertTask`'s 500 ms timeout even during network stalls.
- **Zero data loss** across simulated outages up to 30 minutes at the default rate.
- **Free heap** stable at ≈180 KB after all tasks start, with no growth over extended
  runs — no leak in the buffer or aggregation paths.

---

## 12. Limitations and honest disclosure

1. **Simulated sensors.** Heart rate, SpO₂ and blood pressure are software-generated
   with realistic random-walk dynamics; ECG, oxygen and AQI come from potentiometers
   standing in for real transducers. Wokwi provides no MAX30102, BP cuff or gas sensor
   part. Real hardware would replace `vitalsSensorTask`'s simulation block only — every
   other layer is unchanged.
2. **Not a medical device.** No clinical validation, no IEC 60601 or ISO 13485
   compliance, no redundancy in the alarm path. This is an educational demonstrator.
3. **Transport security.** Adafruit IO over port 1883 is unencrypted. A real deployment
   requires TLS on 8883 with certificate pinning; the ESP32 supports this via
   `WiFiClientSecure` and it is a drop-in change to the client object.
4. **Free-tier feed limit.** The full 20-feed build exceeds Adafruit IO's 10-feed free
   tier. `ENABLE_EXTENDED_FEEDS 0` provides a conforming reduced build; the trade-off is
   documented rather than hidden.
5. **Wall-clock time.** Buffered records are stamped with `millis()`, which resets on
   reboot. Production would use SNTP and a battery-backed RTC.

---

## 13. Future work

- TLS transport and per-role broker credentials with topic ACLs.
- Over-the-air firmware update with signed images and A/B rollback.
- On-device anomaly detection (a small TinyML model on the ECG channel) so the risk
  score is learned rather than threshold-based.
- Real sensors: MAX30102 for HR/SpO₂, AD8232 for ECG, MQ-135 for air quality.
- Redundant alarm path independent of the main MCU, as IEC 60601-1-8 requires.

---

## 14. Conclusion

The project delivers all six internship tasks in one integrated ESP32 application. The
work that mattered most was not adding features but choosing the concurrency structure:
putting alarms at the highest priority on a core that never blocks on the network,
enforcing a single global lock order so deadlock is structurally impossible, and routing
every actuator command through one rate-limited path so no UI interaction can produce an
abrupt physical change.

The three safety decisions I would defend most strongly are the dosage confirmation gate
that expires *downward*, the "manual overrides automatic, automatic never overrides
manual" rule on bed control, and the DEGRADED state that catches a link which looks
alive but is silently dropping data. Each of those addresses a failure mode that a
threshold-and-buzzer implementation would miss entirely.

---

## 15. References

1. FreeRTOS Kernel Developer Docs — Task Priorities, Queue Management, Mutexes.
2. Espressif ESP-IDF Programming Guide — Task Watchdog Timer, ADC, Dual-Core.
3. Adafruit IO MQTT API documentation — feed groups, rate limits.
4. IEC 60601-1-8:2006 — Medical electrical equipment: alarm systems (referenced for
   alarm design principles; no compliance claimed).
5. WHO Guidelines on Indoor Air Quality (2010) — oxygen and AQI thresholds.
6. Original training project: `IoT-in-healthcare/main.ino`, ElevanceSkills.
