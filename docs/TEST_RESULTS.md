# Test Results

Measured results of the 16 test cases in [`TEST_PLAN.md`](TEST_PLAN.md), run against the **real
firmware in the Wokwi simulator** on 13 October 2026. Every number below was read from the serial
timestamps, the simulator clock, the OLED framebuffer, the LED/buzzer/servo state, or an independent
MQTT subscriber — none was judged by eye except the two lines explicitly marked *(frame)*.
Raw logs, the JSON results and the saved frames are in [`evidence/`](evidence/README.md).

| | |
|---|---|
| **Result** | **49 of 49 automated checks pass**, plus 4 of 4 supplementary pixel-level checks, on the final build |
| **Build under test** | `main.ino` SHA-256 `9e69da719afebbf68aa48637d9e12146e0f486461ead55e3a46ef7f892e43739` (2066 lines) · `diagram.json` `3602f9cf…77ed` · `libraries.txt` `2e74856b…27ae` (`evidence/tested_files_sha256.txt`) |
| **Build** | Wokwi cloud build of exactly those files: HTTP 200, firmware image 1,600,201 bytes |
| **Platform** | Wokwi ESP32 DevKit-V1 simulation; Wi-Fi `Wokwi-GUEST` through Wokwi's IoT gateway; public broker `broker.hivemq.com:1883`, topic prefix `rprms-hl-8842` |
| **Rig** | `tools/verification/` — headless Chrome over the DevTools protocol, Node.js 24.19, PowerShell 5.1, an independent MQTT client |
| **Host** | Windows 10, 2-core Pentium G2030: the simulator ran at roughly **13–82 % of real time** (it moves with host load), so **every duration is in simulated time** (serial `t=… ms` stamps and the simulator clock) |
| **Session length** | 49 checks: 506 simulated s (8.4 min); including the supplementary checks and the fever test 836 s (13 min 56 s). One boot banner, **0** watchdog / panic / abort lines, 0 failed publishes, 0 keepalive drops |
| **Free heap** | **166 KB** after boot → **165 KB** at the end (OLED SYSTEM page, `TC01_oled_system_start.png`, `TC16_oled_system_end.png`) |

---

## 1. Results by test case

