# Demo Video Script — 6 minutes

Shot-by-shot recording plan. Every one of the six tasks gets visible, on-camera proof.

**Setup before recording**
- Wokwi simulation running, OLED readable.
- Both Adafruit IO dashboards open in browser tabs.
- Serial monitor visible.
- Screen recorder at 1080p; enable microphone.

**Recommended layout** — Wokwi left half, dashboard right half, serial monitor as a
narrow strip along the bottom. All three visible at once means you never cut away from
the evidence.

---

## 0:00 – 0:30 · Introduction

> "Hi, I'm Hrishikesh Lokhande. This is my ElevanceSkills internship project — a Remote
> Patient Risk Monitoring System built on ESP32 with FreeRTOS. All six internship tasks
> are implemented in one integrated project. It monitors six patient parameters and four
> room parameters concurrently, scores clinical risk in real time, lets doctors remotely
> control medication and bed position, adapts its own sampling rate to patient condition,
> and keeps logging without data loss when the network drops."

**On screen** Wokwi circuit, full view.

---

## 0:30 – 1:20 · Task 1 — FreeRTOS concurrency

**Show** Serial monitor, then OLED cycling pages.

> "Nine FreeRTOS tasks run concurrently across both ESP32 cores. Alarm handling is the
> highest priority on core 1 — the core that never blocks on the network. MQTT and link
> supervision are on core 0. That split matters: in the original single-task design, a
> stalled broker froze the alarms."

**Point at** Serial line `[BOOT] 9 FreeRTOS tasks started`.

> "They share data through five queues, three mutexes and a binary semaphore. Every
> shared field lives in one struct behind one mutex, so a dashboard payload can never mix
> a heart rate from one sample with an SpO₂ from the next. Only one lock nesting exists
> in the whole system — MQTT then state — so deadlock is structurally impossible."

**Show** OLED pages 1 and 2.

> "Page one is the original vitals. Page two is the expansion — blood pressure and ECG,
> new for this task. A 20-second task watchdog supervises all nine tasks."

---

## 1:20 – 2:10 · Task 2 — Role-based dashboards

**Show** Both dashboards side by side.

> "Data is split into two dashboards on separate MQTT topic groups. Medical staff get
> `medical.*` — heart rate, SpO₂, body temperature, blood pressure, ECG. Facility
> management get `facility.*` — room temperature, oxygen, air quality."

**Point at** the Facility dashboard.

> "There is no patient data on this dashboard at all. That's not a hidden widget — the
> two roles subscribe to different topic prefixes, so on a broker with topic ACLs a
> facilities credential is cryptographically unable to receive patient vitals."

**Turn the AQI potentiometer up past 300.**

> "Gauges have clinical thresholds. Watch the AQI gauge go amber at 150, red at 300 — and
> the alert stream logs it."

**Show** `medical.summary`.

> "Every 60 seconds the device publishes an aggregate — average, min and max. At
> five-second sampling one bed generates 17,000 messages a day; a 200-bed ward generates
> three and a half million. Aggregating on-device is what makes this scale."

---

## 2:10 – 3:10 · Task 3 — Medication dosage

**Show** OLED page 4 and the dosage slider.

> "Doctors set medication dosage from a 0-to-100 milligram-per-hour slider. Watch what
> happens when I drag it straight to 60."

**Drag slider 0 → 60.**

> "The delivered dose ramps at five milligrams per hour per second — twelve seconds to
> get there. The slider value is never the infused dose. A rate limiter sits between
> them, so an accidental slider fling can't produce a step change in an infusion pump."

**Drag slider to 95.**

> "Now above 80, the critical threshold. The red LED blinks rapidly, the buzzer sounds —
> and look at the OLED: 'CONFIRM 95'. The system pins the dose at 80 and waits."

**Wait 15 seconds without confirming.**

> "I'm not going to confirm. The request expires and the dose stays at 80. That's
> deliberate — the failure mode of doing nothing is the *safe* one. There's no path from
> silence to an overdose."

**Set to 95 again, toggle confirm.**

> "Confirm within the window and it releases — still ramped, never a jump."

---

## 3:10 – 3:50 · Task 4 — Bed elevation

**Show** Wokwi servo close-up, then the bed slider.

> "Bed angle, zero to ninety degrees, on a servo."

**Drag slider 0 → 90.**

> "Smooth — one degree every 25 milliseconds, about 2.3 seconds of travel. Not a snap.
> Patient comfort and safety."

