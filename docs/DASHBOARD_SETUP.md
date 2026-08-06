# Dashboard Setup Guide — Adafruit IO

Build the two role-separated dashboards the project requires, then capture the
screenshots for submission. Budget about 30 minutes.

---

## Step 0 — Account and credentials

1. Sign up at **https://io.adafruit.com**.
2. Click the yellow key icon (**My Key**). Copy **Username** and **Active Key**.
3. In `main.ino`, replace:

```c
#define IO_USERNAME "[Username]"
#define IO_KEY      "[Key]"
```

---

## Step 1 — Decide your feed budget

> **The free tier allows 10 feeds.** The full build uses 20 publish feeds + 7 control
> feeds. Pick one path before you start creating feeds.

### Path A — Free tier (10 feeds)

Set in `main.ino`:

```c
#define ENABLE_EXTENDED_FEEDS 0
```

Create exactly these 10 feeds:

| # | Feed key | Group | Used by |
|---|----------|-------|---------|
| 1 | `medical.body-temperature` | medical | T2 gauge |
| 2 | `medical.heart-rate` | medical | T2 gauge |
| 3 | `medical.spo2` | medical | T2 gauge |
| 4 | `medical.patient-status` | medical | T2 text |
| 5 | `facility.room-temperature` | facility | T2 gauge |
| 6 | `facility.oxygen-level` | facility | T2 gauge |
| 7 | `facility.aqi` | facility | T2 gauge |
| 8 | `system.state` | system | T6 |
| 9 | `control.dosage` | control | T3 slider |
| 10 | `control.bed-angle` | control | T4 slider |

**Trade-off, stated plainly — read this before choosing Path A.** Ten feeds is not enough
to evidence all six tasks on a dashboard. Path A drops:

| Lost on free tier | Which task clause it evidences |
|---|---|
| `medical.blood-pressure`, `medical.ecg` | T1 — the added health parameters |
| `medical.alerts`, `facility.alerts` | T2 — "additional alert feeds for abnormal conditions" |
| `medical.summary`, `facility.summary` | T2 — "periodic data aggregation" |
| `control.dosage-confirm` | T3 — "critical values may require confirmation" |
| `control.bed-preset`, `control.bed-auto` | T4 — presets and automatic adjustment |
| `control.sampling-rate`, `control.sampling-mode` | T5 — the whole dashboard-facing half |
| `system.backlog` | T6 — the resync evidence |

The firmware logic is unchanged and still runs — you can demonstrate T3/T4/T5/T6 from the
OLED and serial monitor — but **Task 2's aggregation clause cannot be satisfied at all**
on free tier, because the summary feeds do not exist. `aggPublish()` is compiled out under
`ENABLE_EXTENDED_FEEDS 0` for exactly this reason (see the note below).

**Use Path B or C if you possibly can.** The portal explicitly asks for dashboard
screenshots, and the rubric requires all tasks fully integrated.

> **Why aggregation is compiled out rather than left to fail.** Publishing to a feed that
> doesn't exist fails every time. Two consecutive publish failures put the connectivity
> state machine into DEGRADED (by design — see T6). Left unguarded, a free-tier build
> would sit in DEGRADED permanently and look broken. The `#if !ENABLE_EXTENDED_FEEDS`
> guard in `aggPublish()` prevents that. The same reasoning is why `drainOfflineBuffer()`
> gives up after 3 consecutive failures instead of retrying forever.

### Path B — Adafruit IO Plus (~$10/month)

Leave `ENABLE_EXTENDED_FEEDS 1`. Create all 27 feeds. Everything below works as written.
One month is enough to record the demo and cancel.

### Path C — Free unlimited: HiveMQ + Node-RED

Also leave `ENABLE_EXTENDED_FEEDS 1`, then in `main.ino`:

```c
#define AIO_SERVER     "broker.hivemq.com"
#define AIO_SERVERPORT 1883
#define IO_USERNAME    "rprms"     // any unique prefix — becomes your topic root
#define IO_KEY         ""          // HiveMQ public broker needs no auth
```

