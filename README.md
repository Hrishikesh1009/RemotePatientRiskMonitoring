# Remote Patient Risk Monitoring System

**Live Wokwi project (public, no login):** https://wokwi.com/projects/473060533558119425
**Source:** https://github.com/Hrishikesh1009/RemotePatientRiskMonitoring

**ElevanceSkills Internship — Final Project**
Intern: **Hrishikesh Lokhande** · Domain: IoT / Embedded Systems
Platform: ESP32 + FreeRTOS + MQTT (HiveMQ / Adafruit IO) + Wokwi + Node-RED

A single integrated ESP32 system that continuously monitors a hospital patient and their
room, scores clinical risk in real time, lets clinicians remotely control medication
dosage and bed elevation from a dashboard, adapts its own sampling rate to the patient's
condition, and keeps logging without data loss when the network drops.

All **six internship tasks are implemented in one project**, as the portal requires — not
six separate sketches.

---

## Task coverage

| # | Task | Where it lives | Measured evidence |
|---|------|----------------|-------------------|
| 1 | Multi-Sensor Expansion Using FreeRTOS | 10 tasks, §7–§15 of `main.ino`; **BP and ECG each in their own task** | 10 queues, 3 mutexes, 1 semaphore, 20 s watchdog; no reset in the longest run (TC-01, TC-16) |
| 2 | Dual-Role IoT Monitoring System | `medical.*` / `facility.*` topic groups, §3 + §15 | two dashboards, threshold gauges, alert feeds, 60 s aggregates (TC-04, 05, 06) |
| 3 | Smart Medication Dosage Adjustment | `applyCommand()` §16, `actuatorTask()` §12, OLED footer §11 | ramp measured **5.00 mg/hr/s**; >80 held until confirmed; red LED + buzzer (TC-07, 08, 14) |
| 4 | Intelligent Remote Bed Elevation Control | `actuatorTask()` §12, auto logic §9 | servo 0–90°, 1°/25 ms ramp, presets 10/45/90°, auto from SpO₂, manual wins (TC-09, 10) |
| 5 | Smart Dynamic Sampling Rate | `interruptibleDelay()` §6, §9, §16 | 5–60 s slider; auto 5 s ↔ 30 s with 3-sample hysteresis; `vTaskDelay` slices + wake queues (TC-11) |
| 6 | Advanced Offline Detection | `connectivityTask()` §14, §13 | ONLINE/DEGRADED/OFFLINE; "LOGGING OFFLINE"; per-fault buzzers; 120-record buffer; backoff; in-order resync; **on-demand fault switches** (TC-12, 13) |

Clause-by-clause: [`docs/TASK_MAPPING.md`](docs/TASK_MAPPING.md). Full report:
[`docs/TECHNICAL_REPORT.md`](docs/TECHNICAL_REPORT.md).

---

## Quick start

**▶ https://wokwi.com/projects/473060533558119425** — open it and press Play. It ships
pointed at the public `broker.hivemq.com` with topic prefix `rprms-hl-8842`, so no account
or key is needed. Serial prints `[BOOT] 10 FreeRTOS tasks started`; the OLED cycles five
pages with a **dose read-out on every screen**; within a few seconds `[NET] state OFFLINE -> ONLINE`.

For the dashboards, start Node-RED and open `http://127.0.0.1:1880/ui` (details in
[`docs/NODE_RED_SETUP.md`](docs/NODE_RED_SETUP.md)):

```bash
tools/start-dashboard.cmd
```

> **Switching brokers.** `main.ino` has a `USE_HIVEMQ` switch at the top. Set it to `0` to
> use Adafruit IO instead (fill in `IO_USERNAME` / `IO_KEY`; see
> [`docs/DASHBOARD_SETUP.md`](docs/DASHBOARD_SETUP.md)).

> **With no broker reachable the project still runs** — it boots into OFFLINE mode, shows
> the `LOGGING OFFLINE` banner and buffers samples. That is Task 6 demonstrating itself.

---

## Try each task in 30 seconds

