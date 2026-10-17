# Screenshots

Twelve required evidence shots and three bonus shots, all taken on **13 October 2026** against the
**final firmware** (`main.ino` SHA-256 `9e69da71…`, see `docs/evidence/tested_files_sha256.txt`)
running in the Wokwi simulator, with the Node-RED dashboards (`dashboards/node-red-flows.json`,
84 nodes) subscribed to the live public broker. They were captured by the verification tooling
(headless Chrome), not edited by hand. Three honest notes apply to them:

* `10-backlog-resync.png` was **retaken on 17 October 2026**, with the same firmware: the Facility
  "Last Resynced Record" card used to print the whole replayed record (heart rate, SpO₂,
  temperature), which contradicts the role separation, so the flow now shows only the time of the
  record (`tools/gen_flows.py`, `compact backlog` node). All other shots are from 13 October.
* The Wokwi page shows **SIGN IN** in the corner because the rig opens the public project in an
  anonymous session and types the repository files into it; nothing is saved to the Wokwi account.
* The simulated patient has rare random episodes (a heart rate below 60 or above 120 raises a
  warning). To keep the "normal" frames calm, `01` was taken with the heart rate / SpO₂ **pinned
  through the dashboard's own "Patient Simulation (demo aid)" sliders** — they are visible in the
  frame (75 bpm, 97 %). `02` and `05` use the same sliders for the critical case.

| File | Task | What it shows |
|---|---|---|
| `01-medical-dashboard-normal.png` | 2 | Medical Staff tab, patient stable: gauges green, filled trend chart, 60 s aggregate (`n=2 HR 69 …`), empty alert log, Auto-bed and Adaptive-sampling switches matching the device |
| `02-medical-dashboard-critical.png` | 2, 3, 4 | HR 165 / SpO₂ 82 / body temperature 38.88 °C all red, `CRITICAL` alert log, delivered dose 70 mg/hr (amber band), bed moved automatically to 90° |
| `03-facility-dashboard-normal.png` | 2 | Facility Management tab, environment nominal — **no `medical.*` widget anywhere** (topic-level role separation) |
| `04-facility-dashboard-alert.png` | 2 | AQI 317 (red) after turning the AQI potentiometer; alert log `[CRITICAL] Air quality hazardous` (the earlier `Oxygen level unsafe` entries are from the oxygen potentiometer) |
| `05-dosage-slider-ramp.png` | 3 | The dashboard slider at 70 mg/hr while the **delivered dose is mid-ramp at 50** (limit 5 mg/hr per second) |
| `06-dosage-critical-confirm.png` | 3 | Circuit close-up: OLED THERAPY page `>CONFIRM 95 mg/hr`, footer `CONF`, delivered dose 11.3 and still rising, **red LED lit** (the glow is real: frames with the LED off were discarded) |
| `07-bed-control.png` | 4 | **Composite** of real captures: Wokwi pane (servo horn at 60°, serial `[BED]` 1°/25 ms ramp) beside the dashboard's Bed Angle slider and Actual Bed Angle gauge (60) |
| `08-sampling-adaptive.png` | 5 | **Composite**: serial `[RATE] auto-adapt: 30 s -> 5 s` on a heart rate of 53, back to `5 s -> 30 s` after stable samples, beside the Facility "Sampling Interval" and the Adaptive Sampling switch |
| `09-offline-banner.png` | 6 | Wi-Fi cut in the circuit: `*** LINK FAILURE ***`, `LOGGING OFFLINE`, `Cause: WIFI`, `Buffered: 9`, `Retry in 25s`, dose footer still shown, **amber LED lit** |
| `10-backlog-resync.png` | 6 | **Composite**: serial `[NET] restored -- replaying 10 buffered samples` / `resynced 10 records` beside the Facility System Health card (ONLINE, `Last Resynced Record` = only the time, `t=254 s (record replayed)` — the card shows no patient values) |
| `11-wokwi-circuit.png` | 1 | The complete circuit — all 26 parts, six slide switches along the top |
| `12-serial-monitor.png` | 1 | Boot log from the top: banner, `[BOOT] 10 FreeRTOS tasks started`, `[BOOT] task watchdog armed: 20 s timeout`, `[NET]` OFFLINE → ONLINE and the first resync |
| `13-bonus-node-red-flow.png` | 2 | The 84-node dashboard flow in the Node-RED editor (including the three "compact …" formatter nodes) |
| `14-bonus-architecture-diagram.png` | — | `docs/architecture-diagram.svg` rendered to PNG |
| `15-bonus-workflow-diagram.png` | — | `docs/workflow-diagram.svg` rendered to PNG |

**How the special ones were made.** `07`, `08` and `10` paste captured pieces side by side under
caption bars — nothing in them is redrawn. In `12` the serial pane was enlarged with CSS for the
capture so the whole boot log fits; its content is untouched. `06` and `09` are crops of the Wokwi
canvas at 4× scale.

**Reproduce.** Start the simulation with `tools/verification/` (`node simserver.js`), the dashboard
with `tools/start-dashboard.cmd`, open `http://127.0.0.1:1880/ui/#!/0` (Medical) and `#!/1`
(Facility) — the tabs are 0-indexed. The dashboard-capture helper used here is not part of the
repository; the scenarios are the ones in `docs/TEST_PLAN.md` (TC-07, TC-08, TC-09, TC-11, TC-12).