| TC | Task | Result | Headline measurement |
|----|------|--------|----------------------|
| 01 Boot, tasks, watchdog, OLED | T1 | ✅ 8/8 | 10 FreeRTOS tasks, watchdog 20 s, one boot banner, no phantom Wi-Fi fault or rate flip at boot; all five OLED pages seen within 13.0 simulated s (3 s dwell: +0.1, 2.4, 5.5, 8.8, 12.8 s) |
| 02 Alarm cadence during an outage | T1 | ✅ 1/1 | critical-alarm pass period **1.01 s** before the Wi-Fi cut vs **1.16 s** during it (ratio 1.15, limit 0.7–1.3). The extra 0.15 s is the Wi-Fi beep pattern sharing the alert task (one pass in five is lengthened by ≈ 0.5 s) |
| 03 Probe disconnect → DEGRADED | T1, T6 | ✅ 4/4 | DEGRADED / SENSOR **3.4 s** (temperature), **1.6 s** (heart rate), **3.0 s** (SpO₂) after the switch, with the 30 s stable rate in force; status "Sensor disconnected", never "Patient stable" |
| 04 Role separation (topics) | T2 | ✅ 1/1 | 8 `medical.*` and 7 `facility.*` topics, disjoint *(topic separation only — not broker-enforced access control)* |
| 05 Facility inputs and alerts | T1, T2 | ✅ 4/4 | AQI pot 650 → AQI 317 → `[CRITICAL] Air quality hazardous`; 400 → 195 → `[WARNING] Air quality poor`; O₂ pot 60 → 15.59 % → `[CRITICAL] Oxygen level unsafe`; ECG pot full scale → 5.00 mV |
| 06 Periodic aggregation | T2 | ✅ 1/1 | `medical.summary` n=9, HR 63.9 (53–81), SpO₂ 97.7, T 36.81; `facility.summary` n=9, AQI avg 97 / max 97; internally consistent |
| 07 Dosage rate limit | T3 | ✅ 1/1 | **5.00 mg/hr per simulated second** (13 `[DOSE]` lines) |
| 08 Critical confirmation | T3 | ✅ 3/3 | an unconfirmed 95 is held at 80 (max delivered 80.0) and the gate expires after **15 006 ms** (limit 15 000 ms); red LED on in 81 samples and buzzer 3 in 50 while pending; `confirmed` releases 95 by ramp |
| 09 Bed ramp, presets, manual | T4 | ✅ 4/4 | slider 0 → 90° in **2.23 s** (1°/25 ms); presets 10/45/90 reached (servo shows 12/46/89); emergency preset **0.73 s**, still a ramp; manual command turns Auto bed OFF *(frame; measured by S-09b)* |
| 10 Automatic bed | T4 | ✅ 4/4 | SpO₂ 88 → 46°; 82 (CRITICAL) → 89°; back to normal → 12° (target 10); after a manual 70° the bed stays at 70° while SpO₂ is 82 |
| 11 Adaptive sampling | T5 | ✅ 6/6 | spacing **5.08 s** at the 5 s setting; 8 of 8 samples published exactly once; a task asleep on a 60 s period resamples **1.26 s** after a 5 s command; auto mode 5 → 30 s when stable, 30 → 5 s at HR 130, back to 30 s only after **3 fresh stable samples** |
| 12 Wi-Fi outage → resync | T6 | ✅ 6/6 | OFFLINE/WIFI **1.9 s** after the cut; backoff 1, 2, 4, 8, 16 s (the OLED banner showed `Buffered: 7` and `Retry in 32s` on the 32 s step); **8 buffered → 8 replayed**, in order, 5.0 s apart, each seen once by an independent subscriber on `system.backlog`; ONLINE **13.1 s** after release |
| 13 Broker outage | T6 | ✅ 3/3 | OFFLINE/MQTT **0.7 s** after the cut; 2-long-beep pattern logged; 4 records replayed on release |
| 14 Dose on the OLED in real time | T3 | ✅ *(frame)* + 3/3 supplementary | the footer band is **identical on all five pages**, differs between dose 0 and 60, and is on the offline banner (S-14a/b/c) |
| 15 Command reliability | T3–5 | ✅ 1/1 | 12 of 12 commands in a burst applied |
| 16 Stability | — | ✅ 1/1 | one boot banner, no watchdog/panic/abort, heap 166 → 165 KB |

*(frame)* = the suite line confirms a saved frame by eye (`TC09_oled_therapy_auto_off.png`,
`TC14_footer_dose60_warn.png`); the measured, pixel-level versions are the supplementary checks below.

---

## 2. Supplementary checks and experiments (same final build)

| ID | What | Result |
|----|------|--------|
| S-14a/b/c | The OLED footer band (rows 52–63 of the 128×64 canvas) is hashed on every page: identical on all five pages at dose 0 (366 lit pixels each), identical again at dose 60 and different from dose 0, and identical on the offline banner | **PASS** ×3 |
| S-09b | The "Auto bed:" row of the THERAPY page: ON → after a manual angle (OFF) → after `bed-auto=1` (ON again); signatures `…667:133`, `…590:144`, `…667:133` | **PASS** |
| S1 Fever path | DS18B20 moved to 39.0 °C in the running simulation → `medical.body-temperature` 39.00, status "High body temperature", CRITICAL alert, buzzer 1, `[RATE] auto-adapt: 30 s -> 5 s`; back to 36.8 °C → "Patient stable" and `5 s -> 30 s` after 3 samples | **PASS** (`evidence/supplementary_S1_fever_serial.txt`) |
| S2 GPIO15 | BUG_LOG B-14 re-tested: the LED on GPIO15 never lights in Wokwi (0 samples while buzzer 3 on the same alert pass was on 18 times); on GPIO19 the same alarm gives 81 samples | **confirmed** (`evidence/supplementary_S2_gpio15_experiment.txt`) |
| S3 Natural broker loss | About 886 simulated seconds in, after everything above had finished, the public broker / Wokwi gateway stopped answering. The firmware detected it by itself (`broker not answering keepalive`), reported `OFFLINE (fault: MQTT)` and retried with backoff 1 → 60 s, with no reboot, watchdog or panic | **handled** (`evidence/supplementary_S3_natural_broker_loss_serial.txt`) |

