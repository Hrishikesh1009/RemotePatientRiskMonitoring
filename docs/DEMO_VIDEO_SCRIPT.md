# Demo Video Script — about 7 minutes

The portal asks the video to show: **project overview, implementation, simulation,
verification, functional explanation, working demonstration** — clear, structured,
professional. This script covers each in that order. Speak in your own words; the quoted
lines are a guide, not something to read out. **You must record this yourself** — it is
your explanation of your own project.

## Before recording

- Wokwi project open and **running** (https://wokwi.com/projects/473060533558119425);
  zoom the browser so the OLED is readable. Serial monitor visible.
- Node-RED running (`tools/start-dashboard.cmd`) with both dashboards open in two tabs:
  `http://127.0.0.1:1880/ui/#!/0` (Medical Staff) and `#!/1` (Facility Management).
- A way to publish MQTT (the dashboard sliders do it; or a web MQTT client open on
  `rprms-hl-8842/feeds/#`).
- Screen recorder at 1080p, microphone on. Recommended layout: Wokwi left half, dashboard
  right half, serial strip along the bottom, so you never cut away from the evidence.
- Do a silent rehearsal first — the ramps and the backoff countdown run in real time.

> **Switch layout** (top row of the circuit, left → right): **Temp · HR · SpO₂ · PIR ·
> Wi-Fi cut · Broker cut**. Sliding a switch to the right applies the fault.

---

## 0:00 – 0:40 · Overview  *(project overview)*

> "I'm Hrishikesh Lokhande. This is my ElevanceSkills internship project: a Remote Patient
> Risk Monitoring System on an ESP32 with FreeRTOS. It monitors six patient parameters and
> four room parameters, scores clinical risk, publishes to two role-separated dashboards,
> lets doctors control medication dosage and bed elevation remotely, adapts its own
> sampling rate, and keeps logging when the network fails. All six internship tasks are
> implemented in one integrated project."

**On screen** the Wokwi circuit, then `architecture-diagram.svg` for a few seconds.

## 0:40 – 1:50 · Implementation  *(implementation / functional explanation)*

**Show** the circuit, then `main.ino` scrolled to the task table / `setup()`.

> "The circuit has a DS18B20 for body temperature, a DHT22 for the room, a PIR, three
> potentiometers for ECG, oxygen and air quality, an OLED, a servo for the bed, three LEDs,
> four buzzers, and six slide switches — four simulate a probe coming off, two are for
> fault injection that I'll use in Task 6. Heart rate and SpO₂ are simulated because Wokwi
> has no pulse-oximeter, and I'll say so honestly in the report."

> "The firmware is ten FreeRTOS tasks across both cores. Alarms are the highest priority
> and sit on core 1, the core that never blocks on the network; MQTT and link supervision
> are on core 0. They share data through queues, three mutexes and a binary semaphore, and
> a 20-second task watchdog supervises all ten. Blood pressure and ECG each have their own
> task, as Task 1 asks."

**Point at** serial: `[BOOT] 10 FreeRTOS tasks started`, `task watchdog armed: 20 s`.

## 1:50 – 2:30 · Task 2 — role-based dashboards  *(simulation)*

**Show** both dashboards side by side.

> "Data is published to separate topic groups — `medical.*` and `facility.*`. The facility
> dashboard has no patient data on it at all. To be precise about what that means: this is
> topic separation, the precondition for role-based access. Real enforcement needs ACLs on
> the broker, which this project doesn't configure, and I say so in the report."

**Drag the AQI potentiometer** past the red band.

> "Gauges have predefined thresholds — AQI goes amber at 150 and red at 300, and the alert
> feed logs it. Every minute the device also publishes an aggregate — average, minimum and
> maximum — so a ward of beds doesn't flood the cloud."

## 2:30 – 3:30 · Task 3 — medication dosage  *(working demonstration)*

**Show** the OLED footer and the dosage slider.

> "The dosage slider is 0 to 100 milligrams per hour. Watch the OLED footer — the dose is on
> every screen. I'll drag to 60."

**Slider → 60.**

> "It ramps at five milligrams per hour per second — twelve seconds. A slider fling can
> never become a step change in an infusion."

**Slider → 95.**

> "Above 80 is critical. The system holds the dose at 80, the OLED says CONFIRM 95, the red
> LED blinks rapidly and the buzzer sounds. If nobody confirms, after fifteen seconds it
> *stays* at 80 — the failure mode of doing nothing is the safe one."

**Set 95 again and press CONFIRM within the window.**

> "Confirm in time and it releases — still ramped, never a jump."

## 3:30 – 4:10 · Task 4 — bed elevation

**Show** the servo close-up and the bed slider.

> "Bed angle zero to ninety degrees on a servo — one degree every 25 milliseconds, about
> two and a half seconds end to end. Three presets: sleeping 10°, breathing support 45°,
> emergency 90°, which ramps faster but is still a ramp."

**Press Emergency; then toggle Auto bed on and set the simulated SpO₂ slider to 88, then 82, then 0.**

> "In automatic mode the bed follows the patient: SpO₂ under 90 goes to 45°, under 85 and
> critical goes to 90°, back to normal returns to 10°. And the moment I touch the manual
> slider, automatic mode switches off — a clinician's instruction is never silently
> reversed."

## 4:10 – 4:50 · Task 5 — adaptive sampling

**Show** the serial `[SAMPLE]` / `[RATE]` lines and OLED SYSTEM page.

> "The sampling interval is 5 to 60 seconds. In automatic mode a stable patient settles at
> 30 seconds to save power."

**Set the simulated heart rate to 130.**

> "An abnormal patient drops it straight to five seconds."

**Set it back to 0.**

> "And it only returns to 30 after three consecutive stable samples, so a patient hovering
> at a threshold can't make the rate flap. The delay is built from `vTaskDelay` slices that
> poll a queue, so a rate change wakes a sleeping task within a fraction of a second."

## 4:50 – 6:00 · Task 6 — offline fault tolerance

**Slide the Wi-Fi cut switch right.**

> "Now the important one — I'm cutting the Wi-Fi."

**Show** the OLED banner.

> "Within two seconds: LOGGING OFFLINE, the cause, how many samples are buffered, and a
> live countdown to the next retry. Three short beeps means Wi-Fi; two long beeps means the
> broker — I'll show that next. The system keeps sampling into a 120-record buffer."

**Let it run ~30 s so the retry counts 1, 2, 4, 8, 16.** Then **slide the switch back.**

> "On restore it re-associates, reconnects to the broker, and replays every buffered record
> in order to the backlog feed. Records out equal records in."

**Show** `system.backlog` filling, and serial `[NET] restored -- replaying N …` / `resynced N records`.

**Slide Broker cut right, then back; then slide the HR probe switch right.**

> "A broker failure shows cause MQTT. And a disconnected probe gives DEGRADED, not OFFLINE —
> the link is fine but the data can't be trusted, so it never reports 'Patient stable'
> from an unplugged lead, and it notices within about three seconds."

## 6:00 – 6:40 · Verification  *(verification / "waveforms")*

**Show** `docs/TEST_RESULTS.md` and one `docs/evidence/` log.

> "I verified this in the simulator by measurement, not by eye. Sixteen test cases, run as
> forty-nine automated checks that all pass, using serial timestamps, the OLED framebuffer,
> LED, buzzer and servo state, and an independent MQTT subscriber. For example: the dose
> ramps at exactly 5.00 milligrams per hour per second; sampling is 5.08 seconds apart at a
> 5-second setting; an outage with eight buffered samples replayed all eight, in order. The
> bug log lists every defect I found — including a watchdog reset caused by a stalled
> network — and how I fixed it, and the report is honest about what is not exercised."

## 6:40 – 7:00 · Close

> "All six tasks in one integrated project. The work that mattered most wasn't adding
> features — it was the structure: alarms that can't be blocked by the network, one lock
> order so deadlock can't form, every actuator command through one rate-limited path, and
> a recovery path I tested by breaking the real link. Source, circuit, report and logs are
> in the GitHub repository linked below. Thanks for watching."

**On screen** the GitHub URL and the Wokwi URL.

---

## Recording tips

- **Don't cut during a ramp** — the whole point of Tasks 3 and 4 is that the change is gradual.
- **Zoom the OLED** when you read it aloud; at 1080p full-frame it's unreadable.
- Do the **offline demo last** — it's the longest segment. If you overrun, trim the Task 1
  narration, not Task 6.
- Say plainly what is simulated (HR, SpO₂, BP) and what is not enforced (role-based access).
  Evaluators reward accuracy over polish.
- Upload with link sharing set to *Anyone with the link* and test it in a logged-out window.
