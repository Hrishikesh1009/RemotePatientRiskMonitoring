# Task Mapping — Evidence for Evaluation

Each of the six internship tasks, quoted verbatim from the portal, with the exact
implementation that satisfies every clause and the test that demonstrates it. Section
numbers (§) refer to the banner comments in `main.ino`; `TC-nn` refers to
[`TEST_RESULTS.md`](TEST_RESULTS.md) (procedures in [`TEST_PLAN.md`](TEST_PLAN.md)).

> **How to see each task working in 30 seconds** — open the Wokwi project, press Play, and:
> T1 serial shows `[BOOT] 10 FreeRTOS tasks started` · T2 open the Node-RED dashboards
> (`docs/NODE_RED_SETUP.md`) · T3 publish `control.dosage = 95` · T4 publish
> `control.bed-preset = emergency` · T5 publish `control.sim-hr = 130` · T6 slide
> `swWifiCut` (the 5th switch from the left). Details in the README.

---

## Task 1 — Multi-Sensor Expansion Using FreeRTOS (Concurrent System Design)

> *"The system is extended by adding additional health parameters such as blood pressure
> or ECG monitoring, each managed by separate FreeRTOS tasks. Multiple tasks—including
> sensor reading, environment monitoring, MQTT communication, alert handling, and display
> updates—run concurrently using priority-based scheduling. Inter-task communication is
> handled using queues, semaphores, and mutexes to ensure data consistency and avoid race
> conditions. A watchdog timer is implemented for system reliability, ensuring that the
> system remains stable, responsive, and capable of handling real-time healthcare
> monitoring without failure."*

| Clause | Implementation | Location | Test |
|--------|----------------|----------|------|
| Additional parameter — blood pressure | `systolic` / `diastolic`, derived from heart rate | §7b `bloodPressureTask` | TC-01 |
| Additional parameter — ECG | `mv` from the ADC1 pot on GPIO36 | §7c `ecgTask` | TC-05d |
| **"each managed by separate FreeRTOS tasks"** | Blood pressure and ECG each have **their own task and their own queue** (`qBp`, `qEcg`); neither is in the core vitals loop and they do not share a task | §7b, §7c | TC-01 |
| Sensor reading task | `vitalsSensorTask`, prio 4, core 1 | §7 | TC-01 |
| Environment monitoring task | `envSensorTask`, prio 3, core 1 | §8 | TC-05 |
| MQTT communication task | `mqttTask`, prio 3, core 0 | §15 | TC-06, TC-15 |
| Alert handling task | `alertTask`, prio 6, core 1 | §10 | TC-08 |
| Display update task | `displayTask`, prio 2, core 1 | §11 | TC-01, TC-14 |
| *(plus)* risk engine | `processingTask`, prio 5, core 0 | §9 | TC-10, TC-11 |
| *(plus)* actuator ramp | `actuatorTask`, prio 4, core 1 | §12 | TC-07, TC-09 |
| *(plus)* link supervision | `connectivityTask`, prio 3, core 0 | §14 | TC-12, TC-13 |
| Priority-based scheduling | 5 distinct priority levels (6/5/4/3/2), split across both cores | §17 `setup()` | TC-02 |
| Queues | 6 data/command queues (`qVitals`, `qBp`, `qEcg`, `qEnv`, `qCommand`, `qOffline`) + 4 wake queues = **10** | §5 | TC-01 |
| Semaphores | `semAlert` binary semaphore (risk engine → alert task) | §5, §9, §10 | TC-08 |
| Mutexes | `mState`, `mI2C`, `mMqtt` | §5 | TC-01 |
| Data consistency | Whole-struct copy under `mState` — no torn reads | §6 `lockState()` | — |
| Race conditions avoided | Every shared field lives in `SystemState`, lock-guarded | §4, §6 | — |
| Watchdog timer | `esp_task_wdt` 20 s, panic reset, all 10 tasks subscribed; boot log prints `task watchdog armed: 20 s` | §17 `initWatchdog()` | TC-01, TC-16 |
| "system … responsive" | Probe switches watched at 1 Hz; a disconnect is reported in ≈ 3 s | §9 | TC-03 |

**Deadlock argument.** Only one nesting exists — `mMqtt` then `mState` — and it occurs in
exactly two places (`mqttTask → publishAll()`, `connectivityTask → drainOfflineBuffer()`).
Both acquire in the same order and no path acquires `mState` first, so the circular-wait
condition cannot form.

**Watchdog vs. long sleeps.** A sampling task may wait 60 s, which would starve a 20 s
watchdog. `interruptibleDelay()` (§6) waits in 100 ms `vTaskDelay()` slices and feeds the
watchdog every slice, so worst-case starvation is 100 ms at any configured rate.

