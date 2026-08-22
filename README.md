# Remote Patient Risk Monitoring System

**Live Wokwi project:** https://wokwi.com/projects/473060533558119425

**ElevanceSkills Internship — Final Project**
Intern: **Hrishikesh Lokhande** · Domain: IoT / Embedded Systems
Platform: ESP32 + FreeRTOS + MQTT (HiveMQ / Adafruit IO) + Wokwi

A single integrated ESP32 system that continuously monitors a hospital patient and
their room, scores clinical risk in real time, lets clinicians remotely control
medication dosage and bed elevation from a dashboard, adapts its own sampling rate
to patient condition, and keeps logging without data loss when the network drops.

All **six internship tasks are implemented in one project**, as the portal instructions
require — not six separate sketches.

---

## Task coverage

| # | Task | Where it lives | Proof it works |
|---|------|----------------|----------------|
| 1 | Multi-Sensor Expansion Using FreeRTOS | 9 tasks, §7–§15 of `main.ino` | BP + ECG in their **own** task (`advancedVitalsTask`), room env in another; 8 queues, 3 mutexes, binary semaphore, 20 s watchdog |
| 2 | Dual-Role IoT Monitoring System | `medical.*` / `facility.*` feed groups, §3 + §15 | Two separate dashboards, gauge thresholds, alert feeds, 60 s aggregation |
| 3 | Smart Medication Dosage Adjustment | `applyCommand()` §16, `actuatorTask()` §12 | 0–100 mg/hr slider → OLED, red LED + buzzer above 80, rate limiting, confirmation gate |
| 4 | Intelligent Remote Bed Elevation Control | `actuatorTask()` §12, auto logic §9 | Servo 0–90°, smooth ramp, 3 presets, condition-driven auto mode |
| 5 | Smart Dynamic Sampling Rate | `interruptibleDelay()` §6, §9, §16 | 5–60 s slider, auto mode 5 s ↔ 30 s, `vTaskDelay` + queue wake |
| 6 | Advanced Offline Detection | `connectivityTask()` §14, §13 | ONLINE/DEGRADED/OFFLINE, "LOGGING OFFLINE" OLED, per-fault buzzers, 120-sample buffer, exponential backoff resync |

Full detail: [`docs/TASK_MAPPING.md`](docs/TASK_MAPPING.md).

---

## Build status

**Compiles clean** against arduino-cli 1.5.2 / esp32:esp32 core 3.3.11
(`esp32:esp32:esp32doit-devkit-v1`):

```
Sketch uses 980546 bytes (74%) of program storage space. Maximum is 1310720 bytes.
Global variables use 53840 bytes (16%) of dynamic memory, leaving 273840 bytes for local variables.
```

0 errors, 0 sketch warnings on a clean rebuild with `--warnings all`.
Full toolchain details and reproduction steps: [`docs/BUILD_VERIFICATION.md`](docs/BUILD_VERIFICATION.md).

**Dashboard verified end-to-end.** Node-RED loads the bundled flow with 0 errors,
serves `/ui`, receives live telemetry into every widget, and its sliders/buttons publish
`control.*` messages in the exact format `applyCommand()` parses. Details in
[`docs/NODE_RED_SETUP.md`](docs/NODE_RED_SETUP.md).

Firmware runtime behaviour still needs the simulator — work through
[`docs/TEST_PLAN.md`](docs/TEST_PLAN.md) TC-01…TC-12 in Wokwi before submitting.

---

## Repository layout

```
RemotePatientRiskMonitoring/
├── main.ino                     Complete firmware (single file, ~950 lines)
├── diagram.json                 Wokwi circuit — 24 parts, 58 connections
├── libraries.txt                Wokwi / Arduino library dependencies
├── wokwi.toml                   Optional VS Code simulation config
├── README.md                    This file
├── docs/
│   ├── PROJECT_REPORT.md        Full project report
│   ├── BUILD_VERIFICATION.md    Compile evidence, toolchain, repro steps
│   ├── NODE_RED_SETUP.md        Free dashboard route — import and deploy
│   ├── TASK_MAPPING.md          Task-by-task evidence with line references
│   ├── ARCHITECTURE.md          Design rationale, RTOS table, MQTT topic map
│   ├── DASHBOARD_SETUP.md       Step-by-step Adafruit IO dashboard build
│   ├── TEST_PLAN.md             12 test cases with expected results
│   ├── DEMO_VIDEO_SCRIPT.md     Shot-by-shot 6-minute recording script
│   ├── SUBMISSION_CHECKLIST.md  What to upload and where
│   ├── architecture-diagram.svg System architecture
│   └── workflow-diagram.svg     Runtime workflow / state machines
├── dashboards/
│   ├── node-red-flows.json      Pre-built dashboard — import & deploy (78 nodes)
│   ├── medical-dashboard.md     Medical Staff Dashboard block spec
│   └── facility-dashboard.md    Facility Management Dashboard block spec
├── tools/
│   ├── start-dashboard.cmd      Launch the Node-RED dashboard
│   ├── sim_esp32.py             Publish the ESP32's topics without Wokwi
│   └── gen_flows.py             Regenerate the dashboard flow
├── screenshots/                 7 captured, 5 Wokwi shots outstanding
└── submission_pdfs/             Google Drive folder, ready to upload
```

---

## Quick start

The project is already published and runs as-is:

**▶ https://wokwi.com/projects/473060533558119425** — open it and press Play.

It ships pointed at the public `broker.hivemq.com` with topic prefix `rprms-hl-8842`,
so no account or API key is needed.

For the dashboard, start Node-RED and open `http://127.0.0.1:1880/ui`:

```bash
tools/start-dashboard.cmd
```

