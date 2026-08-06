# Task Mapping — Evidence for Evaluation

Each of the six internship tasks, quoted verbatim from the portal, with the exact
implementation that satisfies every clause. Section numbers (§) refer to the banner
comments in `main.ino`.

---

## Task 1 — Multi-Sensor Expansion Using FreeRTOS (Concurrent System Design)

> *"The system is extended by adding additional health parameters such as blood pressure
> or ECG monitoring, each managed by separate FreeRTOS tasks. Multiple tasks—including
> sensor reading, environment monitoring, MQTT communication, alert handling, and display
> updates—run concurrently using priority-based scheduling. Inter-task communication is
> handled using queues, semaphores, and mutexes to ensure data consistency and avoid race
> conditions. A watchdog timer is implemented for system reliability."*

| Clause | Implementation | Location |
|--------|----------------|----------|
| Additional health parameters — blood pressure | `bpSystolic` / `bpDiastolic`, HR-correlated | §7b `advancedVitalsTask` |
| Additional health parameters — ECG | `ecgMv` from ADC1 pot on GPIO36 | §7b `advancedVitalsTask` |
| **"each managed by separate FreeRTOS tasks"** | BP and ECG are **not** in the core vitals loop — they have their own task, fed from `qVitals` and producing `qAdvanced` | §7b |
| Environment parameters | DHT22 room temp/RH + O₂ + AQI pots | §8 `envSensorTask` |
| Sensor reading task | `vitalsSensorTask`, prio 4, core 1 | §7 |
| Environment monitoring task | `envSensorTask`, prio 3, core 1 | §8 |
| MQTT communication task | `mqttTask`, prio 3, core 0 | §15 |
| Alert handling task | `alertTask`, prio 6, core 1 | §10 |
| Display update task | `displayTask`, prio 2, core 1 | §11 |
| *(plus)* risk engine | `processingTask`, prio 5, core 0 | §9 |
| *(plus)* actuator ramp | `actuatorTask`, prio 4, core 1 | §12 |
| *(plus)* link supervision | `connectivityTask`, prio 3, core 0 | §14 |
| Priority-based scheduling | 6 distinct priority levels, both cores | §17 `setup()` |
| Queues | `qVitals`, `qAdvanced`, `qEnv`, `qCommand`, `qOffline` + 3 wake queues (8 total) | §5 |
| Semaphores | `semAlert` binary semaphore | §5, §9, §10 |
| Mutexes | `mState`, `mI2C`, `mMqtt` | §5 |
| Data consistency | Whole-struct copy under `mState` — no torn reads | §6 `lockState()` |
| Race conditions avoided | Every shared field lives in `SystemState`, lock-guarded | §4, §6 |
| Watchdog timer | `esp_task_wdt` 20 s, panic reset, all 9 tasks subscribed | §17 `initWatchdog()` |

**Deadlock argument.** Only one nesting exists — `mMqtt` then `mState` — and it occurs in
exactly two places (`mqttTask → publishAll()`, `connectivityTask → drainOfflineBuffer()`).
Both acquire in the same order and no path acquires `mState` first, so the circular-wait
condition cannot form.

**Watchdog vs. long sleeps.** `vitalsSensorTask` may sleep 60 s, which would starve a
20 s watchdog. `interruptibleDelay()` (§6) chunks the sleep into 1 s slices and feeds the
watchdog between each. Worst-case starvation is therefore 1 s at any configured rate.

---

## Task 2 — Dual-Role IoT Monitoring System (Role-Based Dashboards)

> *"The system separates data into two logical dashboards: a Medical Staff Dashboard
> displaying patient vitals (heart rate, SpO₂, body temperature) and a Facility Management
> Dashboard showing environmental conditions (room temperature, oxygen level, AQI). Each
> dataset is published to separate MQTT topics, enabling role-based access and monitoring.
> Gauge widgets are used with predefined safe thresholds, and additional alert feeds are
> implemented for abnormal conditions. The system also performs periodic data aggregation."*

| Clause | Implementation | Location |
|--------|----------------|----------|
| Medical Staff Dashboard — heart rate | `medical.heart-rate` | §3, §15 |
| Medical Staff Dashboard — SpO₂ | `medical.spo2` | §3, §15 |
| Medical Staff Dashboard — body temperature | `medical.body-temperature` | §3, §15 |
| Facility Dashboard — room temperature | `facility.room-temperature` | §3, §15 |
| Facility Dashboard — oxygen level | `facility.oxygen-level` | §3, §15 |
| Facility Dashboard — AQI | `facility.aqi` | §3, §15 |
| Separate MQTT topics | Four disjoint prefixes: `medical.` `facility.` `system.` `control.` | §3 |
| Role-based access | Topic-prefix ACLs at the broker — a `facility.#` subscriber cannot receive patient data | §3 header comment |
| Gauge widgets with safe thresholds | 6 gauges, thresholds tabulated | `DASHBOARD_SETUP.md` |
| Alert feeds for abnormal conditions | `medical.alerts` / `facility.alerts`, published only when risk ≠ NORMAL | §15 `publishAll()` |
| Periodic data aggregation | 60 s avg/min/max JSON to `medical.summary` / `facility.summary` | §15 `aggAdd()`, `aggPublish()` |