**Publish `emergency` to `control.bed-preset`.**

> "Three presets: sleeping at 10 degrees, breathing support at 45, emergency at 90.
> Emergency ramps faster — three degrees per step — because someone who can't breathe
> needs the bed up now. But it still ramps."

**Toggle auto mode on, let SpO₂ drop.**

> "In automatic mode the bed follows patient condition. SpO₂ below 90 and it goes to 45
> degrees for breathing support. Below 85 with critical risk, full upright at 90."

**Drag the slider manually.**

> "And the moment I touch the slider, auto mode switches off. Manual overrides automatic;
> automatic never overrides manual. That's standard medical device practice — a
> clinician's explicit instruction is never silently reversed."

---

## 3:50 – 4:30 · Task 5 — Adaptive sampling

**Show** OLED page 5, `Rate:` line.

> "Sampling interval is adjustable from 5 to 60 seconds. Right now it's in automatic mode
> and the patient is stable, so it's at 30 seconds — conserving battery."

**Raise the DS18B20 to 39 °C.**

> "Patient deteriorates — and it drops straight to 5 seconds. High-frequency monitoring
> when it matters."

**Return the temperature to normal.**

> "Recovery takes three consecutive normal cycles before it slows back down. That
> hysteresis stops a patient hovering at a threshold from flapping the rate every second."

**Move the sampling slider to 45.**

> "Manual override — and notice it takes effect immediately. The sensor task might be
> halfway through a 30-second sleep, but it's blocked on a queue, not a plain delay. A
> token wakes it in milliseconds. That same helper feeds the watchdog in one-second
> slices, so one piece of code solves both the watchdog requirement and the
> responsiveness requirement."

---

## 4:30 – 5:40 · Task 6 — Offline fault tolerance

**Break the network connection.**

> "Now the important one. I'm killing the network."

**Show** the OLED banner.

> "Within two seconds: 'LOGGING OFFLINE'. It names the cause — Wi-Fi or MQTT — shows how
> many samples are buffered, and counts down to the next retry."

**Point at** the buzzer / let it sound.

> "The buzzer patterns are distinct per failure type. Three short beeps means Wi-Fi is
> gone. Two long beeps means the broker is gone. One chirp means a sensor is
> disconnected. A technician can diagnose the failure class from the corridor without
> looking at a screen."

**Show** the buffered counter incrementing.

> "Readings aren't lost — they go into a 120-record ring buffer, about 30 minutes of
> data. When it fills, the oldest record is dropped, because at handover the recent data
> is what matters."

**Show** the retry countdown.

> "Retries back off exponentially — 1, 2, 4, 8, 16, 32, capped at 60 seconds. In a ward
> where 200 beds all reconnect after a switch reboot, backoff is what stops the recovery
> becoming the outage."

**Restore the connection.**

> "And restoring it..."

**Show** serial `[NET] restored -- replaying n buffered samples`, then `system.backlog`.

> "It replays every buffered record in order. If a publish fails mid-replay the record
> goes back to the *front* of the queue, so an interrupted resync never scrambles the
> timeline. Zero data loss."

**Open a slide switch.**

> "One more state worth showing. If I disconnect a sensor — the link is fine, but the
> system goes DEGRADED and reports 'Sensor disconnected' instead of 'Patient stable'. A
> monitor that reports a healthy patient from an unplugged probe is worse than one that
> reports nothing."

---

## 5:40 – 6:00 · Close

> "All six tasks in one integrated project. The work that mattered most wasn't adding
> features — it was the concurrency structure. Alarms at the highest priority on a core
> that never blocks on the network. A single global lock order so deadlock can't happen.
> And every actuator command routed through one rate-limited path, so no user interaction
> can produce an abrupt physical change.
>
> Source, circuit and full report are in the GitHub repo linked below. Thanks for
> watching."

**On screen** GitHub URL and Wokwi project URL.

---

## Recording tips

- **Do a silent rehearsal first.** The dosage ramp and the backoff countdown are both
  real-time; know how long you're waiting so the pauses don't feel dead.
- **Don't cut during a ramp.** The whole point of Tasks 3 and 4 is that the change is
  gradual — an edit destroys the evidence.
- **Zoom the OLED** when reading it aloud. It's 128×64; at 1080p full-frame it's
  unreadable.
- **Trigger the offline test last** — it's the longest segment and the most impressive.
- If you overrun, trim the Task 1 narration, not the Task 6 demonstration.
