# tools/

Helper scripts. Neither is required to build or run the project — they exist to make
the dashboard verifiable and screenshottable without the Wokwi simulation attached.

---

## `sim_esp32.py` — publish the ESP32's MQTT traffic from your PC

Speaks exactly the topics `main.ino` publishes, so the Node-RED dashboard lights up
without Wokwi running. Useful for building/adjusting the dashboard, and for capturing
the critical-state screenshots on demand instead of waiting for the simulated patient
to deteriorate.

```bash
pip install paho-mqtt
```

```bash
python tools/sim_esp32.py 120 normal
```

```bash
python tools/sim_esp32.py 120 critical
```

`normal` holds the patient in a stable band. `critical` deteriorates them over ~15 s —
heart rate to 165, SpO₂ to 82, temperature to 38.9 °C, AQI past 300 — which drives the
gauges into their red bands and fills both alert logs. That is the screenshot you want
for `02-medical-dashboard-critical.png`.

It also subscribes to `control.*` and prints anything the dashboard sends, so you can
confirm the sliders and buttons are publishing.

> **This is a test harness, not part of the deliverable.** The real data path is
> ESP32 → MQTT → dashboard. Do not present simulator screenshots as Wokwi output — use
> it to build and check the dashboard, then capture your final screenshots with the
> Wokwi simulation actually running.

---

## `gen_flows.py` — regenerate the Node-RED dashboard

Emits `dashboards/node-red-flows.json` (78 nodes). Edit the `GAUGES` / `TEXTS` /
`SLIDERS` / `SWITCHES` tables at the bottom and re-run, rather than hand-editing 1,700
lines of JSON.

```bash
python tools/gen_flows.py
```

Change `PREFIX` at the top if you change `IO_USERNAME` in `main.ino` — the two must
match or no data flows.

It self-validates on exit: unique node ids, no dangling wire/group/tab references.

> **One trap worth knowing.** A node's `id` must never equal any node's `type`.
> Node-RED scans config-node properties for strings matching node ids, so a node with
> `id: "ui_base"` and `type: "ui_base"` looks like a self-reference and the runtime
> aborts with `Circular config node dependency detected`. The generator now uses
> `ui_base_rprms`. This cost a debugging cycle; the validator checks for it.
