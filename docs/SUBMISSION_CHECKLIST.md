# Submission Checklist

## ⚠️ Read this first

The portal states: **"One-time submission only: Please ensure all your work is complete
before submitting. You won't be able to make changes after submission."**

Do not press Submit until every box below is ticked.

**Deadline:** 26 October 2026
**Submission form asks for:** a Google Drive folder link containing PDFs and links, plus
a project report field.
**The instructions modal additionally requires:** GitHub repo, Wokwi link, project
report, architecture diagram, workflow diagram, dashboard screenshots, and demo video.

---

## Phase 0 — Portal housekeeping (do this now, not at the end)

These come from the **Internship guidelines and policies** panel, not the task list, and
they affect your evaluation independently of the code.

- [ ] **Log daily reports.** Policy: *"Log a short work report for each active project…
      Consistent reporting reflects professionalism."* Use **Daily report → Log today's
      update** on the project card. This is a running obligation — a submission with an
      empty report history reads badly regardless of code quality. Start today.
- [ ] **Mark all 6 tasks complete** in **Tasks → 6 assigned · open checklist** once each
      feature is verified in the simulator. The dashboard currently shows **0/6**.
- [ ] Confirm the counter reads **6/6** before submitting.

> **Stipend is a cliff, not a slope:** 6 of 6 tasks → ₹3,000. Exactly 5 → ₹500. Fewer
> than 5 → ₹0. Everyone receives a certificate either way. There is no partial credit
> between 5 and 6, so the sixth task is worth ₹2,500 on its own.

> **Rubric note:** *"All tasks must be fully integrated to meet the 100% completion
> requirement."* This is why everything lives in one `main.ino` rather than six sketches.

> **Originality note:** *"Maintain code quality, originality, and best practices."* Be
> ready to explain any part of the design in your own words — the mentors may ask. The
> rationale for every non-obvious decision is in `ARCHITECTURE.md`.

---

## Phase 1 — Code and simulation

- [ ] `main.ino` pasted into Wokwi; simulation runs without error
- [ ] `diagram.json` pasted; all 24 parts present, no floating wires
- [ ] All 9 libraries from `libraries.txt` added
- [ ] `IO_USERNAME` / `IO_KEY` replaced with real Adafruit IO credentials
- [ ] Serial prints `[BOOT] 9 FreeRTOS tasks started`
- [ ] OLED cycles all five pages
- [ ] Ran 10 minutes with no watchdog reset and stable free heap
- [ ] Wokwi project **saved and set to public** — copy the share URL

**Wokwi URL:** `_______________________________________________`

---

## Phase 2 — Dashboards

- [ ] Adafruit IO feeds created (see `DASHBOARD_SETUP.md` for the free-tier decision)
- [ ] Medical Staff Dashboard built — 3 gauges + text + alert stream + chart + controls
- [ ] Facility Management Dashboard built — 3 gauges + text + alert stream + chart
- [ ] **Verified: no `medical.*` block appears on the Facility dashboard**
- [ ] Sliders confirmed working end-to-end (drag → OLED changes)
- [ ] Aggregation JSON appearing on both `summary` feeds

---

## Phase 3 — Testing

Work through `TEST_PLAN.md`:

- [ ] TC-01 Boot and task creation
- [ ] TC-02 Alarms unblocked during network stall
- [ ] TC-03 Sensor disconnect → DEGRADED
- [ ] TC-04 Role separation
- [ ] TC-05 Gauge thresholds
- [ ] TC-06 Periodic aggregation
- [ ] TC-07 Dosage rate limiting
- [ ] TC-08 Dosage critical confirmation (both timeout and confirm paths)
- [ ] TC-09 Bed smooth transition
- [ ] TC-10 Automatic bed adjustment
- [ ] TC-11 Adaptive sampling
- [ ] TC-12 Offline detection, buffering and resync

---

## Phase 4 — Screenshots

All 12 into `screenshots/` (filenames listed in `DASHBOARD_SETUP.md` Step 6):

- [ ] 01 Medical dashboard, normal
- [ ] 02 Medical dashboard, critical + alert stream
- [ ] 03 Facility dashboard, normal
- [ ] 04 Facility dashboard, AQI alert
- [ ] 05 Dosage slider mid-ramp
- [ ] 06 OLED `>CONFIRM` + red LED
- [ ] 07 Bed control + servo horn
- [ ] 08 Adaptive sampling at 5 s
- [ ] 09 OLED `LOGGING OFFLINE`
- [ ] 10 `system.backlog` resync
- [ ] 11 Full Wokwi circuit
- [ ] 12 Serial monitor boot lines

