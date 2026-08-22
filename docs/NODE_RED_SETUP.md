# Node-RED Dashboard Setup (free, unlimited feeds)

The alternative to Adafruit IO. Free, no feed cap, and therefore the only route that
can evidence **every** clause of Task 2 — including the periodic aggregation the
10-feed free tier cannot support.

`dashboards/node-red-flows.json` is a **complete, pre-built dashboard**: 78 nodes, all
27 MQTT topics already wired to the right widget. You import it; you do not build it.

Budget ~25 minutes, most of which is installing Node-RED.

---

## What you get

**Two tabs**, mirroring the two MQTT topic groups — the role separation is visible in
the UI, not just asserted in the report.

| Medical Staff tab | Facility Management tab |
|---|---|
| Heart rate, SpO₂, body temp gauges with clinical thresholds | Room temp, oxygen, AQI gauges with safe-range bands |
| Blood pressure, ECG, clinical status, 60 s aggregate | Humidity, occupancy, 60 s aggregate |
| Vitals trend chart (1 h) | Environment trend chart (1 h) |
| **Therapy Control** — 3 sliders, 3 toggles, 3 preset buttons | **System Health** — link state, sampling interval, resync log |
| **Actuator readback** — delivered dose + actual bed angle | Environmental alert log (last 30) |
| Clinical alert log (last 30) | |

The **Actuator Readback** gauges are the ones to point at in the demo video: drag the
dosage slider and the readback gauge *ramps* toward it instead of jumping. That is the
Task 3 rate limiter, visible on camera.

---

## Step 1 — Install Node-RED

Needs Node.js 18+ ([nodejs.org](https://nodejs.org)).

```bash
npm install -g --unsafe-perm node-red
```

Then install the dashboard nodes:

```bash
npm install -g node-red-dashboard
```

Start it:

```bash
node-red
```

Editor at **http://127.0.0.1:1880** · dashboard at **http://127.0.0.1:1880/ui**

> If `node-red-dashboard` did not install globally, add it from the editor instead:
> **☰ menu → Manage palette → Install → search `node-red-dashboard` → Install**.
> This flow uses dashboard **v1** node types (`ui_gauge`, `ui_slider`, …), which is what
> `node-red-dashboard` provides. Do **not** install `@flowfuse/node-red-dashboard`
> (v2) — its node types are different and the import will show unknown nodes.

---

## Step 2 — Import the dashboard

1. Open **http://127.0.0.1:1880**
2. **☰ menu → Import**
3. Paste the entire contents of `dashboards/node-red-flows.json`
4. Click **Import**, then **Deploy** (red button, top right)

---

## Step 3 — Point the firmware at the same broker

In `main.ino`, change these four lines only:

```c
#define AIO_SERVER      "broker.hivemq.com"
#define AIO_SERVERPORT  1883
#define IO_USERNAME     "rprms"
#define IO_KEY          ""
```

Leave `ENABLE_EXTENDED_FEEDS 1`.

`Adafruit_MQTT` is a plain MQTT client — it is not tied to Adafruit's service — so
nothing else in the firmware changes.

> ### Pick your own topic prefix
> `broker.hivemq.com` is a **public** broker. Anyone can subscribe to `rprms/#` and see
> your data, and anyone publishing to `rprms/feeds/control.dosage` would drive your
> simulated infusion pump. Nothing here is real patient data, but change `rprms` to
> something unique (e.g. `rprms-hl-8842`) so you are not fighting another intern who
> copied the same default.
>
> If you change it, update the topics in the flow too — in the Node-RED editor:
> **☰ menu → Search** (or Ctrl+F), search `rprms/feeds/`, and edit each node. Faster:
> find-and-replace `rprms/feeds/` in `node-red-flows.json` **before** importing.

---

## Step 4 — Verify

1. Run the Wokwi simulation.
2. Serial monitor should stop printing `[NET] recovery attempt` and settle.
3. `system.state` on the Facility tab should read **ONLINE**.
4. Gauges start moving within one sampling period (default 15 s).

If the gauges stay empty, check in this order:

| Check | How |
|---|---|
| Broker connected? | Node-RED editor — the `mqtt in` nodes show a green "connected" dot |
| Topic prefix matches? | `IO_USERNAME` in `main.ino` must equal the prefix in the flow topics |
| Firmware actually publishing? | Serial monitor — no repeating `[NET] recovery attempt` |
| Right dashboard version? | Unknown-node errors on import mean dashboard v2 is installed, not v1 |

---

---

## Verified working

This dashboard was installed and run end-to-end, not just written:

| Check | Result |
|---|---|
| Node-RED starts with the flow loaded | 78 nodes, **0 errors**, `Started flows` |
| Dashboard served | `GET /ui/` → **HTTP 200** |
| Device → dashboard | All gauges, text widgets, charts and both alert logs populated from live MQTT |
| Dashboard → device | Button published `control.bed-preset = emergency`; toggles published `control.dosage-confirm = 1`, `control.bed-auto = 1` |
| Payload format | Matches what `applyCommand()` parses (`c.value >= 1.0f` for toggles, preset strings for buttons) |

Captured from the running dashboard during a deteriorating-patient run:

```
Heart Rate 165 bpm   SpO2 82 %   Body Temp 38.9 degC
Blood Pressure 151/91 mmHg       Clinical Status: Severe hypoxaemia
Delivered Dose 72 mg/hr          Actual Bed Angle 45 deg

Clinical Alert Log
  13:14:28 [CRITICAL] Severe hypoxaemia
  13:14:10 [CRITICAL] High body temperature
  13:14:07 [WARNING]  Mild fever
```

The bed angle moving to 45° on its own is Task 4's automatic mode; the WARNING →
CRITICAL escalation in the log is Task 2's alert feed.

### One bug this shook out

The first version of the flow set the theme node's `id` to `ui_base` — which is also its
`type`. Node-RED scans config-node properties for strings matching node ids, so it read
the `type` field as a self-reference and refused to start:

```
Error: Circular config node dependency detected: ui_base
```

Renamed to `ui_base_rprms`. `tools/gen_flows.py` now checks that no node id collides
with any node type.

---

## Step 5 — Screenshots

Everything in `docs/DASHBOARD_SETUP.md` Step 6 applies unchanged — same 12 screenshots,
just captured from `/ui` instead of Adafruit IO. Two Node-RED specifics:

- Use **two browser tabs**, one per dashboard tab, to shoot them side by side for the
  role-separation screenshot. The Facility tab contains **no** `medical.*` widget — that
  absence is the evidence.
- Node-RED's dark theme screenshots better than the light one for a report. Already set.

---

## Why this satisfies the brief

The portal names **no** dashboard platform. Searching the task text, instructions,
guidelines and page source turns up zero mentions of Adafruit IO, ThingSpeak, Blynk,
Node-RED or any other service. What Task 2 actually requires is:

| Task 2 requirement | How this flow meets it |
|---|---|
| "separate MQTT topics" | `medical.*` and `facility.*`, one tab each |
| "role-based access and monitoring" | Two tabs; on a broker with topic ACLs a `facility.#` credential cannot receive `medical.*` |
| "Gauge widgets with predefined safe thresholds" | 8 gauges with `seg1`/`seg2` colour bands at the clinical limits |
| "additional alert feeds for abnormal conditions" | Two scrolling alert logs, last 30 entries each |
| "periodic data aggregation" | `medical.summary` / `facility.summary`, published every 60 s |

The only platform the portal *does* mandate is Wokwi, for the project itself — and that
is unchanged.