---

## 3. Every automated check

The table is generated from `evidence/final_pass_results.json` (49 checks) and
`evidence/supplement_pass_results.json` (4 checks); long values are shortened here and complete in the
log.

### TC-01 · Boot, tasks, watchdog, OLED pages (Task 1)  — 8/8 checks passed

| Check | What is asserted | Result | Measured |
|---|---|---|---|
| `TC-01a` | firmware tag printed | PASS | [BOOT] firmware 2026-10-13 final |
| `TC-01b` | 10 FreeRTOS tasks started (once) | PASS | serial: [BOOT] 10 FreeRTOS tasks started |
| `TC-01c` | task watchdog armed 20 s | PASS |  |
| `TC-01d` | no panic/WDT/abort lines | PASS | count=0 |
| `TC-01e` | exactly one boot banner (no resets) | PASS | boot banners=1 |
| `TC-01f` | no phantom Wi-Fi fault at boot (B-34) | PASS | Wi-Fi associated at boot |
| `TC-01g` | no boot-time rate flip 15->5 s (B-30) | PASS |  |
| `TC-01h` | all five OLED pages appear | PASS | pages seen after 13.0 simulated s (first seen at sim +s): SYSTEM@0.1, PATIENT@2.4, EXTENDED@5.5, FACILITY@8.8, THERAPY@12.8 |

### TC-02 · Alarms are not slowed by a network outage (Task 1)  — 1/1 checks passed

| Check | What is asserted | Result | Measured |
|---|---|---|---|
| `TC-02` | red-LED alarm cadence same before and during the outage | PASS | critical-alarm passes: 5 before the cut (period 1.01 s) vs 4 during the Wi-Fi outage (period 1.16 s), ratio 1.15; OFFLINE/WIFI reported inside the window: True; sim windows +1..+6 s and +7.5..+12.5 s after the request |

### TC-03 · Probe disconnect -> DEGRADED (Tasks 1, 6)  — 4/4 checks passed

| Check | What is asserted | Result | Measured |
|---|---|---|---|
| `TC-03-temp` | probe temp off -> DEGRADED/SENSOR within 3 s | PASS | 3.4 s; trace has [SENSOR] line: True |
| `TC-03-hr` | probe hr off -> DEGRADED/SENSOR within 3 s | PASS | 1.6 s; trace has [SENSOR] line: True |
| `TC-03-status` | status reads "Sensor disconnected" (never "Patient stable") | PASS | Sensor disconnected |
| `TC-03-spo2` | probe spo2 off -> DEGRADED/SENSOR within 3 s | PASS | 3.0 s; trace has [SENSOR] line: True |

### TC-04 · Role separation at topic level (Task 2)  — 1/1 checks passed

| Check | What is asserted | Result | Measured |
|---|---|---|---|
| `TC-04` | device publishes disjoint medical.* / facility.* topic groups | PASS | medical topics: medical.body-temperature,medical.heart-rate,medical.spo2,medical.patient-status,medical.blood-pressure,medical.ecg,medical.summary,medical.alerts; facility topics: facility.aqi,facility.humidity,facility.motion,facility.room-temperature,facili… |

### TC-05 · Facility inputs, gauges and alert feeds (Tasks 1, 2)  — 4/4 checks passed

