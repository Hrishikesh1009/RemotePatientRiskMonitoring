# Demo Video — script and timeline

**Video:** https://drive.google.com/file/d/1z_txnAd4YhKfVJfZGMZbQZoqUU3RGK9w/view?usp=sharing — MP4, 1080p, **4 min 57 s** (the portal asks for 2–5 minutes).
The same file is uploaded in the portal's submission form. It is narrated by the intern, in his own voice.

The portal asks the video to show: **project overview, implementation, simulation, verification,
functional explanation, working demonstration** — clear, structured, professional. The video follows
that order: the circuit and the firmware design first, then each of the six tasks demonstrated on the
running simulation with the dashboards beside it, then the verification results and the key design
decisions.

## Timeline

| Time | Section | What is on screen |
|---|---|---|
| 0:00 | Overview and circuit | title card, then the Wokwi circuit; its parts are labelled one by one as they are named |
| 0:24 | Task 1 — architecture | architecture diagram with the Task 1 requirement checklist |
| 0:41 | Task 1 — firmware | `main.ino`: the task table, `setup()` starting the ten tasks, the boot log; a card lists queues, mutexes, semaphore and watchdog |
| 0:55 | Task 2 — two dashboards | Medical and Facility dashboards side by side, with their MQTT topic groups |
| 1:12 | Task 2 — gauges and alerts | the air-quality knob is turned past 150: the AQI gauge turns amber and a warning appears in the alert feed |
| 1:23 | Task 2 — critical alerts | heart rate set to 130 and air quality past 300: both gauges turn red with their own alerts |
| 1:37 | Task 2 — aggregation | the 60-second average / minimum / maximum on both dashboards |
| 1:44 | Task 3 — medication dosage | dose slider to 60, then 95 (held at 80, `CONF`, red LED, buzzer); not confirmed, then confirmed |
| 2:14 | Task 4 — bed elevation | bed slider, presets 45° / 10° / 90°, automatic mode from SpO₂ 88 and 82, then the manual override |
| 2:50 | Task 5 — adaptive sampling | sampling slider 10 s then 5 s, adaptive mode, heart rate 130, back to normal |
| 3:20 | Task 5 — implementation | `interruptibleDelay()`: `vTaskDelay` slices and the wake queue |
| 3:32 | Task 6 — link states, Wi-Fi cut | ONLINE / DEGRADED / OFFLINE, the Wi-Fi switch cut, `LOGGING OFFLINE`, retry backoff |
| 3:47 | Task 6 — restore and replay | link restored, the 10 buffered records replayed in order |
| 3:57 | Task 6 — broker failure | broker cut (cause MQTT), restored, 5 records replayed |
| 4:09 | Task 6 — degraded, not offline | heart-rate probe disconnected: DEGRADED, then reconnected |
| 4:22 | Verification | `docs/TEST_RESULTS.md`, then the dose and bed-angle traces plotted from the serial log |
| 4:38 | Key design decisions | the key decisions one by one, then the GitHub and Wokwi links |

## How the footage was made

The circuit, both dashboards, the OLED and the serial text come from real runs of the firmware in
Wokwi with Node-RED. The recording PC runs the simulation slower than real time, so the video uses
*simulated* time: the device is shown at its real speed, waiting is removed by plain cuts, and each
cut is marked on screen. Nothing is sped up. The mouse pointer, click rings, captions, the zoomed OLED,
the enlarged alert feeds and the dose / bed-angle plots are drawn from the recorded data. Three
one-second still frames (after Task 4, after Task 6 and at the end) give the narration time to finish.

Heart rate, SpO₂ and blood pressure are simulated, because Wokwi has no such sensors, and role
separation is by MQTT topic, not by broker ACLs; the narration says both.

## Narration script

The text the narration follows, section by section (the times above are where each section starts).

**Overview and circuit.** Hi, I'm Hrishikesh Lokhande. This is my ElevanceSkills internship project:
remote patient risk monitoring. An ESP32 running FreeRTOS scores the patient's risk and feeds two
dashboards. Everything's labelled on screen: sensors, pots, OLED, bed servo, LEDs, buzzers, fault
switches. Heart rate, SpO two and blood pressure are simulated; Wokwi has no such parts.

**Task 1.** Task one is multi-sensor expansion: ten FreeRTOS tasks, scheduled by priority, on both
cores. Blood pressure and ECG each get their own task. Alarms run on core one at top priority; the
network stays on core zero. Queues, a semaphore and mutexes share data; the task table sets every
priority and core; setup() starts all ten tasks; and a twenty-second watchdog supervises them.

**Task 2.** Task two: role-based dashboards. Medical staff see heart rate, SpO two, body temperature,
blood pressure and ECG. Facility staff get room temperature, oxygen and air quality. Separate MQTT
topic groups; real access control would need broker ACLs. Gauges have preset thresholds. I turn the
air-quality knob past one fifty: the gauge goes amber and a warning gets logged. Medical side: I pin
heart rate at one thirty. Meanwhile air quality crosses three hundred: red, critical. Then the
heart-rate gauge goes red, with an alert. Every minute each side publishes aggregates: average,
minimum and maximum. Summaries, not raw samples.

**Task 3.** Task three: dose slider, live on the OLED. It ramps at five milligrams per hour per second.
Normal below sixty, warning from sixty, critical above eighty. Ninety-five: held at eighty, CONF, red
LED blinking, buzzer. If nobody confirms within fifteen seconds, it just stays at eighty. This time I
press CONFIRM in time: released, still ramped. Above eighty, the OLED footer reads CRIT.

**Task 4.** Task four: the slider moves the servo one degree every twenty-five milliseconds. Presets:
breathing support at forty-five, sleeping at ten, emergency at ninety: a faster ramp, still a ramp. In
auto mode the bed follows the patient: normal is ten. Simulated SpO two eighty-eight: forty-five.
Eighty-two, critical: ninety. I move the slider and manual wins: a clinician's instruction never gets
silently reversed.

**Task 5.** Task five: the slider sets the sampling interval, five to sixty seconds. Ten here. Then
five. Any manual change switches adaptive mode off. Now adaptive mode: a stable patient settles at
thirty seconds, which saves power. I pin heart rate at one thirty: straight to five seconds. I release
it and it only goes back to thirty after three stable samples, so it doesn't flap. Under the hood it's
vTaskDelay in short slices, polling its own queue without blocking: a new rate applies within a
hundred milliseconds.

**Task 6.** Task six: three link states – ONLINE, DEGRADED and OFFLINE. Wi-Fi cut: OFFLINE, LOGGING
OFFLINE, three short beeps. It buffers readings locally and retries with exponential backoff: one,
two, four, eight, sixteen seconds. I switch back and it reconnects, then replays the buffer in order:
ten in, ten out, nothing lost. Now I cut the broker: OFFLINE, cause MQTT, two long beeps. I restore it
and the five buffered records replay the same way. A disconnected heart-rate probe: the link is still
fine, so DEGRADED, not OFFLINE. The data can't be trusted, so it never says patient stable.
Reconnected: back ONLINE.

**Verification and close.** Tested by measurement in the simulator: sixteen test cases, forty-nine
automated checks, all passing. Traces from the serial log show five milligrams per hour per second and
one degree every twenty-five milliseconds. Key decisions: alarms get their own core; one lock order, so
no deadlock; one rate limiter for every actuator; safe by default; manual overrides automatic; offline:
buffer, replay in order. Code on GitHub, circuit on Wokwi. Thanks for watching.
