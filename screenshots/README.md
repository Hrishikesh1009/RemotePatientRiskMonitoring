# Screenshots

Place the 12 required screenshots here before submitting. Filenames and required content
are specified in [`../docs/DASHBOARD_SETUP.md`](../docs/DASHBOARD_SETUP.md) Step 6.

| Filename | Content |
|----------|---------|
| `01-medical-dashboard-normal.png` | All medical gauges green |
| `02-medical-dashboard-critical.png` | HR or SpO₂ red + alert stream populated |
| `03-facility-dashboard-normal.png` | All facility gauges green |
| `04-facility-dashboard-alert.png` | AQI past 300, alert stream populated |
| `05-dosage-slider-ramp.png` | Slider at 100, delivered-dose gauge mid-ramp |
| `06-dosage-critical-confirm.png` | OLED `>CONFIRM`, red LED lit |
| `07-bed-control.png` | Bed slider + readback gauge + servo horn |
| `08-sampling-adaptive.png` | Sampling rate at 5 s during an abnormal episode |
| `09-offline-banner.png` | OLED `LOGGING OFFLINE` + buffered count |
| `10-backlog-resync.png` | `system.backlog` filling after reconnect |
| `11-wokwi-circuit.png` | Full circuit view |
| `12-serial-monitor.png` | `[BOOT] 9 FreeRTOS tasks started` + `[NET]` lines |

The two that carry the most evidential weight are **05** (proves the dosage rate limiter
exists) and **09/10** (prove the offline buffer and resync work). Don't skip those.
