# Bug Log

Every defect found in this project, how it was found, what caused it, how it was fixed
and how the fix was checked. IDs are stable — `main.ino` and the README cite **B-14** and
**B-15** by number.

**Sources.** B-01 – B-13 are reconstructed from the git history (commit hashes given, run
`git show <hash>` to read the original message); B-14 – B-22 come from the 3 Oct 2026 QA
pass over the Wokwi circuit and firmware; B-23 – B-38 were found on **13 Oct 2026** by
running the project end to end in the Wokwi simulator with a serial/MQTT/OLED measurement
rig, and were measured before and after the fix. Nothing here is invented to look thorough:
where a fix is only reasoned from the code and was not seen failing, the entry says so.
Defects in the measurement rig itself (not the firmware) are listed separately at the end.

Severity: **High** = wrong or unsafe behaviour of a required feature · **Medium** = feature
works but misleads or is hard to verify · **Low** = cosmetic / documentation.

---

## Summary

| ID | Found | Sev | Area | Defect (one line) | Fixed in |
|----|-------|-----|------|-------------------|----------|
| B-01 | 2026-08-06 | High | T1 alarms | Heart-rate alarm threshold was unreachable (`> 300` on a `random(45,300)` source) | `e80eeca` |
| B-02 | 2026-08-06 | High | T1 sensing | Body temperature read uninitialised when the sensor was disconnected | `e80eeca` |
| B-03 | 2026-08-06 | High | T1 concurrency | Blocking MQTT I/O shared a task with alarm generation — alarms stop when the broker is slow | `e80eeca` |
| B-04 | 2026-08-06 | Medium | Build | Arduino auto-prototype hoisting broke the build (`Aggregate` declared too late) | `1e8262a` |
| B-05 | 2026-08-06 | Low | T1 watchdog | `loop()` logged `task_wdt: delete_entry(): task not found` on some cores | `2b0b07a` |
| B-06 | 2026-08-06 | High | T6 / T1 | Supervisor kept its watchdog subscription across blocking `mqtt.connect()` — could panic-reset on a slow network | `2b0b07a` |
| B-07 | 2026-08-06 | High | T5 | One shared wake queue woke only one of the sampling tasks (token consumed by the first waiter) | `e80eeca` (final design) |
| B-08 | 2026-08-22 | High | T2 dashboard | Node-RED refused to start: `ui_base` node had `id == type` ("Circular config node dependency") | `eb79ef7` |
| B-09 | 2026-08-22 | Medium | T2 dashboard | Heart-rate gauge colour bands inverted (60–120 bpm showed amber) | `ac18c39` |
| B-10 | 2026-08-23 | Low | Docs | README status text stale | `1b38c2d` |
| B-11 | 2026-08-23 | High | T2 / docs | "Role-based access" was claimed as enforced; only topic separation exists | `e801efc` |
| B-12 | 2026-08-23 | Medium | Docs | Report said all 12 test cases passed; the results file said 4 were not confirmed | `e801efc` |
| B-13 | 2026-08-23 | Low | Docs | README listed a non-existent `submission_pdfs/`; demo script named the wrong dashboard stack | `e801efc`, `f0234f4` |
| B-14 | 2026-10-03 | High | T3 circuit | Dosage LED on GPIO15 never lit in Wokwi → moved to GPIO19 | working tree → this release |
| B-15 | 2026-10-03 | High | T3/T6 circuit | Buzzers wired to `D16`/`D17`, which do not exist on Wokwi's DevKit-V1 (`RX2`/`TX2`) | this release |
| B-16 | 2026-10-03 | High | T1/T2/T6 circuit | Potentiometers and probe switches grounded to `GND.3`, which does not exist (`GND.1`, `GND.2` only) | this release |
| B-17 | 2026-10-03 | Medium | T5 | "3 stable cycles" hysteresis counted 1 Hz loop ticks, not samples | this release |
| B-18 | 2026-10-03 | High | T3 | Critical-dose alarm stayed silent while a >80 request awaited confirmation | this release |
| B-19 | 2026-10-03 | Medium | T6 | Result of `mqtt.ping()` ignored — a silently dead connection went unnoticed | this release |
| B-20 | 2026-10-03 | Medium | T6 | A stalled backlog replay was retried only at the next outage | this release |
| B-21 | 2026-10-03 | Medium | T2 dashboard | Room-temperature gauge one-sided (green to 26 °C, red above 30) vs the firmware's 18–26 °C band | this release |
| B-22 | 2026-10-03 | Low | Tooling | `gen_flows.py` out of sync with the committed flow (prefix, `ui_base` id) | this release |
| B-23 | 2026-10-13 | High | Delivery | The public Wokwi project still ran the 5 Sep sketch **and the broken wiring** (B-14–B-16) | this release |
| B-24 | 2026-10-13 | High | Delivery | GitHub `main` lacked every 3 Oct fix; PR #1 (report corrections) unmerged | this release |
| B-25 | 2026-10-13 | Medium | T6 | OLED "Retry in Ns" showed the backoff interval; docs claimed a live countdown | this release |
| B-26 | 2026-10-13 | Medium | T6 | Publish failures were still counted while OFFLINE (reasoned from code — not seen failing) | this release |
| B-27 | 2026-10-13 | High | T6 | No repeatable way to take the simulated device offline, so the buffer → resync cycle could not be demonstrated | this release |
| B-28 | 2026-10-13 | High | T6 / T1 | Probe-disconnect noticed only at the next sample: **32 s** at the 30 s stable rate | this release |
| B-29 | 2026-10-13 | Medium | T6 | After Wi-Fi returned, the broker waited out the 60 s backoff: back ONLINE only after **≈ 65 s** | this release |
| B-30 | 2026-10-13 | High | T1/T2/T5/T6 | Boot-time evaluation of zero-filled placeholder data: phantom CRITICAL "Oxygen level unsafe", a 15 s→5 s rate flip and an all-zero record in the backlog | this release |
| B-31 | 2026-10-13 | Medium | T4 | Servo pulse range ≠ Wokwi's SG90 model: commanded 10/30/45/90° showed 4/25/40/87° | this release |
| B-32 | 2026-10-13 | High | T5 / T2 | MQTT loop pinged on every pass and published on a separate timer: publish cadence 2–3× the sampling period | this release |
| B-33 | 2026-10-13 | Medium | T2/T5 | The synthetic patient's heart rate drifted into 150–175 bpm "Severe tachycardia" and stayed there | this release |
| B-34 | 2026-10-13 | Low | T6 | A phantom "Wi-Fi fault" buzzer/log entry at boot even though Wi-Fi had associated | this release |
| B-35 | 2026-10-13 | Medium | Docs | Architecture/workflow diagrams still showed 9 tasks, Adafruit IO, GPIO15 and "ACLs enforce separation" | this release |
| B-36 | 2026-10-13 | Medium | T4 | The fast "emergency" servo ramp was selected by the *angle* 90 instead of the *mode*: a slider move to 90° took 0.73 s instead of ≈ 2.3 s | this release |
| B-37 | 2026-10-13 | Low | T3 | The 15 s confirmation expiry left no trace in the log, so the gate could only be inferred, not measured | this release |
| B-38 | 2026-10-13 | High | T6 / T1 | A stalled TCP link blocked `mqttTask` in a chain of ~15 un-fed `publish()` writes → the 20 s task watchdog **rebooted the chip** (RAM buffer and alarms lost) | this release |