---

## Task 2 — Dual-Role IoT Monitoring System (Role-Based Dashboards)

> *"The system separates data into two logical dashboards: a Medical Staff Dashboard
> displaying patient vitals (heart rate, SpO₂, body temperature) and a Facility Management
> Dashboard showing environmental conditions (room temperature, oxygen level, AQI). Each
> dataset is published to separate MQTT topics, enabling role-based access and monitoring.
> Gauge widgets are used with predefined safe thresholds, and additional alert feeds are
> implemented for abnormal conditions. The system also performs periodic data aggregation."*

| Clause | Implementation | Location | Test |
|--------|----------------|----------|------|
| Medical Staff Dashboard — heart rate, SpO₂, body temperature | `medical.heart-rate`, `medical.spo2`, `medical.body-temperature` (+ BP, ECG, status) | §3, §15 | TC-04 |
| Facility Dashboard — room temperature, oxygen level, AQI | `facility.room-temperature`, `facility.oxygen-level`, `facility.aqi` (+ humidity, motion) | §3, §15 | TC-04, TC-05 |
| Separate MQTT topics | Disjoint prefixes `medical.` `facility.` `system.` `control.` — 29 topics | §3 | TC-04 |
| Role-based access | **Topic separation only — broker-enforced access control is not part of this project.** See the caveat below. | §3 | — |
| Gauge widgets with safe thresholds | 6 threshold-banded gauges (Node-RED) | `dashboards/node-red-flows.json`, `DASHBOARD_SETUP.md` | TC-05 |
| Alert feeds for abnormal conditions | `medical.alerts` / `facility.alerts`, published when risk ≠ NORMAL | §15 `publishAll()` | TC-05 |
| Periodic data aggregation | 60 s avg/min/max JSON to `medical.summary` / `facility.summary`; `n` = samples in the window | §15 `aggAdd()`, `aggPublish()` | TC-06 |

**Why aggregation matters.** At 5 s sampling one bed emits 17,280 messages/day; a 200-bed
ward emits 3.5 M. Summarising on-device is what makes the architecture scale — the task's
"closer to enterprise IoT architecture" clause.

**Role-based access — what is and isn't actually enforced.** The firmware publishes
`medical.*` and `facility.*` as genuinely disjoint topic namespaces, and that separation is
a real, necessary precondition for role-based access. But **no access control exists in
this repo and the shipped default does not enforce separation**:

- `USE_HIVEMQ 1` (the default in `main.ino`) points at the public `broker.hivemq.com`, which
  has no ACLs: a client subscribed to `facility.#` **can** also subscribe to `medical.#`.
- Making the separation real needs ACLs on whatever broker is actually deployed (Mosquitto
  ACL file, HiveMQ Cloud per-credential permissions, AWS IoT Core policies) so that a
  facilities credential is denied `medical.#`. That is infrastructure, not firmware, and is
  not part of this submission.

**Bottom line:** the *architecture* is role-separation-ready and the two Node-RED tabs
subscribe only to their own prefix by convention. *Role-based access itself* is not
implemented or demonstrated and should not be claimed as complete. If the rubric requires
the access control itself, that is the one clause in this task that is genuinely open.

---

## Task 3 — Smart Medication Dosage Adjustment System

> *"A dashboard slider (0–100 mg/hr) allows doctors to remotely control medication dosage,
> which is displayed in real-time on the OLED. The system includes safety levels: normal,
> warning, and critical. If the dosage exceeds 80 mg/hr, a red LED blinks rapidly and a
> buzzer alerts for safety. To prevent sudden changes, rate-limiting logic restricts abrupt
> dosage increases, and critical values may require confirmation."*

| Clause | Implementation | Location | Test |
|--------|----------------|----------|------|
| Dashboard slider 0–100 mg/hr | `control.dosage`, clamped to `[0,100]` | §16 `CMD_DOSAGE` | TC-07 |
| Remote control by doctors | MQTT subscribe → `qCommand` → `applyCommand()` | §15, §16 | TC-15 |
| Displayed in real time on the OLED | **Persistent footer on every OLED screen** (`DOSE 60.0 mg/hr WARN`), whole screen redrawn at 1 Hz; page 4 also shows it with the level word | §11 `oledFooter()` | TC-14 |
| Safety level — normal | < 60 mg/hr, LED off, silent (`NORM`) | §10, §11 | TC-07 |
| Safety level — warning | 60–80 mg/hr, slow LED pulse (`WARN`) | §10, §11 | TC-07 |
| Safety level — critical | > 80 mg/hr (`CRIT`), or a > 80 request awaiting confirmation (`CONF`) | §10, §11 | TC-08 |
| Red LED blinks rapidly above 80 | 5 blinks at 60 ms on/off, **GPIO19** — also while a > 80 request awaits confirmation | §10 | TC-08 |
| Buzzer alerts above 80 | 2 × 120 ms on buzzer 3 (GPIO17) | §10 | TC-08 |
| Rate limiting on abrupt increases | `DOSAGE_RAMP_MGH_S = 5.0` mg/hr per second in `actuatorTask` — **measured 5.00 mg/hr/s** | §12 | TC-07 |
| Critical values require confirmation | `dosageConfirmPending`, 15 s window, `control.dosage-confirm` | §16, §12 | TC-08 |

