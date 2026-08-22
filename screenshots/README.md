# Screenshots

## Captured (7)

Taken from the live Node-RED dashboard with real MQTT traffic on `broker.hivemq.com`.

| File | Shows |
|---|---|
| `01-medical-dashboard-normal.png` | Medical Staff tab, patient stable — all gauges green |
| `02-medical-dashboard-critical.png` | **Key evidence.** HR 165 / SpO₂ 82 / temp 38.9 all red, status "Severe hypoxaemia", alert log populated, bed auto-moved to 45°, dose in the amber warning band |
| `03-facility-dashboard-normal.png` | Facility tab, environment nominal — **no `medical.*` widget anywhere** (that absence is the role separation) |
| `04-facility-dashboard-critical.png` | AQI/oxygen out of range, environmental alert log populated |
| `10-node-red-flow.png` | The 78-node flow in the editor |
| `11-architecture-diagram.png` | System architecture (PNG of the SVG) |
| `12-workflow-diagram.png` | Runtime workflow + state machines (PNG of the SVG) |

## Still needed — capture these yourself from Wokwi (5)

These require the Wokwi simulator visible on screen. Wokwi renders the circuit and
serial monitor to `<canvas>`, so they cannot be captured headlessly.

| File | How |
|---|---|
| `05-wokwi-circuit.png` | Full circuit view — all 24 parts |
| `06-oled-vitals.png` | OLED page 1 (temp / HR / SpO₂) |
| `07-oled-therapy.png` | OLED page 4 (dose + bed angle) — drag the dosage slider first so it reads mid-ramp |
| `08-oled-offline.png` | OLED showing `LOGGING OFFLINE` — trigger by breaking the network |
| `09-serial-monitor.png` | Serial showing `[BOOT] 9 FreeRTOS tasks started` and the `[NET]` backoff sequence |

**Project:** https://wokwi.com/projects/473060533558119425

## Reproducing the dashboard shots

Node-RED must be running (`start-dashboard.cmd`), then:

```bash
python tools/sim_esp32.py 150 normal
```

```bash
python tools/sim_esp32.py 150 critical
```

Capture from `http://127.0.0.1:1880/ui/#!/0` (Medical) and `#!/1` (Facility).
Note the tabs are **0-indexed** — `#!/1` is Facility, not Medical.

> Use the simulator to *build and check* the dashboard. For the final submission,
> capture with the Wokwi simulation actually running, so the data is coming from the
> firmware rather than a test harness.