---

## Requirement-conformance changes (not defects)

Re-reading the six task texts against the code on 13 Oct produced four changes where the
old code worked but did not match the wording as literally as it could:

| ID | Task text | Before | After |
|----|-----------|--------|-------|
| C-1 | T1 — parameters "each managed by **separate** FreeRTOS tasks" | BP and ECG shared one `advancedVitalsTask` | `bloodPressureTask` and `ecgTask`, each with its own queue (10 tasks) |
| C-2 | T3 — dosage "displayed in **real-time** on the OLED" | on one of five pages (3 s in every 15 s), redrawn every 3 s | persistent footer on every page and the offline banner, whole screen redrawn at 1 Hz |
| C-3 | T5 — "non-blocking delays (**vTaskDelay**) and dynamic updates through queues" | waited with a timed `xQueueReceive` | 100 ms `vTaskDelay()` slices polling the task's own wake queue |
| C-4 | T6 — "If Wi-Fi or MQTT fails…" | could only be demonstrated by restarting with a wrong SSID (which loses the RAM buffer) | `SW_WIFI_CUT` / `SW_BROKER_CUT` switches act on the real link (see B-27) |

---

## Detail — defects found by measurement on 13 Oct 2026

Each of these was observed in the running simulator, fixed, and re-measured on the same rig.
Raw logs are in `docs/evidence/`.

