# Test Plan

Sixteen test cases covering all six tasks. Each can be followed by hand in the public Wokwi
project with no extra hardware; the same cases were also run automatically and measured
(see [`TEST_RESULTS.md`](TEST_RESULTS.md) and [`../tools/verification/README.md`](../tools/verification/README.md)).

**Conventions.** Commands are MQTT messages to `rprms-hl-8842/feeds/<feed>` (use the Node-RED
sliders or any MQTT client). The six slide switches sit in the top row of the circuit,
**left → right: Temp · HR · SpO₂ · PIR · Wi-Fi cut · Broker cut**; sliding a switch to the
right applies the fault. Durations are read from the serial `t=… ms` stamps (simulated time).

---

## TC-01 · Boot, tasks, watchdog, OLED pages  *(Task 1)*

**Steps** Press Play; watch the serial monitor and the OLED for 30 s.
**Expected**
- `=== Remote Patient Risk Monitoring System ===`, `[BOOT] firmware …`, `[BOOT] task watchdog armed: 20 s timeout`, `[BOOT] 10 FreeRTOS tasks started` (once).
- One boot banner only (no resets); no `task_wdt`, `Guru Meditation`, `abort()`, `Backtrace`.
- OLED cycles five pages 3 s each — PATIENT VITALS → EXTENDED VITALS → FACILITY / ROOM → THERAPY CONTROL → SYSTEM STATUS — each ending with the `DOSE … mg/hr …` footer.
- No phantom Wi-Fi fault at boot and no `[RATE] auto-adapt: 15 s -> 5 s` before the first sample.

**Pass** All five pages appear; exactly one boot banner.

## TC-02 · Alarms are not blocked by a network outage  *(Task 1)*

**Steps** `control.dosage` = 95 (starts the critical alarm). While the red LED is blinking,
slide **Wi-Fi cut** right.
**Expected** The LED blink cadence and the dose ramp are unchanged while the supervisor is
retrying — alarms live on core 1 at priority 6; the network is on core 0.
**Pass** The period of the critical-alarm pass (5 red-LED blinks + 2 buzzer beeps, ≈ 1 s) during
the outage is within ±30 % of the period before it. (Measured by counting pass *starts* in two
simulated-time windows; the Wi-Fi beep pattern shares the alert task and lengthens one pass in
five by ≈ 0.5 s, so a ratio of ≈ 1.1 is expected.)

## TC-03 · Probe disconnect → DEGRADED  *(Tasks 1, 6)*

**Steps** Slide the **HR** (then Temp, then SpO₂) switch right; later back.
**Expected** `[SENSOR] probe change -> … hr:OFF …`, `[NET] state ONLINE -> DEGRADED (fault: SENSOR)`
**within about 3 s even at the 30 s stable rate**, amber LED, a single chirp every ~5 s,
OLED `HR : -- bpm`; patient status never reads "Patient stable" while a probe is off.
Releasing the switch returns the system to ONLINE.
**Pass** DEGRADED reported ≤ 4 s after the switch moves.

## TC-04 · Role separation (topic level)  *(Task 2)*

**Steps** Run 2 minutes; compare the topics each dashboard tab uses.
**Expected** The device publishes disjoint `medical.*` and `facility.*` groups; the Facility
tab contains no `medical.*` widget.
**Pass** Both groups populated, disjoint. *(This shows topic separation; broker-enforced
access control is not part of the project — see `TASK_MAPPING.md` Task 2.)*

## TC-05 · Facility inputs, gauges and alert feeds  *(Tasks 1, 2)*

**Steps** Drag the AQI pot (bottom) to ~650/1023, then ~400; the O₂ pot (middle) to ~60; the
ECG pot (top) to full scale.
**Expected** AQI > 300 → `[CRITICAL] Air quality hazardous` on `facility.alerts`; AQI
150–300 → `[WARNING] Air quality poor`; O₂ < 19.5 % → `[CRITICAL] Oxygen level unsafe`;
ECG ≈ 5.00 mV on `medical.ecg` (read by `ecgTask`).
**Pass** Each alert text and value appears within one environment period (≤ ~15 s).

## TC-06 · Periodic aggregation  *(Task 2)*

**Steps** Let the system run 90 s.
**Expected** One JSON record per minute per role, e.g.
`{"n":12,"hr_avg":80.5,"hr_min":74,"hr_max":91,"spo2_avg":96.8,"spo2_min":95,"temp_avg":36.81}`.
**Pass** `n` is the number of samples in the window; `hr_min ≤ hr_avg ≤ hr_max`.

## TC-07 · Dosage rate limiting  *(Task 3)*

**Steps** `control.dosage` = 60 from 0.
**Expected** The delivered dose (serial `[DOSE] t=… delivered …`, OLED footer) climbs at
5 mg/hr per second, reaching 60 after ≈ 12 s. It never jumps.
**Pass** Slope 5.00 ± 0.15 mg/hr/s computed from the first and last `[DOSE]` lines.

## TC-08 · Dosage critical confirmation  *(Task 3)*