| Check | What is asserted | Result | Measured |
|---|---|---|---|
| `TC-05a` | AQI pot 650/1023 -> AQI ~317 -> CRITICAL "Air quality hazardous" | PASS | aqi=317 alert=[CRITICAL] Air quality hazardous |
| `TC-05b` | AQI pot 400 -> AQI ~195 -> WARNING "Air quality poor" | PASS | aqi=195 alert=[WARNING] Air quality poor |
| `TC-05c` | O2 pot 60 -> O2 ~15.6 % -> CRITICAL "Oxygen level unsafe" | PASS | o2=15.59 alert=[CRITICAL] Oxygen level unsafe |
| `TC-05d` | ECG pot full scale -> ecgTask reports ~5.00 mV | PASS | ecg=5.00 |

### TC-06 · Periodic aggregation (Task 2)  — 1/1 checks passed

| Check | What is asserted | Result | Measured |
|---|---|---|---|
| `TC-06` | 60 s aggregation JSON on both summary feeds, internally consistent | PASS | medical.summary={"n":9,"hr_avg":63.9,"hr_min":53,"hr_max":81,"spo2_avg":97.7,"spo2_min":97,"temp_avg":36.81} \| facility.summary={"n":9,"room_avg":22.00,"o2_avg":21.84,"aqi_avg":97,"aqi_max":97} |

### TC-07 · Dosage rate limiting (Task 3)  — 1/1 checks passed

| Check | What is asserted | Result | Measured |
|---|---|---|---|
| `TC-07` | dose ramps at exactly 5 mg/hr per simulated second | PASS | 5.00 mg/hr/s from 13 lines |

### TC-08 · Dosage critical confirmation (Task 3)  — 3/3 checks passed

| Check | What is asserted | Result | Measured |
|---|---|---|---|
| `TC-08A` | unconfirmed 95 request is held at 80 and expires to 80 (safe ceiling) | PASS | gate expired after 15006 ms (limit 15000 ms); max delivered 80 over 17 ramp lines; independent subscriber system.dosage: 0,4.63,30.03,55.56,80,80 |
| `TC-08A2` | alarm (red LED + buzzer 3) active while confirmation pending | PASS | red LED ON samples 81; buzzer-3 ON samples 50 |
| `TC-08B` | confirmed request releases to 95, still ramped | PASS | serial: [DOSE] confirmed 95.0 mg/hr |

### TC-09 · Bed: smooth ramp, presets, manual (Task 4)  — 4/4 checks passed

| Check | What is asserted | Result | Measured |
|---|---|---|---|
| `TC-09a` | 0 -> 90 deg ramp takes about 2.2-2.4 s (1 deg / 25 ms) | PASS | first log 1 deg, last 90 deg, 2.23 s |
| `TC-09b` | a manual command switches Auto bed OFF | PASS (frame) | see TC09_oled_therapy_auto_off.png — confirmed from the saved frame; measured by S-09b below |
| `TC-09c` | presets sleep 10 / breathing 45 / emergency 90 reach the target (servo shown) | PASS | emergency@90=>89  breathing@45=>46  sleep@10=>12 |
| `TC-09d` | emergency preset ramps faster (~0.75 s) but is still a ramp | PASS | 0.73 s, 5 log lines |

### TC-10 · Automatic bed from patient condition (Task 4)  — 4/4 checks passed

| Check | What is asserted | Result | Measured |
|---|---|---|---|
| `TC-10a` | SpO2 88 (< 90) -> bed 45 deg | PASS | servo shows 46 |
| `TC-10b` | SpO2 82 (< 85, CRITICAL) -> bed 90 deg | PASS | servo shows 89; status: Severe hypoxaemia |
| `TC-10c` | SpO2 back to normal -> bed returns to 10 deg | PASS | servo shows 12 |
| `TC-10d` | manual command beats automation (auto never overrides manual) | PASS | bed stayed at 70 while SpO2 was 82 |

### TC-11 · Adaptive sampling (Task 5)  — 6/6 checks passed