### B-28 — probe disconnect noticed 32 s late
*Symptom.* Flipping the heart-rate probe switch at simulated 01:15.9 raised DEGRADED /
SENSOR at 01:48.1. *Cause.* `vitalsSensorTask` reads its four probe switches once per
sampling period, which is 30 s once the patient is stable — so "continuously monitors …
system health" was true only once per period. *Fix.* `processingTask` watches the four
switches at 1 Hz and, on any change, wakes the vitals and BP tasks through their wake
queues. *After.* Same action: DEGRADED reported 2.2 s later (01:03.2 → 01:05.4) with
`[SENSOR] probe change -> temp:ok hr:OFF … -- resampling now` in the log.

### B-29 — 65 s to reconnect the broker after Wi-Fi returned
*Symptom.* Wi-Fi switch released at 02:52.7; the device was ONLINE again at ≈ 03:57.
*Cause.* Recovery is two stages (Wi-Fi, then broker). The Wi-Fi stage's failures had grown
the backoff to 60 s, and the broker attempt inherited that delay. *Fix.* When a stage
recovers the failure streak is over: backoff returns to 1 s and the next stage is tried on
the next tick. *After.* Released at 01:39.96, back ONLINE and 5/5 buffered records resynced
at 01:50.56 (10.6 s), with `[NET] Wi-Fi re-associated -- reconnecting to the broker next`.

### B-30 — alarms and a bogus record from unmeasured data at boot
*Symptom.* In the first boot trace: `[RATE] auto-adapt: 15 s -> 5 s` before the first
sample, and the first record replayed to `system.backlog` was
`{"t":5159,"bt":0.00,"hr":0,"spo2":0,…,"risk":2}` — all zeros, risk CRITICAL.
*Cause.* The data queues are seeded with zeros in `setup()`; the risk engine scored them
(oxygen 0 % ⇒ "Oxygen level unsafe"). *Fix.* The engine waits until both the vitals and
environment tasks have produced a real sample (`seq > 0`). *After.* The boot trace has no
rate flip and no phantom alarm; the first record is a real sample.

### B-31 — servo angle off by up to 6°
*Measured* (Wokwi `servo.angle` after settling): commanded 10 / 30 / 45 / 90° → shown
4 / 25 / 40 / 87°. *Cause.* The code attached the servo with the nominal 500–2400 µs
range; Wokwi's SG90 maps 0°→≈565 µs and 180°→≈2396 µs. *Fix.* `SERVO_MIN_US 565`,
`SERVO_MAX_US 2396`. *After* (0…90° in 10° steps): commanded 0/5/10/15/20/30/40/50/60/70/80/90 →
shown 0/6/12/15/21/31/40/50/61/70/80/89 — within +2 / −1°, which is the resolution of
Wokwi's integer read-out.

### B-32 — publish cadence 2–3× the sampling period
*Symptom.* With a 5 s period, `medical.heart-rate` arrived every 10–18 s of wall time at
75–88 % simulator speed. *Cause.* `mqttTask` called `mqtt.ping()` on every 200 ms pass (a
blocking round trip holding the client lock, up to 500 ms each) and published from a
free-running timer that was never in phase with the sampling task. *Fix.* Ping only when
idle for 10 s; publish when a *new* vitals sample exists. *After* (serial trace, simulated
time): samples 5083 ms apart, each published 0.1–0.4 s later; 15 s period → samples 15 084 ms
apart.

### B-33 — "stable" patient not stable
*Symptom.* After a few minutes the simulated heart rate sat at 146–175 bpm, status
"Severe tachycardia", continuously. *Cause.* The generator was an unbounded random walk
clamped at 175. *Fix.* Mean reversion to 78 bpm (SpO₂ to 97 %) with rare ±episodes, plus
`control.sim-hr` / `control.sim-spo2` to pin a value on demand. *After.* HR 73–99 over
runs of several minutes, with occasional episodes that decay (e.g. 121 → 113 → 107).