`Adafruit_MQTT` is a plain MQTT client, so no other code changes. Install Node-RED
locally, add `node-red-dashboard`, and build gauges/sliders against the same topic
strings. Unlimited topics, no cost. Slightly more setup work than Path A/B.

---

## Step 2 — Create the feeds

**Feeds → New Group** for each of `medical`, `facility`, `system`, `control`.
Then **New Feed** inside each group with these keys (the `group.feed` form is what the
firmware publishes to):

<details>
<summary><b>medical</b> — 8 feeds</summary>

`body-temperature`, `heart-rate`, `spo2`, `blood-pressure`, `ecg`,
`patient-status`, `alerts`, `summary`
</details>

<details>
<summary><b>facility</b> — 7 feeds</summary>

`room-temperature`, `humidity`, `oxygen-level`, `aqi`, `motion`, `alerts`, `summary`
</details>

<details>
<summary><b>system</b> — 5 feeds</summary>

`state`, `dosage`, `bed-angle`, `sampling-rate`, `backlog`
</details>

<details>
<summary><b>control</b> — 7 feeds</summary>

`dosage`, `dosage-confirm`, `bed-angle`, `bed-preset`, `bed-auto`,
`sampling-rate`, `sampling-mode`
</details>

> **Feed key ≠ feed name.** Adafruit IO derives the key from the name. Verify each
> key matches exactly — a mismatch means silent no-data.

---

## Step 3 — Medical Staff Dashboard

**Dashboards → New Dashboard** → name it `Medical Staff Dashboard`.
Add blocks with the ⚙ / **+** button:

| Block | Feed | Settings |
|-------|------|----------|
| **Gauge** | `medical.heart-rate` | Min 40, Max 180, Label "Heart Rate bpm"<br>Low warn 60 · High warn 120 · Low crit 50 · High crit 150 |
| **Gauge** | `medical.spo2` | Min 70, Max 100, Label "SpO₂ %"<br>Low warn 94 · Low crit 90 |
| **Gauge** | `medical.body-temperature` | Min 33, Max 42, Label "Body Temp °C"<br>High warn 37.5 · High crit 38.0 |
| **Text** | `medical.blood-pressure` | Label "Blood Pressure mmHg" |
| **Text** | `medical.ecg` | Label "ECG mV" |
| **Text** | `medical.patient-status` | Label "Clinical Status" |
| **Stream** | `medical.alerts` | Label "Clinical Alert Log" |
| **Line Chart** | `medical.heart-rate`, `medical.spo2` | 1 hour window, "Vitals Trend" |
| **Text** | `medical.summary` | Label "60 s Aggregate" |

### Control blocks (Tasks 3, 4, 5)

| Block | Feed | Settings |
|-------|------|----------|
| **Slider** | `control.dosage` | Min 0, Max 100, Step 1, Label "Medication mg/hr" |
| **Toggle** | `control.dosage-confirm` | On = `1`, Off = `0`, Label "CONFIRM CRITICAL DOSE" |
| **Slider** | `control.bed-angle` | Min 0, Max 90, Step 1, Label "Bed Angle °" |
| **Toggle** | `control.bed-auto` | On = `1`, Off = `0`, Label "Auto Bed Control" |
| **Slider** | `control.sampling-rate` | Min 5, Max 60, Step 1, Label "Sampling Interval s" |
| **Toggle** | `control.sampling-mode` | On = `1`, Off = `0`, Label "Adaptive Sampling" |
| **Text input** | `control.bed-preset` | Send `sleep`, `breathing` or `emergency` |
| **Gauge** | `system.dosage` | Min 0, Max 100, "Delivered Dose" — high crit 80 |
| **Gauge** | `system.bed-angle` | Min 0, Max 90, "Actual Bed Angle" |
| **Text** | `system.state` | Label "Link State" |

> Adafruit IO has no three-button block. If you prefer buttons over a text input,
> add three **Momentary Button** blocks on `control.bed-preset` with values
> `sleep`, `breathing`, `emergency`.

The two readback gauges (`system.dosage`, `system.bed-angle`) next to their sliders are
what make Tasks 3 and 4 visible on camera: you drag the slider, and the readback gauge
*ramps* toward it instead of jumping. That is the rate limiter working.

