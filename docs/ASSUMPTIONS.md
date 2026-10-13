# Assumptions Document

Every place where the brief left room for interpretation, or where the simulation
environment forced a choice, is recorded here — together with what was decided and why.
Nothing in this file is hidden elsewhere: where an assumption weakens a claim, the
corresponding report section says so.

---

## A. Interpretation of the task brief

| # | Brief says | Interpreted as | Why |
|---|------------|----------------|-----|
| A1 | Task 1 — BP / ECG "each managed by separate FreeRTOS tasks" | **Each** added parameter has its **own** task and queue: `bloodPressureTask` → `qBp`, `ecgTask` → `qEcg`. Neither is in the core vitals loop. | "Each … separate" is read literally. (An earlier revision used one shared task for both; it was split on 2026-10-13 — see `BUG_LOG.md` C-1.) |
| A2 | Task 2 — "separate MQTT topics, enabling role-based access" | Two **role-separated dashboards on disjoint topic namespaces** (`medical.*`, `facility.*`). Broker-enforced access control is **not** implemented. | The brief says separate topics *enable* role-based access. Enforcing it needs per-user credentials and ACLs on the broker — infrastructure outside the firmware. The shipped default is the public `broker.hivemq.com`, which has no ACLs. |
| A3 | Task 3 — "critical values may require confirmation" | Any request **above 80 mg/hr** is held at 80 until confirmed within **15 s**; if no confirmation arrives it stays at 80. The critical alarm (rapid red LED + buzzer) sounds while a confirmation is pending. | "May require" leaves the policy open. Failing *down* to the safe ceiling means silence can never produce an overdose. |
| A3b | Task 3 — dosage "displayed in real-time on the OLED" | The dose and its level are shown in a **persistent footer on every OLED screen** (including the offline banner), redrawn at **1 Hz**. | One of five rotating pages would show the dose for only 3 s in every 15 s, which is not "real time". |
| A4 | Task 4 — "automatically adjust the bed angle based on patient condition" | SpO₂ < 90 → 45°, SpO₂ < 85 with critical risk → 90° (fast ramp), back to 10° when risk returns to NORMAL. **Any manual bed command switches auto mode off**; auto never overrides a manual command. | The brief doesn't define precedence. A clinician's explicit instruction should not be silently reversed by automation. |
| A4b | Task 4 — "emergency (90°)" mode | The emergency *mode* (preset, or auto-mode critical SpO₂) ramps at 3°/25 ms; **every other move, including a slider dragged to 90°, ramps at 1°/25 ms.** | "Emergency" is a mode, not an angle; ramp speed follows the mode (`BUG_LOG.md`, found by TC-09). |
| A5 | Task 5 — "reducing frequency during stable periods" | **30 s** after **3 consecutive normal samples** (fresh readings, not loop ticks). | The brief fixes only the 5 s abnormal rate and the 5–60 s range. 30 s sits mid-range; 3 samples stop a patient hovering at a threshold from flapping the rate. |
| A5b | Task 5 — "non-blocking delays (vTaskDelay)" | The sampling wait is a loop of 100 ms `vTaskDelay()` slices that polls the task's own wake queue without blocking. | Literal reading: `vTaskDelay` for the wait, a queue for dynamic updates. |
| A6 | Task 6 — OFFLINE on "Wi-Fi or MQTT fails" | OFFLINE is entered **only** for a Wi-Fi or broker failure. A disconnected *patient probe* (temperature / heart rate / SpO₂) gives **DEGRADED**, never OFFLINE. The PIR is not a patient-parameter probe: opening its switch only hides movement. | Matches the brief literally, and it is the correct engineering distinction: a dead probe doesn't mean data can't be sent. |
| A7 | Task 6 — "stored in a local buffer using a queue or SPIFFS" | A **RAM queue** (120 records ≈ 30 min at 15 s, 10 min at 5 s). SPIFFS persistence exists behind `USE_SPIFFS` but is **off by default** and not exercised. | "Queue *or* SPIFFS" — the queue alone satisfies it. SPIFFS needs a partition the Wokwi board profile may not have. |
| A8 | Task 6 — "synchronizes all missed data to the cloud" | Buffered records are replayed in original order to the `system.backlog` feed as JSON. They are **not** re-published into the original `medical.*` / `facility.*` feeds. | Re-publishing old values into live feeds would make dashboard gauges jump backwards in time. A separate backlog feed keeps the timeline honest. |
| A9 | Task 6 — "triggers distinct buzzer alerts depending on the failure type" | Buzzer 4: Wi-Fi 3 × 80 ms, broker 2 × 400 ms, probe fault 1 × 60 ms; each pattern is also traced to serial as `[ALERT] buzzer 4, … fault: …`. | Beep counts distinguish the classes without a screen. |

