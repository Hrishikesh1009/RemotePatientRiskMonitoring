# Facility Management Dashboard — Block Specification

**Audience:** maintenance, HVAC, biomedical facilities staff
**Subscribes to:** `facility.#` only
**Publishes to:** nothing
**Must never display:** any `medical.*` feed — no heart rate, no SpO₂, no body
temperature, no blood pressure, no ECG, no clinical status

That last line is the deliverable. The absence of patient data on this dashboard is what
demonstrates Task 2's role-based access requirement, and it is the thing to point at in
the demo video.

---

## Suggested layout

```
┌──────────────┬──────────────┬──────────────┐
│  ROOM TEMP   │  OXYGEN %    │     AQI      │
│    gauge     │    gauge     │    gauge     │
├──────────────┼──────────────┴──────────────┤
│   HUMIDITY   │      ROOM OCCUPANCY         │
│     text     │            text             │
├──────────────┴─────────────────────────────┤
│           ENVIRONMENT TREND (1 h)          │
│                line chart                  │
├────────────────────────────────────────────┤
│         ENVIRONMENTAL ALERT LOG            │
│                  stream                    │
├────────────────────────────────────────────┤
│            60 s AGGREGATE                  │
│                  text                      │
└────────────────────────────────────────────┘
```

---

## Blocks

| Block | Feed | Type | Config |
|-------|------|------|--------|
| Room Temperature | `facility.room-temperature` | Gauge | Min 10 · Max 35 · low warn 18 · high warn 26 · low crit 16 · high crit 28 |
| Oxygen Level | `facility.oxygen-level` | Gauge | Min 15 · Max 25 · low warn 19.5 · high warn 23.5 · low crit 19.0 |
| Air Quality Index | `facility.aqi` | Gauge | Min 0 · Max 500 · high warn 150 · high crit 300 |
| Humidity | `facility.humidity` | Text | % RH |
| Room Occupancy | `facility.motion` | Text | `1` = movement, `0` = still |
| Environment Trend | `facility.room-temperature` + `facility.aqi` | Line chart | 1 hour |
| Environmental Alert Log | `facility.alerts` | Stream | `[LEVEL] message` |
| 60 s Aggregate | `facility.summary` | Text | JSON avg/max |

---

## Threshold rationale

| Parameter | Safe band | Source |
|-----------|-----------|--------|
| Room temperature | 18–26 °C | Standard hospital ward comfort range |
| Oxygen concentration | 19.5–23.5 % | OSHA confined-space limits — below 19.5 % is oxygen-deficient, above 23.5 % is an enhanced fire risk |
| AQI | 0–150 | US EPA — above 150 is unhealthy for the general population, above 300 hazardous |

Oxygen has both a low **and** a high critical limit. That is deliberate: in a ward using
supplemental oxygen, an enriched atmosphere is a fire hazard, so a one-sided threshold
would miss a real danger.

---

## Alarm behaviour

| Condition | Gauge | Alert stream | Device |
|-----------|-------|--------------|--------|
| O₂ < 19.0 % or > 23.5 % | Red | `[CRITICAL] Oxygen level unsafe` | Buzzer 1, 200 ms |
| AQI > 300 | Red | `[CRITICAL] Air quality hazardous` | Buzzer 1, 200 ms |
| AQI 150–300 | Amber | `[WARNING] Air quality poor` | — |
| Room temp outside 18–26 °C | Amber | `[WARNING] Room temp out of range` | — |
| DHT22 read failure | Amber | `[WARNING] Env sensor fault` | — |

---

## Demonstrating the separation

During the demo, open both dashboards side by side and say:

> "This is the facilities view. There is no patient data on it at all — not hidden, not
> filtered client-side. The two roles subscribe to different MQTT topic prefixes. On a
> broker with topic ACLs, a facilities credential scoped to `facility/#` is
> cryptographically unable to receive `medical.heart-rate`. The separation is enforced at
> the transport layer."

Then turn the AQI potentiometer past 300 and show this dashboard alarming while the
medical dashboard stays green — the two data planes are genuinely independent.