**Why aggregation matters.** At 5 s sampling one bed emits 17,280 messages/day; a 200-bed
ward emits 3.5 M. Summarising on-device is what makes the architecture scale, which is
the task's "closer to enterprise IoT architecture" clause.

---

## Task 3 — Smart Medication Dosage Adjustment System

> *"A dashboard slider (0–100 mg/hr) allows doctors to remotely control medication dosage,
> which is displayed in real-time on the OLED. The system includes safety levels: normal,
> warning, and critical. If the dosage exceeds 80 mg/hr, a red LED blinks rapidly and a
> buzzer alerts for safety. To prevent sudden changes, rate-limiting logic restricts abrupt
> dosage increases, and critical values may require confirmation."*

| Clause | Implementation | Location |
|--------|----------------|----------|
| Dashboard slider 0–100 mg/hr | `control.dosage`, clamped to `[0,100]` | §16 `CMD_DOSAGE` |
| Remote control by doctors | MQTT subscribe → `qCommand` → `applyCommand()` | §15, §16 |
| Real-time OLED display | Page 3 "THERAPY CONTROL" shows `dosageCurrent` | §11 |
| Safety level — normal | < 60 mg/hr, LED off, silent | §10 |
| Safety level — warning | 60–80 mg/hr, steady LED pulse | §10 |
| Safety level — critical | > 80 mg/hr | §10 |
| Red LED blinks rapidly above 80 | 5 blinks at 60 ms on/off, GPIO15 | §10 |
| Buzzer alerts above 80 | 2 × 120 ms on buzzer 3 (GPIO17) | §10 |
| Rate limiting on abrupt increases | `DOSAGE_RAMP_MGH_S = 5.0` mg/hr per second in `actuatorTask` | §12 |
| Critical values require confirmation | `dosageConfirmPending`, 15 s window, `control.dosage-confirm` | §16, §12 |

**Three-stage pipeline.** `dosageSetpoint` (raw slider) → `dosageTarget` (after the
confirmation gate) → `dosageCurrent` (after the rate limiter). The slider value is never
the infused dose.

**Fails safe.** If the doctor never confirms, the request expires *downward* to the 80
mg/hr ceiling. There is no path where inaction produces a dangerous dose.

---

## Task 4 — Intelligent Remote Bed Elevation Control

> *"The system enables remote adjustment of bed angle (0–90°) using a dashboard slider
> connected to a servo motor. Instead of abrupt movement, the servo transitions smoothly
> between angles to ensure patient comfort and safety. Safety constraints are applied to
> prevent sudden large movements, and predefined modes such as sleeping (10°), breathing
> support (45°), and emergency (90°) are included. Additionally, the system can
> automatically adjust the bed angle based on patient condition."*

| Clause | Implementation | Location |
|--------|----------------|----------|
| Remote adjustment 0–90° | `control.bed-angle`, clamped `[0,90]` | §16 `CMD_BED_ANGLE` |
| Dashboard slider → servo | SG90 on GPIO13 via ESP32Servo | §5, §17 |
| Smooth transitions, not abrupt | 1° per 25 ms in `actuatorTask` → 0–90° in ≈2.3 s | §12 |
| Safety constraints on large moves | The ramp *is* the constraint; every path goes through it | §12 |
| Preset — sleeping 10° | `BED_PRESET_SLEEP` | §2, §16 |
| Preset — breathing support 45° | `BED_PRESET_BREATH` | §2, §16 |
| Preset — emergency 90° | `BED_PRESET_EMERGENCY`, 3°/step faster ramp | §2, §12, §16 |
| Automatic adjustment by condition | SpO₂ < 85 + CRITICAL → 90°; SpO₂ < 90 → 45°; NORMAL → 10° | §9 |

**Manual overrides automatic.** Any manual slider or preset command sets
`bedAuto = false`. Auto mode is re-armed only by an explicit `control.bed-auto` publish.
A clinician's instruction is never silently reversed by the automation.

---

## Task 5 — Smart Dynamic Sampling Rate (Adaptive Monitoring)

