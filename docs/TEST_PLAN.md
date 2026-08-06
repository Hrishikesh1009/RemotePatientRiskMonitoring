# Test Plan

Twelve test cases covering all six tasks. Each is reproducible in Wokwi with no extra
hardware. Run them in order; several set up state the next one uses.

**Legend** — SW1 = GPIO32 (body temp), SW2 = GPIO33 (heart rate), SW3 = GPIO25 (SpO₂),
SW4 = GPIO26 (motion). Switch **closed to GND** = sensor enabled.

---

## TC-01 · Boot and task creation  *(Task 1)*

**Steps**
1. Start the Wokwi simulation.
2. Watch the serial monitor at 115200 baud.
3. Watch the OLED for 20 seconds.

**Expected**
- Serial prints `=== Remote Patient Risk Monitoring System ===` then
  `[BOOT] 9 FreeRTOS tasks started`.
- OLED shows `RPRMS booting...`, then cycles five pages at 3 s each:
  PATIENT VITALS → EXTENDED VITALS → FACILITY / ROOM → THERAPY CONTROL → SYSTEM STATUS.
- Green LED (GPIO2) blinks once the link is up.
- No watchdog reset within 2 minutes.

**Pass criteria** All five pages appear in order; heap on page 5 is stable across
several rotations.

---

## TC-02 · Concurrency — alarms are not blocked by other work  *(Task 1)*

**Steps**
1. Raise the DS18B20 above 38 °C (click it in Wokwi, set 39).
2. While buzzer 1 is sounding, watch the OLED continue its rotation.
3. Disconnect Wi-Fi (see TC-12) so `mqttTask` starts retrying.

**Expected** Buzzer cadence stays constant while the display refreshes and the network
retries.

**Pass criteria** No audible stutter or gap in the alarm pattern during a network stall.
This is the defect in the original single-task design that the priority split fixes.

---

## TC-03 · Sensor disconnect → DEGRADED  *(Tasks 1, 6)*

**Steps**
1. Open SW2 (heart-rate).
2. Observe OLED page 1 and page 5.
3. Close SW2 again.

**Expected**
- Page 1 shows `HR : -- bpm`.
- Clinical status becomes `Sensor disconnected`, risk WARNING.
- Page 5 shows `Link: DEGRADED`.
- Buzzer 4 emits a single 60 ms chirp roughly every 5 s; yellow LED lit.
- Closing SW2 returns the system to ONLINE within ~2 s.

**Pass criteria** The system never reports "Patient stable" while a probe is
disconnected.

---

## TC-04 · Role separation  *(Task 2)*

**Steps**
1. Open both dashboards side by side.
2. Let the system run for 2 minutes.

**Expected** Medical dashboard populates HR/SpO₂/temp/BP/ECG. Facility dashboard
populates room temp/O₂/AQI/humidity. Neither shows the other's data.

**Pass criteria** No patient identifier or vital appears anywhere on the Facility
dashboard.

---

## TC-05 · Gauge thresholds  *(Task 2)*

**Steps**
1. Turn the AQI pot (GPIO35) slowly from minimum to maximum.
2. Watch the `facility.aqi` gauge.

**Expected** Green below 150, amber 150–300, red above 300. `facility.alerts` receives
`[WARNING] Air quality poor` then `[CRITICAL] Air quality hazardous`.

**Pass criteria** Colour transitions occur at the documented values.

---

## TC-06 · Periodic aggregation  *(Task 2)*

**Steps**
1. Let the system run for 3 minutes.
2. Open the `medical.summary` and `facility.summary` feeds.

**Expected** One JSON record per minute on each, e.g.
`{"n":4,"hr_avg":82.5,"hr_min":74,"hr_max":91,"spo2_avg":96.8,"spo2_min":95,"temp_avg":36.91}`

**Pass criteria** `n` matches the number of samples in the window; `hr_min ≤ hr_avg ≤ hr_max`.

---

## TC-07 · Dosage rate limiting  *(Task 3)*

**Steps**
1. Drag `control.dosage` from 0 to 60 in one motion.
2. Start a stopwatch and watch the `system.dosage` readback gauge and OLED page 4.

**Expected** The delivered dose climbs at 5 mg/hr per second, reaching 60 after ≈12 s.
It never jumps.

**Pass criteria** Measured ramp time is 12 s ± 2 s. A 0→100 drag takes ≈20 s.

---

## TC-08 · Dosage critical confirmation  *(Task 3)*

**Part A — timeout**
1. Set `control.dosage` to 95.
2. Do nothing for 20 seconds.

**Expected** OLED page 4 shows `>CONFIRM 95 mg/hr`. Delivered dose ramps to 80 and
**stops**. After 15 s the prompt clears and the dose stays at 80. Serial prints
`[DOSE] 95.0 mg/hr exceeds 80 -- awaiting confirmation`.