Commands are MQTT messages to `rprms-hl-8842/feeds/<feed>`. Use the Node-RED sliders, or any
MQTT client (e.g. HiveMQ's web client, `mosquitto_pub`). The slide switches sit in the top
row of the Wokwi circuit, **left to right: Temp · HR · SpO₂ · PIR · Wi-Fi cut · Broker cut**;
sliding one to the right applies the fault.

| To see… | Do this | You should see |
|---|---|---|
| **T1** concurrency | press Play | `[BOOT] 10 FreeRTOS tasks started`, `task watchdog armed: 20 s` |
| **T1/T6** probe fault | slide the **HR** switch right | `[SENSOR] probe change …`, `[NET] state ONLINE -> DEGRADED (fault: SENSOR)` within ≈ 3 s, amber LED, one chirp |
| **T2** alert feed + gauge | drag the **bottom potentiometer** (AQI) to the right | `facility.aqi` > 300, alert `[CRITICAL] Air quality hazardous` |
| **T3** rate limit | `control.dosage` = 60 | OLED footer climbs 0 → 60 at 5 mg/hr per second |
| **T3** critical gate | `control.dosage` = 95 | held at 80, `>CONFIRM 95 mg/hr`, footer `CONF`, red LED blinks + buzzer; `control.dosage-confirm` = 1 within 15 s releases it, otherwise it stays at 80 |
| **T4** smooth bed | `control.bed-angle` = 90 (or `control.bed-preset` = `emergency`) | servo horn sweeps in ≈ 2.3 s (emergency ≈ 0.75 s) |
| **T4** auto bed | `control.bed-auto` = 1, then `control.sim-spo2` = 88 / 82 / 0 | bed → 45° / 90° / back to 10° |
| **T5** adaptive rate | `control.sampling-mode` = 1, then `control.sim-hr` = 130, then 0 | `[RATE] auto-adapt: 30 s -> 5 s`, and back to 30 s after 3 stable samples |
| **T5** manual rate | `control.sampling-rate` = 5 / 45 / 60 | `[RATE] manual: N s`; a sleeping task wakes within ≈ 1 s |
| **T6** Wi-Fi outage | slide **Wi-Fi cut** right, wait ≈ 30 s, slide it back | OLED `LOGGING OFFLINE`, `Cause: WIFI`, buffered count rising, backoff 1·2·4·8·16 s, 3-beep pattern; on release `[NET] restored -- replaying N …` then `resynced N records` |
| **T6** broker outage | slide **Broker cut** right, then back | same, `Cause: MQTT`, 2-beep pattern |

`control.sim-hr` / `control.sim-spo2` (0 = automatic) pin the **simulated** heart rate and
SpO₂ — Wokwi has no pulse-oximeter part — so the risk engine, adaptive sampling and
automatic bed can be shown on demand. Click the DS18B20 to set a body temperature (≥ 38 °C is
CRITICAL), drag the three potentiometers for ECG / oxygen / AQI.

---

## Hardware / pin map

| GPIO | Component | Purpose | Task |
|------|-----------|---------|------|
| 14 | DS18B20 | Patient body temperature | inherited |
| 27 | DHT22 | Room temperature + humidity | T1, T2 |
| 12 | PIR | Patient movement / bed exit | inherited |
| 36 (VP) | Potentiometer | ECG amplitude 0–5 mV | T1 |
| 34 | Potentiometer | Room oxygen 15–25 % | T2 |
| 35 | Potentiometer | Air Quality Index 0–500 | T2 |
| 4 | Buzzer 1 | Body temperature / facility alarm | inherited |
| 16 | Buzzer 2 | Heart rate alarm | inherited |
| 17 | Buzzer 3 | SpO₂ + medication alarm | T3 |
| 5 | Buzzer 4 | Motion + connectivity alarm | T6 |
| 32 / 33 / 25 / 26 | Slide switches | Simulate temp / HR / SpO₂ / PIR probe disconnect | T1, T6 |
| **23 / 39 (VN)** | **Slide switches** | **Fault injection: cut Wi-Fi / broker unreachable** | **T6** |
| 21 / 22 | I²C | SSD1306 OLED | inherited |
| 13 | SG90 servo | Bed elevation 0–90° | T4 |
| 19 | Red LED | Medication critical | T3 |
| 2 | Green LED | System-healthy heartbeat | — |
| 18 | Yellow LED | Degraded / offline | T6 |

> **Wokwi pin-naming note.** On Wokwi's ESP32 DevKit-V1, GPIO16 and GPIO17 are named `RX2`
> and `TX2` (there is no `D16`/`D17`), so in `diagram.json` those buzzers are wired to
> `esp:RX2` / `esp:TX2`; the board has only `GND.1` and `GND.2`; and the medication LED is on
> GPIO19 rather than the strapping pin GPIO15. Evidence: `docs/BUG_LOG.md` (B-14, B-15, B-16).

Blood pressure is derived from heart rate with physiological correlation rather than a
separate sensor — Wokwi has no BP cuff part, and the task asks for the *parameter*, not a
specific transducer. Blood pressure and ECG are each handled by their **own** FreeRTOS
task (`bloodPressureTask`, `ecgTask`), as Task 1 asks for the added parameters to be "each
managed by separate FreeRTOS tasks."

---

## MQTT topic map — role-separated dashboards (Task 2)

```
<user>/feeds/medical.{body-temperature,heart-rate,spo2,blood-pressure,ecg,patient-status,alerts,summary}   Medical Staff Dashboard
<user>/feeds/facility.{room-temperature,humidity,oxygen-level,aqi,motion,alerts,summary}                   Facility Management Dashboard
<user>/feeds/system.{state,dosage,bed-angle,sampling-rate,backlog}                                         device telemetry / readback
<user>/feeds/control.dosage            ← slider 0–100 mg/hr                  (T3)
<user>/feeds/control.dosage-confirm    ← button, confirms >80                (T3)
<user>/feeds/control.bed-angle         ← slider 0–90°                        (T4)
<user>/feeds/control.bed-preset        ← sleep | breathing | emergency       (T4)
<user>/feeds/control.bed-auto          ← toggle                              (T4)
<user>/feeds/control.sampling-rate     ← slider 5–60 s                       (T5)
<user>/feeds/control.sampling-mode     ← toggle auto / manual                (T5)
<user>/feeds/control.sim-hr / sim-spo2 ← pin the simulated HR / SpO₂, 0 = auto   (demo aid)
```

> **What's actually enforced, and what isn't.** The two dashboards read from disjoint topic
> prefixes, which is genuinely necessary groundwork for role-based access — but **it is not
> role-based access by itself**, and none is enforced by this project as shipped. The default
> broker (`broker.hivemq.com`, public) has no ACLs: any client can subscribe to `medical.*`
> regardless of which dashboard it is meant to use. Turning this into real access control
> means configuring topic ACLs on whichever broker is actually deployed (Mosquitto ACL
> file, HiveMQ Cloud per-credential permissions, AWS IoT Core policies, …) — infrastructure
> this firmware does not include. See `docs/TASK_MAPPING.md` Task 2 for the full caveat.

### ⚠️ Adafruit IO free tier allows only 10 feeds
The full build uses 20 publish feeds + 9 control feeds. Options: set
`#define ENABLE_EXTENDED_FEEDS 0` (the 10 essential feeds; aggregation is then compiled
out — see [`docs/DASHBOARD_SETUP.md`](docs/DASHBOARD_SETUP.md)), pay for Adafruit IO Plus, or
use any generic broker (HiveMQ, Mosquitto) + Node-RED — `Adafruit_MQTT` is a plain MQTT
client, so only `AIO_SERVER`, `AIO_SERVERPORT` and the feed macros need editing.

---

## Repository layout

```
RemotePatientRiskMonitoring/
├── main.ino                     Complete firmware (single file, ~2,000 lines)
├── diagram.json                 Wokwi circuit — 26 parts, 64 connections
├── libraries.txt                Wokwi / Arduino library dependencies
├── wokwi.toml                   Optional VS Code simulation config
├── README.md                    This file
├── docs/
│   ├── TECHNICAL_REPORT.md      Technical report (portal sections: overview … conclusions)
│   ├── TASK_MAPPING.md          Clause-by-clause evidence for the six tasks
│   ├── ARCHITECTURE.md          Design rationale, RTOS table, MQTT topic map
│   ├── TEST_PLAN.md             16 test cases with expected results
│   ├── TEST_RESULTS.md          Measured results of the final run
│   ├── BUG_LOG.md               Every defect found, cause, fix, verification  (mandatory)
│   ├── ASSUMPTIONS.md           Interpretations, simulation and clinical assumptions  (mandatory)
│   ├── SELF_ATTESTATION.md      Self-attestation checklist — for the intern to complete  (mandatory)
│   ├── BUILD_VERIFICATION.md    Compile evidence and reproduction steps
│   ├── NODE_RED_SETUP.md        Free dashboard route — import and deploy
│   ├── DASHBOARD_SETUP.md       Step-by-step Adafruit IO / HiveMQ dashboard build
│   ├── DEMO_VIDEO_SCRIPT.md     Shot-by-shot recording script
│   ├── SUBMISSION_CHECKLIST.md  What to upload and where
│   ├── architecture-diagram.svg System architecture
│   ├── workflow-diagram.svg     Runtime workflow / state machines
│   └── evidence/                Serial logs, MQTT capture and JSON results of the final test run
├── dashboards/
│   ├── node-red-flows.json      Pre-built dashboard — import & deploy (84 nodes)
│   ├── medical-dashboard.md     Medical Staff Dashboard block spec
│   └── facility-dashboard.md    Facility Management Dashboard block spec
├── tools/
│   ├── verification/            Measurement rig behind docs/TEST_RESULTS.md (suite.ps1, supplement.ps1, README)
│   ├── start-dashboard.cmd      Launch the Node-RED dashboard
│   ├── sim_esp32.py             Publish the ESP32's topics without Wokwi
│   └── gen_flows.py             Regenerate the dashboard flow
└── screenshots/                 12 required + 3 bonus, named per screenshots/README.md
```

---

## Build and verification status

* **Builds on Wokwi's cloud toolchain** from exactly the files in this repository
  (verified 2026-10-13: HTTP 200, firmware image 1,600,201 bytes; `main.ino` SHA-256
  `9e69da71…`, full hashes in `docs/evidence/tested_files_sha256.txt`). Flash/RAM percentages
  of the final revision were not measured — see [`docs/BUILD_VERIFICATION.md`](docs/BUILD_VERIFICATION.md).
* **Verified in simulation** — the 16 test cases of `docs/TEST_PLAN.md`, run on the real
  simulated firmware as **49 automated checks, all passing, plus 4 supplementary pixel-level
  checks** (serial timestamps, simulator clock, OLED framebuffer, LED/buzzer/servo state, an
  independent MQTT subscriber). Results, the run history and what could *not* be exercised:
  [`docs/TEST_RESULTS.md`](docs/TEST_RESULTS.md); raw logs in `docs/evidence/`.
* **Not exercised:** DEGRADED caused by publish failures and the failing-write branch of the
  B-38 watchdog fix (no simulator control stalls the link on demand), and the optional SPIFFS
  mirror (`USE_SPIFFS 0`).
* **Environment note.** Wokwi's free IoT gateway / the public broker occasionally stop answering
  the simulated device after ~10–15 simulated minutes. The firmware shows `LOGGING OFFLINE` and
  keeps retrying; if it never recovers, press **Restart** in Wokwi.

---

## Honest limitations

* Role-based access is **topic separation only** (see above).
* Heart rate, SpO₂ and BP are **simulated**; thresholds are illustrative, not clinically
  validated; this is an educational demonstrator, **not a medical device**.
* The default broker is public and unencrypted; no real patient data exists anywhere.
* Buffered records are stamped with `millis()` (no RTC/SNTP).
* On the slow test machine the public broker / Wokwi gateway occasionally dropped the MQTT
  session; the firmware detected it, buffered and resynced on its own (`docs/BUG_LOG.md`).

---

## Submission status

- [x] Final firmware, circuit and libraries are saved in the public Wokwi project and run there
      logged out (hash-checked, `docs/evidence/wokwi_public_project_verification.txt`)
- [x] Verified in simulation — `docs/TEST_RESULTS.md`
- [x] Node-RED dashboards import and run against the live device
- [x] Mandatory documents present: README, Bug Log, Assumptions, Self-Attestation, report
- [ ] Self-Attestation Checklist completed and signed **by the intern**
- [ ] Demo video recorded and linked here — see [`docs/DEMO_VIDEO_SCRIPT.md`](docs/DEMO_VIDEO_SCRIPT.md)
- [ ] Final portal submission (GitHub link, live URL, report) — one-time and irreversible;
      see [`docs/SUBMISSION_CHECKLIST.md`](docs/SUBMISSION_CHECKLIST.md)

---

## Notes on the original training sketch

This project extends the training build (`IoT-in-healthcare/main.ino`). The original pin
assignments, Adafruit IO transport, OLED/DS18B20/PIR/buzzer wiring and the
sensor→queue→processing structure are preserved. Three deliberate corrections:

1. **Dead threshold.** The original tested `heartRate > 300` while its source was
   `random(45, 300)` — the branch could never fire. Replaced with clinical limits
   (brady < 50, tachy > 150 critical; < 60 / > 120 warning).
2. **`temperature` read uninitialised.** In the original, if `SW_TEMP` was open,
   `temperature` was used before ever being assigned. Now explicitly `NAN` with a
   `tempValid` flag, and the OLED prints `--`.
3. **MQTT in the processing task.** Blocking network I/O sat on the same task as display
   and alarm logic, so a stalled broker froze the alarms. Network I/O now lives on its own
   task on core 0; alarms run at the highest priority on core 1.

---

## License

Submitted as internship coursework for ElevanceSkills, 2026.