## B. Hardware and simulation assumptions

| # | Assumption |
|---|------------|
| B1 | Wokwi has no pulse-oximeter, blood-pressure cuff or gas-sensor part. **Heart rate and SpO₂ are software-generated** (a mean-reverting random walk around 78 bpm / 97 % with rare episodes). **Blood pressure is derived from heart rate** with a physiological correlation. Real hardware would replace only the simulation block inside `vitalsSensorTask`. |
| B2 | **ECG amplitude, room oxygen and AQI come from potentiometers** standing in for real transducers. Body temperature (DS18B20), room temperature/humidity (DHT22) and motion (PIR) are real Wokwi sensor parts. |
| B3 | Four slide switches simulate a probe being unplugged (temperature, heart rate, SpO₂, motion). |
| B4 | **Two further slide switches are fault-injection controls** (`swWifiCut` GPIO23, `swBrokerCut` GPIO39). They act on the *real* connection — `WiFi.disconnect()` / `mqtt.disconnect()` — and the recovery attempt is reported as failed while the switch is held. Releasing a switch is treated as a link-up event (one immediate retry). Wokwi has no network-off control; restarting with a wrong SSID wipes the RAM buffer. They are simulation aids, not part of a real product. |
| B5 | `control.sim-hr` / `control.sim-spo2` **pin the simulated heart rate / SpO₂** (0 = automatic). They exist so Tasks 4 and 5 can be demonstrated on demand; on real hardware the values would come from a probe. |
| B6 | The simulator runs **slower than real time** (≈ 45–90 % on the test machine). All durations in the report (15 s confirm window, 25 ms servo step, backoff) are in **simulated** time, which is what `millis()` and the serial `t=…` stamps measure. |
| B7 | Wi-Fi is Wokwi's virtual open network `Wokwi-GUEST`, routed through Wokwi's public IoT gateway. The gateway and the public broker occasionally drop the MQTT session; the firmware treats that as any other outage. |
| B8 | Timestamps on buffered records use `millis()`, which resets on reboot. A deployment would use SNTP and a battery-backed RTC. |
| B9 | The servo is attached with a pulse range of 565–2396 µs, **calibrated against Wokwi's SG90 model** so the simulated horn shows the commanded angle (within +2/−1°). The nominal datasheet range showed up to 6° of error (`BUG_LOG.md` B-31). |

## C. Clinical and facility thresholds

| # | Assumption |
|---|------------|
| C1 | All thresholds are **illustrative engineering values, not clinically validated limits**: body temp warn 37.5 °C / critical 38.0 °C; heart rate warn < 60 or > 120, critical ≤ 50 or ≥ 150; SpO₂ warn < 90, critical < 85; systolic BP warn > 140 or < 90; room temperature 18–26 °C; oxygen 19.5–23.5 %; AQI warn 150, critical 300. |
| C2 | This is an **educational demonstrator, not a medical device**. No IEC 60601 / ISO 13485 claim is made and the alarm path is not redundant. |
| C3 | Medication dosage is a **simulated infusion**: 0–100 mg/hr slider, warning band from 60, critical above 80, ramp limited to 5 mg/hr per second. |

## D. Network and dashboard assumptions

| # | Assumption |
|---|------------|
| D1 | The default broker is the **public** `broker.hivemq.com:1883` under the private-ish prefix `rprms-hl-8842`. Traffic is unencrypted and anyone who guesses the prefix could read it. **No real patient data exists anywhere in this project.** |
| D2 | Dashboards are **Node-RED (dashboard v1)** served at `http://127.0.0.1:1880/ui` — local to the machine running them, not publicly hosted. The publicly reachable "live" artefact is the **Wokwi project**; the dashboards are evidenced by screenshots and the importable flow (`dashboards/node-red-flows.json`). |
| D3 | The Adafruit IO transport of the original training project is preserved behind `USE_HIVEMQ 0`. The free tier's 10-feed cap means the full 29-feed build needs `ENABLE_EXTENDED_FEEDS 0` there, which disables aggregation. |
| D4 | A dashboard slider is trusted as an input source; **no authentication** protects the `control.*` topics on the public broker. |
| D5 | The Node-RED gauges have three colour bands; the firmware has warning and critical levels. Where the two differ (e.g. heart rate 120–150), the firmware's alert feed and OLED are authoritative. |

## E. Out of scope

- TLS (port 8883) and per-role broker credentials / topic ACLs.
- Over-the-air updates, persistent storage of history, multi-patient support.
- RTL / HDL / ModelSim: this project is **embedded firmware**, not hardware description
  (see `TECHNICAL_REPORT.md` §3 and §4 for how the portal's "RTL Design" and "FSM Design"
  sections are mapped).