> *"The sampling rate is controlled through a dashboard slider ranging from 5 to 60
> seconds, allowing doctors to manually adjust monitoring frequency. In addition, an
> automatic mode intelligently adapts the sampling rate based on patient vitals—switching
> to high-frequency monitoring (5 seconds) during abnormal conditions and reducing
> frequency during stable periods to optimize battery life. This is implemented using
> FreeRTOS with non-blocking delays (vTaskDelay) and dynamic updates through queues."*

| Clause | Implementation | Location |
|--------|----------------|----------|
| Slider 5–60 seconds | `control.sampling-rate`, clamped `[5,60]` | §16 `CMD_SAMPLING_RATE` |
| Manual adjustment by doctors | Setting a rate also sets `samplingAuto = false` | §16 |
| Automatic mode | `control.sampling-mode` toggle | §16 `CMD_SAMPLING_MODE` |
| High frequency (5 s) when abnormal | `worst != RISK_NORMAL → SAMPLING_ABNORMAL_S` | §9 |
| Reduced frequency when stable | 3 consecutive normal cycles → `SAMPLING_STABLE_S` (30 s) | §9 |
| Battery-life optimisation | Fewer wakeups, fewer radio transmissions, fewer sensor reads | §9 |
| Non-blocking delays (`vTaskDelay`) | `vTaskDelay()` used directly in 6 tasks; the 3 sampling tasks use `interruptibleDelay()`, which blocks on a queue with a timeout — the same descheduled, zero-CPU wait, but wakeable | §6 |
| Dynamic updates through queues | `notifySamplingChanged()` posts a token to `qWakeVitals`, `qWakeAdvanced` and `qWakeEnv`; all three wake in ms | §6 |

**Hysteresis.** Three stable cycles are required before slowing down. Without it a
patient hovering at a threshold would flap the rate every second, wasting power and
making the dashboard trend unreadable.

---

## Task 6 — Advanced Offline Detection (Fault-Tolerant System)

> *"The ESP32 continuously monitors Wi-Fi, MQTT, and system health to determine three
> states: ONLINE, DEGRADED, and OFFLINE. If Wi-Fi or MQTT fails, the system enters offline
> mode, displays "LOGGING OFFLINE" on the OLED, and triggers distinct buzzer alerts
> depending on the failure type. Instead of losing data, sensor readings are stored in a
> local buffer using a queue or SPIFFS. Once connectivity is restored, the device
> automatically synchronizes all missed data to the cloud using a retry mechanism with
> exponential backoff."*

| Clause | Implementation | Location |
|--------|----------------|----------|
| Continuously monitors Wi-Fi | `WiFi.status()` polled at 1 Hz | §14 |
| Continuously monitors MQTT | `mqtt.connected()` + publish-failure counter | §14, §15 |
| Continuously monitors system health | `sensorFault` flag from the four slide switches | §9, §14 |
| Three states ONLINE / DEGRADED / OFFLINE | `NetState` enum, full transition table | §4, §14 |
| Wi-Fi or MQTT fails → offline mode | `!wifiUp` or `!mqttUp` → `NET_OFFLINE` | §14 |
| "LOGGING OFFLINE" on OLED | Banner pre-empts page rotation; shows cause, buffered count, retry countdown | §11 |
| Distinct buzzer per failure type | Wi-Fi 3×80 ms · MQTT 2×400 ms · sensor 1×60 ms | §10 |
| Readings stored in a local buffer | `qOffline`, 120 records, ring (drops oldest) | §5, §13 |
| ... using a queue or SPIFFS | FreeRTOS queue primary; optional SPIFFS mirror via `USE_SPIFFS` | §13 |
| Automatic synchronisation on restore | `drainOfflineBuffer()` triggered on the OFFLINE → up transition | §14 |
| Retry with exponential backoff | 1·2·4·8·16·32·60 s, capped | §14 |

**Why DEGRADED earns its place.** A link that is associated but silently dropping
publishes is more dangerous than one that is plainly down, because the dashboard keeps
showing the last value and looks alive. Counting consecutive publish failures catches
exactly that.

**Ordering preserved across a failed resync.** If a publish fails mid-replay, the record
is returned to the **front** of the queue with `xQueueSendToFront`, so an interrupted
resync never scrambles the timeline.

---

## Cross-task integration

The tasks are not six bolted-together features — several share machinery:

- `interruptibleDelay()` satisfies both T1's watchdog requirement and T5's fast
  rate-change response.
- `actuatorTask` enforces the rate limits for both T3 (dosage) and T4 (bed angle).
- `processingTask`'s risk score drives T5's adaptive rate *and* T4's automatic bed angle.
- The `sensorFault` flag from T1's slide switches feeds T6's DEGRADED state.
- T2's `system.backlog` feed is the destination for T6's resync.