---

## Step 4 — Facility Management Dashboard

**Dashboards → New Dashboard** → `Facility Management Dashboard`.

| Block | Feed | Settings |
|-------|------|----------|
| **Gauge** | `facility.room-temperature` | Min 10, Max 35, "Room Temp °C"<br>Low warn 18 · High warn 26 · Low crit 16 · High crit 28 |
| **Gauge** | `facility.oxygen-level` | Min 15, Max 25, "Oxygen %"<br>Low warn 19.5 · High warn 23.5 · Low crit 19.0 |
| **Gauge** | `facility.aqi` | Min 0, Max 500, "Air Quality Index"<br>High warn 150 · High crit 300 |
| **Text** | `facility.humidity` | Label "Humidity %" |
| **Text** | `facility.motion` | Label "Room Occupancy" |
| **Stream** | `facility.alerts` | Label "Environmental Alert Log" |
| **Line Chart** | `facility.room-temperature`, `facility.aqi` | 1 hour, "Environment Trend" |
| **Text** | `facility.summary` | Label "60 s Aggregate" |

**Add no `medical.*` block to this dashboard.** That absence *is* the role separation,
and it is the thing to point at in the demo video.

---

## Step 5 — Prove the role separation

Screenshot both dashboards side by side and state in your report:

> The Facility dashboard subscribes only to `facility.#`. On a broker with topic ACLs
> (Mosquitto, HiveMQ, AWS IoT Core), a facilities credential scoped to `facility.#`
> is cryptographically unable to receive `medical.heart-rate`. Separation is enforced
> at the transport layer, not by hiding widgets in the UI.

---

## Step 6 — Screenshots to capture

Save into `screenshots/`:

| Filename | Content |
|----------|---------|
| `01-medical-dashboard-normal.png` | All gauges green |
| `02-medical-dashboard-critical.png` | HR or SpO₂ in the red band + alert stream populated |
| `03-facility-dashboard-normal.png` | All environment gauges green |
| `04-facility-dashboard-alert.png` | AQI pot turned up past 300, alert stream populated |
| `05-dosage-slider-ramp.png` | Slider at 100, `system.dosage` gauge mid-ramp (catches the rate limiter) |
| `06-dosage-critical-confirm.png` | OLED showing `>CONFIRM`, red LED lit |
| `07-bed-control.png` | Bed slider + readback gauge + servo horn in Wokwi |
| `08-sampling-adaptive.png` | `system.sampling-rate` showing 5 s during an abnormal episode |
| `09-offline-banner.png` | Wokwi OLED showing `LOGGING OFFLINE` + buffered count |
| `10-backlog-resync.png` | `system.backlog` feed filling with JSON after reconnect |
| `11-wokwi-circuit.png` | Full circuit view |
| `12-serial-monitor.png` | `[BOOT] 9 FreeRTOS tasks started` and `[NET]` lines |

---

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| Blocks stay "—" | Feed key mismatch | Compare the key in Adafruit IO to the `#define` in §3 |
| `Throttle warning` email | >30 data points/min on free tier | Raise the sampling slider to 15 s+, or disable extended feeds |
| Sliders do nothing | Publishing to `medical.*` instead of `control.*` | Control blocks must target the `control` group |
| OLED shows OFFLINE forever | Wrong username/key, or blocked port 1883 | Check credentials; try a different network |
| Dosage gauge won't pass 80 | Working as designed | Toggle `control.dosage-confirm` on within 15 s |
| Bed ignores the slider | Auto mode re-enabled | Any manual move disables auto; check `control.bed-auto` is off |
| Data stops after ~10 feeds | Free-tier limit reached | See Step 1, Path A/B/C |
| Stuck in DEGRADED forever, link looks fine | Publishing to feeds that don't exist — every publish fails, which is what DEGRADED detects | Create the missing feeds, or set `ENABLE_EXTENDED_FEEDS 0` |
| `[NET] backlog replay stalled` in serial | `system.backlog` feed missing | Create it; records are retained meanwhile, not lost |