**A — timeout.** `control.dosage` = 95; do nothing for 20 s.
**Expected** Delivered dose rises to 80 and **stops**; OLED THERAPY page shows
`>CONFIRM 95 mg/hr`, footer `DOSE 80.0 mg/hr CONF`; red LED blinks 5 × 60 ms and buzzer 3
sounds; after 15 s the prompt clears and the dose **stays at 80**.
**B — confirm.** `control.dosage` = 95, then within 15 s `control.dosage-confirm` = 1.
**Expected** `[DOSE] confirmed 95.0 mg/hr`; dose ramps 80 → 95 at 5 mg/hr/s; footer `CRIT`.
**Pass** Inaction never produces more than 80; confirmation releases 95 by ramp.

## TC-09 · Bed: smooth ramp, presets, manual  *(Task 4)*

**Steps** `control.bed-angle` = 90 from 0; then presets `sleep`, `breathing`, `emergency`.
**Expected** `[BED]` lines show 1° per 25 ms (0 → 90° ≈ 2.3 s); the servo horn sweeps
visibly; presets reach 10° / 45° / 90° (horn within ±3°); emergency ramps in ≈ 0.75 s but is
still a ramp; a manual command turns **Auto bed: OFF** (OLED THERAPY page).
**Pass** No instantaneous jump on any path.

## TC-10 · Automatic bed from patient condition  *(Task 4)*

**Steps** `control.bed-auto` = 1; `control.sim-spo2` = 88, then 82, then 0. Then
`control.bed-angle` = 70 and `control.sim-spo2` = 82 again.
**Expected** 88 → bed 45°; 82 (CRITICAL) → 90°; 0 (normal) → back to 10°; after the manual
70°, SpO₂ 82 does **not** move the bed (automation never overrides a manual command).
**Pass** Each angle reached within ±3°; manual wins.

## TC-11 · Adaptive sampling  *(Task 5)*

**Steps** (a) `control.sampling-rate` = 5; read `[SAMPLE]` timestamps. (b) Set 60, wait
~14 s, then set 5. (c) `control.sampling-mode` = 1 with a stable patient; then
`control.sim-hr` = 130; then 0.
**Expected** (a) samples ≈ 5.0 s apart, one publish per sample. (b) a new sample appears
within ≈ 1.5 s of the command, not up to 60 s later. (c) `[RATE] auto-adapt: … -> 30 s` when
stable, `… -> 5 s` when abnormal, and back to 30 s only after **3 fresh stable samples**.
**Pass** All three observations; hysteresis counted in samples, not loop ticks.

## TC-12 · Wi-Fi outage: offline, buffer, restore, resync  *(Task 6)*

**Steps** Set a 5 s rate. Slide **Wi-Fi cut** right; wait ~60 s; slide it back.
**Expected**
- Within ~2 s: `[FAULT] SW_WIFI_CUT ON`, `[NET] state ONLINE -> OFFLINE (fault: WIFI)`, OLED banner
  `*** LINK FAILURE ***` / `LOGGING OFFLINE` / `Cause: WIFI` / `Buffered: n` / `Retry in Ns` (a live countdown), amber LED, `[ALERT] buzzer 4, WIFI fault: 3 short beeps`.
- `[NET] recovery attempt, backoff=` 1, 2, 4, 8, 16, 32, 60 (capped).
- `Buffered:` rises by one per sampling period.
- On release: `[NET] Wi-Fi re-associated …`, `[NET] state OFFLINE -> ONLINE`, `restored -- replaying n buffered samples`, `resynced n records`; `system.backlog` receives n JSON records, oldest first, spaced as sampled.
**Pass** records out = records in, chronological, each exactly once; ONLINE again within ~25 s of release.

## TC-13 · Broker outage  *(Task 6)*

**Steps** Slide **Broker cut** right for ~30 s, then back.
**Expected** As TC-12 with `fault: MQTT`, `Cause: MQTT`, `2 long beeps`; replay on release.
**Pass** OFFLINE within ~2 s, resync complete after release.

## TC-14 · OLED shows the dose in real time  *(Task 3)*

**Steps** Change `control.dosage` and watch several different OLED pages.
**Expected** The footer `DOSE nn.n mg/hr NORM|WARN|CRIT|CONF` is on **every** screen
including the offline banner and updates about once a second.
**Pass** Footer present on all five pages and the banner; value follows the ramp.

## TC-15 · Command reliability  *(Tasks 3–5)*

**Steps** Publish `control.sampling-rate` = 10, 11, …, 21 at 1.5 s intervals.
**Expected** Twelve `[RATE] manual: N s` lines, one per command, none missing.
**Pass** 12 of 12 applied.

## TC-16 · Stability

**Steps** Run the whole plan; then read the serial log and the OLED SYSTEM page.
**Expected** A single boot banner; no watchdog/panic/abort; free heap steady (SYSTEM page).
**Pass** No unplanned reset; heap within ±2 KB of its post-boot value.

---

## Extra checks

- **Fever path** (needs a restart): set the DS18B20 to 39 °C → `High body temperature`
  CRITICAL, buzzer 1, rate 5 s; set back to 36.8 °C → "Patient stable".
- **Probe PIR**: the PIR switch only hides movement; it is not a patient-parameter probe, so
  it does not cause DEGRADED.

## Regression checklist after any code change

- [ ] All 10 tasks start (TC-01)
- [ ] No watchdog reset over a 10-minute run (TC-16)
- [ ] Alarms keep their cadence during an outage (TC-02)
- [ ] Dosage still cannot step (TC-07); gate still fails safe (TC-08)
- [ ] Buffer still replays in order, once each (TC-12)
- [ ] Docs, diagrams and the Wokwi project updated together