---

## Phase 5 — Demo video

- [ ] Recorded following `DEMO_VIDEO_SCRIPT.md` (~6 minutes)
- [ ] All six tasks visibly demonstrated
- [ ] Audio clear; OLED readable when referenced
- [ ] Uploaded to Google Drive (or YouTube unlisted) with link sharing **on**

**Video URL:** `_______________________________________________`

---

## Phase 6 — GitHub repository

- [ ] Public repo created, e.g. `remote-patient-risk-monitoring-system`
- [ ] Pushed: `main.ino`, `diagram.json`, `libraries.txt`, `wokwi.toml`, `README.md`,
      `docs/`, `dashboards/`, `screenshots/`
- [ ] README renders correctly on GitHub
- [ ] Both SVG diagrams display in the browser
- [ ] Wokwi link added to the README
- [ ] Demo video link added to the README

```bash
git init && git add . && git commit -m "Remote Patient Risk Monitoring System - all 6 internship tasks"
```

```bash
git branch -M main && git remote add origin https://github.com/<you>/remote-patient-risk-monitoring-system.git && git push -u origin main
```

**GitHub URL:** `_______________________________________________`

---

## Phase 7 — PDFs for the Drive folder

The submission form asks for **PDFs**. Convert these Markdown files:

- [ ] `PROJECT_REPORT.md` → `Project_Report.pdf`
- [ ] `TASK_MAPPING.md` → `Task_Mapping.pdf`
- [ ] `ARCHITECTURE.md` → `Architecture.pdf`
- [ ] `TEST_PLAN.md` → `Test_Plan.pdf`
- [ ] `architecture-diagram.svg` → `Architecture_Diagram.pdf`
- [ ] `workflow-diagram.svg` → `Workflow_Diagram.pdf`

**Easiest conversion:** open the `.md` in VS Code → *Markdown PDF* extension → *Export
(pdf)*. For the SVGs, open in a browser → Ctrl+P → *Save as PDF* → set landscape.

---

## Phase 8 — Google Drive folder

Structure it so an evaluator finds everything in under a minute:

```
Hrishikesh_Lokhande_RPRMS/
├── 00_START_HERE.txt          ← GitHub + Wokwi + video links
├── 01_Project_Report.pdf
├── 02_Architecture_Diagram.pdf
├── 03_Workflow_Diagram.pdf
├── 04_Task_Mapping.pdf
├── 05_Test_Plan.pdf
├── 06_Screenshots/            ← all 12 PNGs
├── 07_Demo_Video.mp4
└── 08_Source_Code/            ← main.ino, diagram.json, libraries.txt
```

**`00_START_HERE.txt` template:**

```
REMOTE PATIENT RISK MONITORING SYSTEM
Hrishikesh Lokhande | ElevanceSkills Internship | IoT Domain

GitHub : https://github.com/<you>/remote-patient-risk-monitoring-system
Wokwi  : https://wokwi.com/projects/<id>
Video  : <link>

All 6 internship tasks implemented in one integrated Wokwi project.
Task-by-task evidence: 04_Task_Mapping.pdf
```

- [ ] Folder created and populated
- [ ] **Sharing set to "Anyone with the link — Viewer"**
- [ ] Verified in a private/incognito window that the link opens without sign-in

**Drive URL:** `_______________________________________________`

---

## Phase 9 — Submit

- [ ] Every box above ticked
- [ ] Drive link opens in incognito
- [ ] GitHub repo is public
- [ ] Wokwi project is public
- [ ] Video plays from the shared link

Then on the portal: **Submit your final project → Submit →** paste the Google Drive URL
and the project report field → **Submit project**.

- [ ] Screenshot the confirmation for your records

---

## Optional — email the mentors

The instructions modal says queries go to **training@elevanceskills.com** with name,
domain, GitHub repository link, and Wokwi project link. Sending this alongside your
submission costs nothing and creates a second record of your work:

```
Subject: Final Project Submission - Hrishikesh Lokhande - IoT Domain

Name   : Hrishikesh Lokhande
Domain : IoT / Embedded Systems
Project: Remote Patient Risk Monitoring System
GitHub : <link>
Wokwi  : <link>
Drive  : <link>

All six internship tasks are implemented in a single integrated Wokwi project
as required. Task-by-task evidence is in docs/TASK_MAPPING.md.
```