**Three-stage pipeline.** `dosageSetpoint` (raw slider) → `dosageTarget` (after the
confirmation gate) → `dosageCurrent` (after the rate limiter). The slider value is never
the infused dose.

**Fails safe.** If the doctor never confirms, the request expires *downward* to the 80
mg/hr ceiling. There is no path where inaction produces a dangerous dose (TC-08 A).

---

## Task 4 — Intelligent Remote Bed Elevation Control

> *"The system enables remote adjustment of bed angle (0–90°) using a dashboard slider
> connected to a servo motor. Instead of abrupt movement, the servo transitions smoothly
> between angles to ensure patient comfort and safety. Safety constraints are applied to
> prevent sudden large movements, and predefined modes such as sleeping (10°), breathing
> support (45°), and emergency (90°) are included. Additionally, the system can
> automatically adjust the bed angle based on patient condition."*

| Clause | Implementation | Location | Test |
|--------|----------------|----------|------|
| Remote adjustment 0–90° | `control.bed-angle`, clamped `[0,90]` | §16 `CMD_BED_ANGLE` | TC-09 |
| Slider → servo | SG90 on GPIO13 via ESP32Servo; pulse range calibrated to Wokwi's SG90 (horn shows the commanded angle within +2/−1°) | §5, §17 | TC-09 |
| Smooth transitions, not abrupt | 1° per 25 ms in `actuatorTask` — 0→90° in ≈ 2.3 s (measured) | §12 | TC-09a |
| Safety constraints on large moves | The ramp *is* the constraint; every path (slider, preset, auto) goes through it | §12 | TC-09 |
| Preset — sleeping 10° / breathing 45° / emergency 90° | `BED_PRESET_SLEEP/BREATH/EMERGENCY`; emergency ramps 3°/step (≈ 0.75 s) but is still a ramp | §2, §16, §12 | TC-09c, TC-09d |
| Automatic adjustment by condition | SpO₂ < 85 + CRITICAL → 90°; SpO₂ < 90 → 45°; NORMAL → 10° | §9 | TC-10 |

**Manual overrides automatic.** Any manual slider or preset command sets `bedAuto = false`.
Auto mode is re-armed only by an explicit `control.bed-auto` publish — a clinician's
instruction is never silently reversed (TC-10d). Because Wokwi has no pulse-oximeter part,
`control.sim-hr` / `control.sim-spo2` pin the simulated vitals so the automatic mode can be
shown on demand (TC-10a–c).

---

## Task 5 — Smart Dynamic Sampling Rate (Adaptive Monitoring)

> *"The sampling rate is controlled through a dashboard slider ranging from 5 to 60
> seconds, allowing doctors to manually adjust monitoring frequency. In addition, an
> automatic mode intelligently adapts the sampling rate based on patient vitals—switching
> to high-frequency monitoring (5 seconds) during abnormal conditions and reducing
> frequency during stable periods to optimize battery life. This is implemented using
> FreeRTOS with non-blocking delays (vTaskDelay) and dynamic updates through queues."*

| Clause | Implementation | Location | Test |
|--------|----------------|----------|------|
| Slider 5–60 seconds | `control.sampling-rate`, clamped `[5,60]` | §16 `CMD_SAMPLING_RATE` | TC-11a |
| Manual adjustment by doctors | Setting a rate also sets `samplingAuto = false` | §16 | TC-11 |
| Automatic mode | `control.sampling-mode` toggle | §16 `CMD_SAMPLING_MODE` | TC-11d/e |
| High frequency (5 s) when abnormal | `worst != RISK_NORMAL → SAMPLING_ABNORMAL_S` | §9 | TC-11d |
| Reduced frequency when stable | 3 consecutive **fresh** normal samples → `SAMPLING_STABLE_S` (30 s) | §9 | TC-11e |
| Battery-life optimisation | Fewer wakeups, sensor reads and radio transmissions; one publish per sample, keepalive only when idle | §9, §15 | TC-11b |
| **Non-blocking delays (`vTaskDelay`)** | The sampling wait is built from 100 ms `vTaskDelay()` slices — the task is descheduled, no busy-wait | §6 `interruptibleDelay()` | TC-11 |
| **Dynamic updates through queues** | `notifySamplingChanged()` posts a token to each task's wake queue; the task polls its own queue without blocking and resamples within ≤ 100 ms (measured ≈ 1 s end to end, vs up to 60 s un-woken) | §6 | TC-11c |
| Exact cadence | The loop subtracts its own work time; measured **5.08 s** between samples at "5 s" | §7 | TC-11a |