**Part B — confirm**
1. Set `control.dosage` to 95 again.
2. Within 15 s, toggle `control.dosage-confirm` on.

**Expected** Serial prints `[DOSE] confirmed 95.0 mg/hr`. Dose ramps from 80 to 95 over
3 s. Red LED blinks rapidly (5 × 60 ms); buzzer 3 sounds twice.

**Pass criteria** Inaction produces the *safe* outcome. There is no path from silence to
95 mg/hr.

---

## TC-09 · Bed smooth transition  *(Task 4)*

**Steps**
1. Set `control.bed-angle` to 90.
2. Watch the servo horn in Wokwi and time the travel.

**Expected** The horn sweeps continuously and visibly; ≈2.3 s from 0° to 90°.
`system.bed-angle` climbs through intermediate values.

**Then** publish `emergency` to `control.bed-preset` from 0°: travel completes in
≈0.75 s — faster, but still a visible ramp, not a snap.

**Pass criteria** No instantaneous jump on any path.

---

## TC-10 · Automatic bed adjustment  *(Task 4)*

**Steps**
1. Toggle `control.bed-auto` on.
2. Wait for the simulated SpO₂ to drop below 90 (or open SW3 briefly then close it and
   wait for a low reading — the simulation drifts and desaturates when HR is high).
3. Then drag `control.bed-angle` manually.

**Expected**
- SpO₂ < 90 → bed ramps to 45°; below 85 with CRITICAL risk → 90°.
- Risk returns to NORMAL → bed returns to 10°.
- The manual drag sets auto off (OLED page 4 shows `Auto bed: OFF`) and the automation
  stops moving the bed.

**Pass criteria** Automatic control never overrides a manual command.

---

## TC-11 · Adaptive sampling  *(Task 5)*

**Steps**
1. Ensure `control.sampling-mode` is on (auto).
2. Watch OLED page 5 `Rate:` while the patient is stable.
3. Raise the DS18B20 to 39 °C.
4. Return it to 36.8 °C and wait.
5. Then move `control.sampling-rate` to 45 manually.

**Expected**
- Stable → `30s (auto)` after 3 normal cycles.
- Abnormal → drops to `5s (auto)` within one cycle.
- Recovery → returns to 30 s only after 3 consecutive normal cycles (hysteresis).
- Manual slider → `45s (man)`, auto disabled, and the change takes effect
  **immediately** rather than after the current sleep expires.

**Pass criteria** The rate change is visible in under 2 s — proof that the wake queue is
waking the sleeping task rather than the task waiting out its full sleep.

**Also verify all three sampling tasks respond, not just one.** Set the slider to 5 s
while stable and watch the serial monitor / dashboard: `medical.heart-rate`,
`medical.blood-pressure` **and** `facility.room-temperature` must all start updating at
the new cadence. If BP or room temperature keeps updating at the old rate, the per-task
wake queues have been collapsed back into one shared queue — a token consumed by the
first waiter starves the others.

---

## TC-12 · Offline detection, buffering and resync  *(Task 6)*

**Steps**
1. With the system ONLINE and buffering empty, break the link. In Wokwi the simplest
   method is to edit `WIFI_SSID` to a wrong value and restart; to break it *mid-run*,
   pause the simulation, or set `AIO_SERVER` to an unreachable host to isolate the MQTT
   failure path specifically.
2. Observe the OLED for 60 seconds.
3. Restore the correct value and let it reconnect.
4. Watch `system.backlog` and the serial monitor.

**Expected**
- Within ~2 s: OLED shows the fixed banner
  ```
  *** LINK FAILURE ***
  LOGGING OFFLINE
  Cause: WIFI          (or MQTT)
  Buffered: n
  Retry in 8s
  ```
- `Buffered:` increments once per sampling period.
- `Retry in` counts 1 → 2 → 4 → 8 → 16 → 32 → 60 and caps at 60.
- Buzzer 4: three 80 ms shorts for Wi-Fi, two 400 ms longs for MQTT. Yellow LED lit.
- On restore: serial prints `[NET] restored -- replaying n buffered samples` then
  `[NET] resynced n records`. `system.backlog` fills with JSON at ~600 ms intervals.
- OLED page 5 `Sync:` counter matches the number of records buffered.

**Pass criteria** Record count out equals record count in, in chronological order, with
no gap in the timeline.

---

## Regression checklist after any code change

- [ ] All 9 tasks still start (TC-01)
- [ ] No watchdog reset over a 10-minute run
- [ ] Free heap stable — no growth over 10 minutes (page 5)
- [ ] Alarms still sound during a network stall (TC-02)
- [ ] Dosage still cannot step (TC-07)
- [ ] Buffer still replays in order (TC-12)