### B-27 — Task 6 could not be demonstrated on demand
The previous test notes record the offline evidence as a Wi-Fi association that "genuinely
failed" mid-session — luck, not a method; editing `WIFI_SSID` and restarting loses the RAM
buffer, so the buffered → replayed half of the cycle could not be shown that way. Two slide
switches now cut the real Wi-Fi link or close the real MQTT session. Measured with
`SW_WIFI_CUT`: OFFLINE/WIFI within 1 s, backoff 1·2·4·8·16 s logged, 21 samples buffered at
5 s spacing, ONLINE and `[NET] resynced 21 records`; an independent MQTT subscriber saw
21 records on `system.backlog`, chronological, 5 s apart. See `TEST_RESULTS.md` TC-12/13.

### B-36 — "emergency" speed chosen by the number 90, not by the mode
*Symptom.* In the first full automated run, `TC-09a` (bed slider 0 → 90°) took **0.73 s**
with the first `[BED]` line already at 3° — the emergency ramp (3°/25 ms) — instead of the
≈ 2.3 s that 1°/25 ms gives. *Cause.* `actuatorTask` used the fast step whenever the
*target angle* was 90, so a slider move or an automatic move to 90° was treated as an
emergency, although Task 4 (and `ASSUMPTIONS.md` A4b) define emergency as a *mode*.
*Fix.* An explicit `bedFast` flag in `SystemState`: set only by the emergency preset and by
the critical-SpO₂ automatic branch, cleared by every manual angle command. *After.* Slider
0 → 90°: **2.23 s**, 1°/25 ms (`TC-09a`); the emergency preset still ramps in 0.73 s and is
still a ramp (`TC-09d`).

### B-37 — the confirmation gate's expiry was invisible
*Gap.* The 15 s confirmation window in `actuatorTask` expired silently, so `TC-08A` could only
infer the expiry from the dose reaching 80 — it could not measure the window. *Fix.* After the
state lock is released the task prints `[DOSE] t=… ms confirmation expired after N ms --
holding at 80 mg/hr` (trace only, no control-flow change). *After.* Measured **15 011 ms**
against the 15 000 ms limit, and an independent MQTT subscriber saw `system.dosage` read
2.26 → 26.65 → 56.69 → 80 → 80 → 80 (`final_pass.log`, `TC-08A`).

### B-38 — a network stall rebooted the chip
*Symptom.* About 12 simulated minutes into a screenshot session (not during the 49-check run)
the serial monitor printed `E (702687) task_wdt: Task watchdog got triggered … - Mqtt (CPU 0)
… Aborting`, then `rst:0xc (SW_CPU_RESET)` and a fresh boot banner (`docs/evidence/run_history/
screenshot_session_serial_with_wdt_reset.txt`). The 30 s before the crash show the cause: at the
5 s rate each sample was published later and later, then the `[MQTT] published` lines stopped
while sampling carried on. After the reboot the simulated Wi-Fi did not re-associate for
minutes (an environment condition — the public broker / Wokwi gateway).
*Cause.* `publishAll()` sends about fifteen messages in a row while `mqttTask` holds the client
lock, and the watchdog was fed only once per pass of the task. When the TCP link stops
draining (the broker or the gateway stops reading) one `WiFiClient::write()` can block for
~10 s, so two or three of them exceed the 20 s watchdog. The reset is the watchdog doing its
job — but it also silences every alarm during the reboot and wipes the RAM offline buffer, the
very thing Task 6 exists to protect. The supervisor could not help: it needs the same lock and
kept its last known "link up".
*Fix.* (1) The watchdog is fed before every write (`MQTT_PUBLISH`), so one stalled write can
never use the whole timeout. (2) A publish round ends at the first failed write instead of
hammering a dead session. (3) After a failed round the session is probed every second
(`MQTT_SUSPECT_PING_MS`); `PING_MISS_LIMIT` unanswered probes drop it, the supervisor reports
OFFLINE / MQTT and buffering starts within seconds. (4) A failed round is logged
(`[MQTT] publish of sample #N FAILED … probing the session`).
*Honest limits.* A real stall cannot be produced on demand in Wokwi, so the failing branch has
**not** been exercised — it was reviewed, built, and the full 49-check suite was re-run on the
new build to show no regression. A sample whose publish fails in the few seconds before the
dead session is dropped is not buffered (it was already sampled and is not re-sent).