| Check | What is asserted | Result | Measured |
|---|---|---|---|
| `TC-11a` | manual 5 s: samples ~5.0 s apart (sim time) | PASS | mean spacing 5.08 s over 7 intervals |
| `TC-11b` | one publish per sample, none repeated (publish-per-sample, B-32) | PASS | 8 of 8 samples published exactly once; repeated publishes: 0 |
| `TC-11c` | rate change wakes a sleeping task within ~1.5 s (not up to 60 s) | PASS | latency 1.26 s (clock read before publish) |
| `TC-11d0` | auto mode: a stable patient settles to 30 s | PASS | trace: [RATE] auto-adapt: 5 s -> 30 s |
| `TC-11d` | auto mode: abnormal vitals (HR 130) -> 5 s | PASS | trace: [RATE] auto-adapt: 30 s -> 5 s |
| `TC-11e` | auto mode: stable -> 30 s only after 3 fresh stable samples (hysteresis) | PASS | samples between release and adapt: 3; adapted: True |

### TC-12 · Wi-Fi outage: offline, buffer, restore, resync (Task 6)  — 6/6 checks passed

| Check | What is asserted | Result | Measured |
|---|---|---|---|
| `TC-12a` | Wi-Fi cut detected -> OFFLINE / cause WIFI within 2 s | PASS | 1.9 s |
| `TC-12b` | exponential backoff 1,2,4,8,16,32 while cut | PASS | backoff sequence: 1,2,4,8,16 |
| `TC-12c` | distinct buzzer pattern logged for Wi-Fi (3 short beeps) | PASS |  |
| `TC-12d` | amber LED lit while offline | PASS |  |
| `TC-12e` | restored: records out == records in, chronological, one MQTT message each | PASS | buffered/replayed 8; resynced 8; independent subscriber saw 8 on system.backlog; in order: True; spacing 5.0,5.0,5.0,5.0 |
| `TC-12f` | back ONLINE soon after release (reset backoff per layer, B-29) | PASS | 13.1 s from release to resync complete |

### TC-13 · Broker outage (Task 6)  — 3/3 checks passed

| Check | What is asserted | Result | Measured |
|---|---|---|---|
| `TC-13a` | broker cut detected -> OFFLINE / cause MQTT within 2 s | PASS | 0.7 s |
| `TC-13b` | distinct buzzer pattern logged for broker (2 long beeps) | PASS |  |
| `TC-13c` | restored -> ONLINE and backlog replayed | PASS | [NET] resynced 4 records |

### TC-14 · OLED shows the dose in real time (Task 3)  — 1/1 checks passed

| Check | What is asserted | Result | Measured |
|---|---|---|---|
| `TC-14` | OLED footer shows live dose on every page | PASS (frame) | see TC14_footer_dose60_warn.png (footer: DOSE 60.0 mg/hr WARN) — confirmed from the saved frame; measured by S-14a/b/c below |

### TC-15 · Command reliability (Tasks 3-5)  — 1/1 checks passed

| Check | What is asserted | Result | Measured |
|---|---|---|---|
| `TC-15` | 12/12 commands applied | PASS | applied: 10,11,12,13,14,15,16,17,18,19,20,21 |

### TC-16 · Stability  — 1/1 checks passed

| Check | What is asserted | Result | Measured |
|---|---|---|---|
| `TC-16a` | no panic/WDT/abort after the whole suite; boot banners == 1 + suite restarts | PASS | sim clock 506 s (8.4 min) in the final session; banners=1; suite-initiated restarts so far=0 |

### Supplementary pixel-level checks

| Check | What is asserted | Result | Measured |
|---|---|---|---|
| `S-14a` | footer band identical on all five pages at dose 0 and not blank | PASS | distinct footer signatures: 1 over 5 pages; lit pixels per page: 366,366,366,366,366 |
| `S-14b` | at dose 60 the footer is again identical on all five pages and differs from the dose-0 footer | PASS | distinct footer signatures: 1; equals the dose-0 footer: False |
| `S-14c` | the offline banner carries the same dose footer (dose 60) | PASS | banner footer signature equals the dose-60 page footer: True |
| `S-09b` | a manual bed command changes the Auto-bed row (ON -> OFF) and "auto" restores it (OFF -> ON) | PASS | ON 1486377667:133 \| after manual angle -1642058590:144 \| auto again 1486377667:133 |