**Hysteresis.** Three fresh stable samples are required before slowing down, so a patient
hovering at a threshold cannot flap the rate.

---

## Task 6 — Advanced Offline Detection (Fault-Tolerant System)

> *"The ESP32 continuously monitors Wi-Fi, MQTT, and system health to determine three
> states: ONLINE, DEGRADED, and OFFLINE. If Wi-Fi or MQTT fails, the system enters offline
> mode, displays "LOGGING OFFLINE" on the OLED, and triggers distinct buzzer alerts
> depending on the failure type. Instead of losing data, sensor readings are stored in a
> local buffer using a queue or SPIFFS. Once connectivity is restored, the device
> automatically synchronizes all missed data to the cloud using a retry mechanism with
> exponential backoff."*

| Clause | Implementation | Location | Test |
|--------|----------------|----------|------|
| Continuously monitors Wi-Fi | `WiFi.status()` polled at 1 Hz | §14 | TC-12 |
| Continuously monitors MQTT | `mqtt.connected()` + idle-only keepalive + publish-failure counter | §14, §15 | TC-13 |
| Continuously monitors system health | Probe switches watched at 1 Hz → `sensorFault` | §9, §14 | TC-03 |
| Three states ONLINE / DEGRADED / OFFLINE | `NetState` enum; every transition is logged as `[NET] state A -> B (fault: X)` | §4, §14 | TC-03, TC-12 |
| Wi-Fi or MQTT fails → offline mode | `!wifiUp` or `!mqttUp` → `NET_OFFLINE` | §14 | TC-12, TC-13 |
| **Repeatable fault injection** | `swWifiCut` (GPIO23) cuts the real Wi-Fi link, `swBrokerCut` (GPIO39) closes the real MQTT session; each is logged as `[FAULT] …` | §14 | TC-12, TC-13 |
| "LOGGING OFFLINE" on OLED | Banner pre-empts page rotation; shows cause, buffered count, **live retry countdown**, plus the dose footer | §11 | TC-12 |
| Distinct buzzer per failure type | Wi-Fi 3 × 80 ms · MQTT 2 × 400 ms · sensor 1 × 60 ms (each pattern also traced as `[ALERT] buzzer 4, … fault: …`) | §10 | TC-12c, TC-13b |
| Readings stored in a local buffer | `qOffline`, 120 records, ring (drops oldest) | §5, §13 | TC-12e |
| … using a queue or SPIFFS | FreeRTOS queue primary; optional SPIFFS mirror via `USE_SPIFFS` | §13 | — |
| Automatic synchronisation on restore | `drainOfflineBuffer()` on the OFFLINE → up transition, in order, to `system.backlog` | §14 | TC-12e |
| Retry with exponential backoff | 1·2·4·8·16·32·60 s, capped; reset when a layer recovers | §14 | TC-12b, TC-12f |

**Why DEGRADED earns its place.** A link that is associated but silently dropping publishes
is more dangerous than one that is plainly down, because the dashboard keeps showing the
last value and looks alive. DEGRADED via a disconnected probe is exercised (TC-03); the
publish-failure branch is implemented but no simulator control can make publishes fail
while the link stays up, so it is **not** exercised (see `BUG_LOG.md`, open items).

**Ordering preserved across a failed resync.** If a publish fails mid-replay the record
goes back to the **front** of the queue (`xQueueSendToFront`), so an interrupted resync
never scrambles the timeline.

---

## Cross-task integration

The tasks are not six bolted-together features — several share machinery:

- `interruptibleDelay()` satisfies both T1's watchdog requirement and T5's fast
  rate-change response.
- `actuatorTask` enforces the rate limits for both T3 (dosage) and T4 (bed angle).
- `processingTask`'s risk score drives T5's adaptive rate *and* T4's automatic bed angle.
- The probe flags from T1's slide switches feed T6's DEGRADED state.
- T2's `system.backlog` feed is the destination for T6's resync.
- The OLED footer shows the T3 dose on every screen, including T6's offline banner, so
  local control stays visible while the network is down.