### B-14 — verified by experiment (13 Oct 2026)
The 3 Oct entry rested on a pin sweep. It was re-tested by building the **project (build
`9d169735…`, final at that moment) with only the LED moved back to GPIO15** (`#define LED_DOSAGE
15`, wire `rDose:2 → esp:D15`, cathode
on the existing `GND.1`, so the GND.3 defect B-16 cannot be the cause). The board's own pin
list confirms `D15` is a real pin. Triggering the critical-dose alarm for 9.7 simulated
seconds gave **0** red-LED samples while buzzer 3 — driven by the same pass of `alertTask` —
was on in 18 samples; on GPIO19 the same stimulus gave 81 LED samples (`TC-08A2`). So the
statement in `main.ino` is accurate: in Wokwi's ESP32 model an output on GPIO15 never reaches
the LED. (On real hardware GPIO15 would be a usable output; it is avoided anyway because it is
a boot-strapping pin.) Raw record: `docs/evidence/supplementary_S2_gpio15_experiment.txt`.

---

## Defects in the verification rig (not in the firmware)

Found while making the automated checks trustworthy. None of them changed a firmware result,
but each one produced a false FAIL (or could have produced a false PASS), so they are listed.

| ID | Where | Defect | Fix |
|----|-------|--------|-----|
| R-1 | `TC-01h`, `TC-02` | Windows were wall-clock sleeps. Wokwi ran at 36–67 % of real time (and dipped lower), so "4 s" covered 1–2 simulated seconds and a 24 s watch covered only 10 s of the 15 s page rotation → false FAILs | every window is now measured on the simulator's own `mm:ss.mmm` clock (`Sleep-Sim`, `Sleep-SimUntil`) |
| R-2 | `TC-03-temp` | The switch click was not verified. One click was lost (`SWITCH swTemp -> 0` after a request for 1) and the 60 s wait then looked like "the firmware ignored the switch" | the controller re-reads the part and re-clicks until it reaches the requested state, and reports `clicks`/`ok` |
| R-3 | `TC-02` | Counted raw LED transitions. The 30 ms sampler cannot resolve every 60 ms toggle at low simulator speed, so the count was meaningless | counts alarm-pass *starts* (rising edge ≥ 0.5 s after the last) in two simulated-time windows |
| R-4 | first run | `TC-11b`, `TC-12b`, `TC-05a/b` read a result before it could exist (publish/sample pairing, a backoff step that had not happened yet, a facility feed that updates every environment period) | pairing by sample number, wait for the 16 s backoff, poll the feed |
| R-5 | `TC-08` | The "footer shows CONF" frame was grabbed before the 1 Hz OLED redraw and read `DOSE 0.0 NORM` | wait 2 simulated seconds; the mistimed frame is not in the evidence set (`TC08_pending_therapy_confirm.png` shows `>CONFIRM 95` and footer `CONF`) |

---

## Open items and honest limits

* **DEGRADED (publish-failure) path.** The `fails >= DEGRADED_FAIL_COUNT` branch exists and
  is reviewed, but no simulator control makes publishes fail while the link stays up, so it
  has **not** been exercised. DEGRADED via a disconnected sensor *is* exercised (TC-03).
* **Unplanned network drops (environment).** In earlier exploratory runs on the slow test
  machine the public broker / Wokwi gateway closed the MQTT session roughly once every 3–5
  simulated minutes; the device detected each one, buffered, reconnected and resynced on its
  own — unintended, but a real-world demonstration of Task 6. One such stall escalated into a
  watchdog reset (B-38, fixed). Two sessions saw the simulated device unable to reach
  `broker.hivemq.com` for minutes while a PC client could (once 7+ minutes; once for the 4+
  minutes after the B-38 reset); a simulation restart clears it. That is a Wokwi-gateway
  condition and is **not** mitigated in firmware — the suite waits for ONLINE before each
  section and would restart the simulation (and say so in the log) if the device stayed away
  for 150 s. See `TEST_RESULTS.md` for how many such events the final run saw.
* **Role-based access** is topic separation only (see `TASK_MAPPING.md` Task 2).
* **Clinical limits** are illustrative engineering values, not a validated medical device.