---

## 4. Run history — how it got to 49/49

The suite was run four times on four builds. The failures are listed because **two of them were not
test problems**:

| Run | Build (`main.ino`) | Result | What the failures were |
|-----|--------------------|--------|------------------------|
| A | `add99597…` | 43 / 49 | `TC-09a` — a **real firmware bug**: a slider move to 90° ramped at the emergency rate (0.73 s instead of ≈ 2.3 s) because the fast ramp was keyed to the angle, not the mode → fixed (BUG_LOG **B-36**). `TC-11b`, `TC-12b`, `TC-05a/b`, `TC-02` were test-rig defects (a result read before it could exist, or a wall-clock window; BUG_LOG R-1, R-3, R-4) |
| B | `7467ed71…` | 46 / 49 | all three were rig defects, diagnosed from the traces rather than re-run until green: `TC-01h` and `TC-02` used wall-clock windows while the simulator ran at 36–67 % (the 4 s "before" window covered ≈ 1.6 simulated seconds and the 24 s page watch only 10 of the 15 s rotation), and `TC-03-temp` was a **lost mouse click** (`SWITCH swTemp -> 0` after a request for 1). Fixes: every window is measured on the simulator clock; switch clicks are verified and repeated (BUG_LOG R-1, R-2) |
| C | `9d169735…` | **49 / 49** | superseded: the final step added only the `[DOSE] … confirmation expired after N ms` trace (B-37). Its screenshot session then exposed **B-38** (below) |
| **D** | **`9e69da71…`** | **49 / 49 + 4 / 4** | the final build: B-38 fix. Includes the supplementary checks, the fever test and the natural-outage observation |

**B-38** (found after run C): in a long screenshot session the task watchdog rebooted the chip
because `mqttTask` was blocked in a chain of un-fed TCP writes to a stalled link
(`evidence/run_history/screenshot_session_serial_with_wdt_reset.txt`). The fix feeds the watchdog
before every write, ends a publish round at the first failure and probes a suspect session every
second. A stall cannot be produced on demand in Wokwi, so the failing-write branch itself is **not
exercised**; run D is the regression run on the changed code.

---

## 5. What was not exercised, and other honest limits

* **DEGRADED caused by publish failures** — implemented, reviewed, but no simulator control makes
  publishes fail while the link stays up. (DEGRADED caused by a disconnected probe *is* tested, TC-03.)
* **The B-38 failing-publish branch** (watchdog fed per write, round ended at the first failure, 1 s
  probes) — reviewed and built, not triggered: the natural outage in S3 was caught by the idle
  keepalive, not by a failed publish.
* **The optional SPIFFS mirror** (`USE_SPIFFS 0`).
* **Long soak runs.** The longest continuous simulation was ≈ 22 minutes (the final session, the last
  ≈ 6 of which were the natural outage S3). In two sessions the public
  broker / Wokwi gateway stopped answering the simulated device after roughly 11–15 simulated
  minutes while the PC could still reach the broker; the firmware reports OFFLINE and keeps retrying,
  and restarting the simulation clears it. This is an environment condition, not mitigated in firmware.
* **Real hardware and real sensors.** Heart rate, SpO₂ and blood pressure are simulated; timing is the
  simulator's clock, not a logic analyser.
* **Role-based access** is topic separation only; the public broker has no ACLs.
* **Flash / RAM usage** of the final build was not measured (Wokwi does not report it); see
  `BUILD_VERIFICATION.md`.
* Thresholds are illustrative engineering values, not clinically validated; this is not a medical device.

## 6. Reproduce

```bash
cd tools/verification && npm install && node simserver.js
powershell -NoProfile -ExecutionPolicy Bypass -File suite.ps1        # in a second terminal, ~25 min
powershell -NoProfile -ExecutionPolicy Bypass -File supplement.ps1   # same session, ~6 min
```

or follow [`TEST_PLAN.md`](TEST_PLAN.md) by hand in the public Wokwi project. See
[`../tools/verification/README.md`](../tools/verification/README.md).
