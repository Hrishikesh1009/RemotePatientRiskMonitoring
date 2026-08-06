# Medical Staff Dashboard — Block Specification

**Audience:** doctors, nurses, clinical staff
**Subscribes to:** `medical.#` and `system.#`
**Publishes to:** `control.#`
**Must never display:** any `facility.*` feed

---

## Suggested layout (Adafruit IO 12-column grid)

```
┌──────────────┬──────────────┬──────────────┬──────────────┐
│  HEART RATE  │     SpO₂     │  BODY TEMP   │  LINK STATE  │
│    gauge     │    gauge     │    gauge     │     text     │
├──────────────┼──────────────┼──────────────┴──────────────┤
│ BLOOD PRESS. │   ECG (mV)   │      CLINICAL STATUS        │
│     text     │     text     │            text             │
├──────────────┴──────────────┴─────────────────────────────┤
│                    VITALS TREND (1 h)                     │
│                       line chart                          │
├───────────────────────────────────────────────────────────┤
│                  CLINICAL ALERT LOG                       │
│                        stream                             │
├──────────────┬──────────────┬──────────────┬──────────────┤
│ MEDICATION   │  DELIVERED   │  BED ANGLE   │ ACTUAL ANGLE │
│   slider     │    gauge     │    slider    │    gauge     │
├──────────────┼──────────────┼──────────────┼──────────────┤
│ CONFIRM DOSE │  AUTO BED    │  SAMPLING s  │  ADAPTIVE    │
│    toggle    │   toggle     │    slider    │   toggle     │
├──────────────┴──────────────┴──────────────┴──────────────┤
│              BED PRESET (sleep/breathing/emergency)       │
│                       text input                          │
└───────────────────────────────────────────────────────────┘
```

---

## Monitoring blocks

| Block | Feed | Type | Config |
|-------|------|------|--------|
| Heart Rate | `medical.heart-rate` | Gauge | Min 40 · Max 180 · low warn 60 · high warn 120 · low crit 50 · high crit 150 |
| SpO₂ | `medical.spo2` | Gauge | Min 70 · Max 100 · low warn 94 · low crit 90 |
| Body Temp | `medical.body-temperature` | Gauge | Min 33 · Max 42 · high warn 37.5 · high crit 38.0 |
| Blood Pressure | `medical.blood-pressure` | Text | String `sys/dia` |
| ECG | `medical.ecg` | Text | mV, 2 dp |
| Clinical Status | `medical.patient-status` | Text | Plain-language risk message |
| Link State | `system.state` | Text | ONLINE / DEGRADED / OFFLINE |
| Vitals Trend | `medical.heart-rate` + `medical.spo2` | Line chart | 1 hour window |
| Clinical Alert Log | `medical.alerts` | Stream | `[LEVEL] message` |
| 60 s Aggregate | `medical.summary` | Text | JSON avg/min/max |

---

## Control blocks

| Block | Feed | Type | Config | Task |
|-------|------|------|--------|------|
| Medication Dose | `control.dosage` | Slider | 0–100, step 1 | T3 |
| Delivered Dose | `system.dosage` | Gauge | 0–100, high crit 80 | T3 |
| Confirm Critical | `control.dosage-confirm` | Toggle | on `1` / off `0` | T3 |
| Bed Angle | `control.bed-angle` | Slider | 0–90, step 1 | T4 |
| Actual Angle | `system.bed-angle` | Gauge | 0–90 | T4 |
| Auto Bed | `control.bed-auto` | Toggle | on `1` / off `0` | T4 |
| Bed Preset | `control.bed-preset` | Text input | `sleep` \| `breathing` \| `emergency` | T4 |
| Sampling Interval | `control.sampling-rate` | Slider | 5–60, step 1 | T5 |
| Adaptive Mode | `control.sampling-mode` | Toggle | on `1` / off `0` | T5 |
| Actual Rate | `system.sampling-rate` | Gauge | 5–60 | T5 |

---

## Why slider + readback gauge pairs

`control.dosage` is what the doctor *asked for*. `system.dosage` is what the device is
*actually delivering*. Placing them adjacent makes the rate limiter visible: drag the
slider to 100 and the gauge climbs toward it over 20 seconds rather than snapping.

Same for `control.bed-angle` / `system.bed-angle`.

This pairing is the single most useful thing to have on screen while recording the demo
video — without it, Tasks 3 and 4 look identical to a naive `write(slider)` implementation.

---

## Alarm behaviour to expect

| Condition | Gauge | Alert stream | Device |
|-----------|-------|--------------|--------|
| HR < 50 or > 150 | Red | `[CRITICAL] Severe bradycardia/tachycardia` | Buzzer 2, 3 beeps |
| HR 50–60 or 120–150 | Amber | `[WARNING] Heart rate abnormal` | Buzzer 2, 1 beep |
| SpO₂ < 85 | Red | `[CRITICAL] Severe hypoxaemia` | Buzzer 3, 3 beeps |
| SpO₂ 85–90 | Amber | `[WARNING] SpO2 below normal` | — |
| Temp ≥ 38.0 | Red | `[CRITICAL] High body temperature` | Buzzer 1, 2 beeps |
| Temp 37.5–38.0 | Amber | `[WARNING] Mild fever` | — |
| Sensor disconnected | — | `[WARNING] Sensor disconnected` | Buzzer 4 chirp, DEGRADED |
| Dose > 80 mg/hr | `system.dosage` red | — | Red LED rapid blink + buzzer 3 |