The OLED shows `RPRMS booting...` then cycles five status pages; serial prints
`[BOOT] 9 FreeRTOS tasks started`. Within ~15 s `system.state` reads **ONLINE**.

> **Switching brokers.** `main.ino` has a `USE_HIVEMQ` switch at the top. Set it to `0`
> to use Adafruit IO instead and fill in your username/key — see
> [`docs/DASHBOARD_SETUP.md`](docs/DASHBOARD_SETUP.md).

> **With no broker reachable the project still runs** — it boots into OFFLINE mode, shows
> the `LOGGING OFFLINE` banner and buffers samples. That is Task 6 demonstrating itself.

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
| 4 | Buzzer 1 | Body temperature alarm | inherited |
| 16 | Buzzer 2 | Heart rate alarm | inherited |
| 17 | Buzzer 3 | SpO₂ + medication alarm | T3 |
| 5 | Buzzer 4 | Motion + connectivity alarm | T6 |
| 32 / 33 / 25 / 26 | Slide switches | Simulate temp / HR / SpO₂ / PIR disconnect | T6 |
| 21 / 22 | I²C | SSD1306 OLED | inherited |
| 13 | SG90 servo | Bed elevation 0–90° | T4 |
| 15 | Red LED | Medication critical | T3 |
| 2 | Green LED | System-healthy heartbeat | — |
| 18 | Yellow LED | Degraded / offline | T6 |

Blood pressure is derived from heart rate with physiological correlation rather than a
separate sensor — Wokwi has no BP cuff part, and the task asks for the *parameter*, not
a specific transducer. This is stated openly in the report.

Note that BP and ECG are handled by their own FreeRTOS task (`advancedVitalsTask`), not
appended to the core vitals loop — Task 1 asks for the added parameters to be "each
managed by separate FreeRTOS tasks."

---

## MQTT topic map — role-based access (Task 2)

```
<user>/feeds/medical.body-temperature     ┐
<user>/feeds/medical.heart-rate           │  Medical Staff Dashboard
<user>/feeds/medical.spo2                 │  (clinical staff only)
<user>/feeds/medical.blood-pressure       │
<user>/feeds/medical.ecg                  │
<user>/feeds/medical.patient-status       │
<user>/feeds/medical.alerts               │
<user>/feeds/medical.summary              ┘

<user>/feeds/facility.room-temperature    ┐
<user>/feeds/facility.humidity            │  Facility Management Dashboard
<user>/feeds/facility.oxygen-level        │  (maintenance staff only)
<user>/feeds/facility.aqi                 │
<user>/feeds/facility.motion              │
<user>/feeds/facility.alerts              │
<user>/feeds/facility.summary             ┘

<user>/feeds/system.state | .dosage | .bed-angle | .sampling-rate | .backlog

<user>/feeds/control.dosage           ← slider 0–100 mg/hr        (T3)
<user>/feeds/control.dosage-confirm   ← button, confirms >80      (T3)
<user>/feeds/control.bed-angle        ← slider 0–90°              (T4)
<user>/feeds/control.bed-preset       ← sleep|breathing|emergency (T4)
<user>/feeds/control.bed-auto         ← toggle                    (T4)
<user>/feeds/control.sampling-rate    ← slider 5–60 s             (T5)
<user>/feeds/control.sampling-mode    ← toggle auto/manual        (T5)
```

Because the three read-side groups sit under distinct topic prefixes, a broker with
topic ACLs grants each role only its own subtree — that is the "role-based access"
the task asks for, implemented at the transport layer rather than in the UI.

### ⚠️ Adafruit IO free tier allows only 10 feeds

The full build above uses 20 publish feeds + 7 control feeds. Options:

- **Free tier:** set `#define ENABLE_EXTENDED_FEEDS 0` in `main.ino`. This drops to the
  10 essential feeds; the exact list is in [`docs/DASHBOARD_SETUP.md`](docs/DASHBOARD_SETUP.md).
- **Adafruit IO Plus** (~$10/mo): full build, no code change.
- **Any generic broker** (HiveMQ, Mosquitto) + Node-RED: unlimited topics. The
  `Adafruit_MQTT` client is a plain MQTT client, so only `AIO_SERVER`, `AIO_SERVERPORT`
  and the feed macros need editing.

---

## What to change before you submit

1. `IO_USERNAME` / `IO_KEY` → your Adafruit IO credentials.
2. Build the two dashboards using [`docs/DASHBOARD_SETUP.md`](docs/DASHBOARD_SETUP.md).
3. Capture screenshots into `screenshots/`.
4. Record the demo using [`docs/DEMO_VIDEO_SCRIPT.md`](docs/DEMO_VIDEO_SCRIPT.md).
5. Work through [`docs/SUBMISSION_CHECKLIST.md`](docs/SUBMISSION_CHECKLIST.md).

---

## Notes on the original training sketch

This project extends the training build (`IoT-in-healthcare/main.ino`). The original
pin assignments, Adafruit IO transport, OLED/DS18B20/PIR/buzzer wiring and the
sensor→queue→processing structure are all preserved. Three deliberate corrections:

1. **Dead threshold.** The original tested `heartRate > 300` while its source was
   `random(45, 300)` — the branch could never fire. Replaced with clinical limits
   (brady < 50, tachy > 150 critical; < 60 / > 120 warning).
2. **`temperature` read uninitialised.** In the original, if `SW_TEMP` was open,
   `temperature` was used before ever being assigned. Now explicitly `NAN` with a
   `tempValid` flag, and the OLED prints `--`.
3. **MQTT in the processing task.** Blocking network I/O sat on the same task as
   display and alarm logic, so a stalled broker froze the alarms. Network I/O now
   lives on its own task on core 0; alarms run at the highest priority on core 1.

---

## License

Submitted as internship coursework for ElevanceSkills, 2026.
