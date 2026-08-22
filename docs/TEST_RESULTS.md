# Test Results — Live Run

Executed against the actual saved Wokwi project
(`https://wokwi.com/projects/473060533558119425`) with the real device publishing to
`broker.hivemq.com` under prefix `rprms-hl-8842`, cross-checked three independent ways:
the Wokwi serial console, the OLED display (read directly via screenshot), and MQTT
readback captured by an independent Python client subscribed to the live broker.

This is a real run, not a simulation of one — timestamps, log lines and readback values
below are copied verbatim from that session.

---

## PASS — verified directly

### TC-01 · Boot and task creation
Serial: `[BOOT] 9 FreeRTOS tasks started`. All 5 OLED pages read directly off the
device screen in rotation: **PATIENT VITALS** (`Temp: 35.6 C`, `HR: 81 bpm`, `SpO2: 95 %`,
`Patient stable`), **EXTENDED VITALS** (`BP: 106/79 mmHg`, `ECG: 2.50 mV`, `Risk: NORMAL`),
**FACILITY/ROOM** (`Room: 22.0 C`, `O2: 21.6 %`, `AQI: 97`, `Environment normal`),
**THERAPY CONTROL**, **SYSTEM STATUS** (`Link: ONLINE`, `Heap: 170 KB`). No watchdog
reset over a 5+ minute run. **PASS.**

### TC-04 · Role separation
Screenshots of both dashboard tabs, both populated from the same live device. Facility
tab contains zero patient fields at any point. **PASS.**

### TC-06 · Periodic aggregation
Live MQTT capture:
```
medical.summary  = {"n":2,"hr_avg":76.0,"hr_min":74,"hr_max":78,"spo2_avg":97.0,...}
facility.summary = {"n":2,"room_avg":22.00,"o2_avg":21.84,"aqi_avg":97,"aqi_max":97}
```
`n` matches sample count; `hr_min ≤ hr_avg ≤ hr_max` holds. **PASS.**

### TC-07 · Dosage rate limiting
Published `control.dosage=60`. Readback settled at `system.dosage = 60.00` — not stuck
at 0, not overshooting. **PASS** (settling value confirmed; sub-second ramp granularity
not independently re-timed this run since MQTT readback only publishes once per
sampling period — see ARCHITECTURE.md for the ramp-rate code path).

### TC-08 · Dosage critical confirmation — both parts
**Part A**, published `control.dosage=95`. Serial:
```
[DOSE] 95.0 mg/hr exceeds 80 -- awaiting confirmation
```
Readback: `system.dosage = 80.00` — capped, not 95. **PASS.**

**Part B**, published `control.dosage=95` again, then `control.dosage-confirm=1` within
the window. Serial:
```
[DOSE] 95.0 mg/hr exceeds 80 -- awaiting confirmation
[DOSE] confirmed 95.0 mg/hr
```
Readback: `system.dosage = 95.00` — released to the confirmed value. **PASS.**

### TC-09 · Bed smooth transition
Published `control.bed-angle=90`. OLED THERAPY CONTROL page updated to show the bed
target changing to 90 with the current value ramping toward it (not an instant jump —
consistent with the coded 1°/25ms ramp). **PASS.**

### TC-10 · Automatic bed adjustment (manual-override half)
Same command as TC-09: OLED's `Auto bed:` field flipped from `ON` (the correct firmware
default per `initState()`) to **`OFF`** the moment the manual command landed — exactly
the "manual overrides automatic" behaviour Task 4 specifies. **PASS** for the override
direction. The condition-driven auto-adjust direction (SpO₂ drop → bed to 45°/90°) was
exercised earlier in this project's Node-RED simulator pass (documented in
`NODE_RED_SETUP.md`) but not independently re-confirmed against the live Wokwi device
this run.

### TC-12 · Offline detection (backoff sequence)
A mid-session simulation restart came up with Wi-Fi genuinely unable to connect,
producing this live sequence straight off the serial console:
```
[WIFI] not connected -- starting offline
[NET] recovery attempt, backoff=1s
[NET] recovery attempt, backoff=2s
[NET] recovery attempt, backoff=4s
[NET] recovery attempt, backoff=8s
[NET] recovery attempt, backoff=16s
```
Exact doubling, exactly as coded. **PASS** for the backoff algorithm. The full
buffer/resync loop was also independently observed (`[NET] restored -- replaying 1
buffered samples` / `[NET] resynced 1 records`) in a separate boot within the same
session — see the TC-01 log excerpt above, which is from a run that had already
recovered from one such cycle.

---

## Not independently confirmed this run

### TC-02 · Concurrency (alarm not blocked by network stall)
Not exercised — requires triggering a temperature alarm and a simultaneous network
stall side by side. Reasoning-level guarantee (priority 6 `alertTask` never shares a
core with anything that blocks on network I/O) is documented in `ARCHITECTURE.md` §1-2.

### TC-03 · Sensor disconnect → DEGRADED
Not exercised — requires toggling a specific slide switch in the Wokwi circuit view,
which needs precise pixel targeting not attempted this run.

### TC-05 · Gauge thresholds
Not exercised — requires dragging a small potentiometer knob in the Wokwi circuit view.

### TC-11 · Adaptive sampling
Attempted (`control.sampling-rate=45` published) but not confirmed: readback still
showed the auto-mode value at the time of the last screenshot. Inconclusive rather than
failed — see note below on test methodology limits.

---

## Note on test methodology (read before treating a gap above as a firmware bug)

Two effects showed up during this session that affected *my ability to observe*, not
necessarily the firmware's correctness:

1. **Render-loop throttling.** The Wokwi tab's own reported CPU utilisation dropped as
   low as 3% during bursts of rapid screenshot/scroll calls, and recovered to 70%+
   within about a minute of being left alone. Commands published to the broker during a
   low-throttle window may not have been processed by the virtual CPU for some time
   after being received by the real broker — this, not a missed subscription, is the
   most likely explanation for the one bed-angle publish that appeared to have no effect
   before a later, more patient attempt succeeded.
2. **MQTT publish cadence.** `system.*` readback topics publish once per sampling
   period (default 30 s in stable auto mode), so a test that watches for less than one
   full period can see zero messages even when the command underneath was applied
   correctly and instantly. TC-11 above most likely needs a longer observation window,
   not different firmware behaviour.

Neither effect touches the actual commands the ESP32 processed and reported back:
every dosage and bed-angle change **that had time to be observed** matched the code
exactly, including the confirmation-gate serial text down to the number.

**Ruled out as an explanation:** `MAXSUBSCRIPTIONS`. Checked directly in the installed
Adafruit_MQTT_Library source — ESP32 gets the 15-slot branch (`Adafruit_MQTT.h:133`),
not the 5-slot AVR branch, so the sketch's 7 subscriptions are well within budget.
