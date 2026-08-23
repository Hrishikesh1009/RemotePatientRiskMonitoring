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

**Status as of this writing:** everything below is complete and verified **except the
demo video** (Phase 5) and the final portal submission (Phase 9). The Drive folder
(Phase 8) is created, populated, and shared — only your own incognito sanity-check and
the one-time Submit click remain.

---

## Phase 0 — Portal housekeeping (do this now, not at the end)

These come from the **Internship guidelines and policies** panel, not the task list, and
they affect your evaluation independently of the code.

- [ ] **Log daily reports.** Policy: *"Log a short work report for each active project…
      Consistent reporting reflects professionalism."* Use **Daily report → Log today's
      update** on the project card. This is a running obligation — a submission with an
      empty report history reads badly regardless of code quality. Keep this current
      through submission day.
- [x] **Mark all 6 tasks complete** in **Tasks → 6 assigned · open checklist**. This was
      done and the counter read **6/6** earlier in this session.
- [ ] Confirm the counter still reads **6/6** before submitting — the portal session has
      since expired (logged back out to the sign-in page), so this needs one fresh look
      after you log back in. It should still read 6/6 since nothing un-marks a task on its
      own, but verify rather than assume.

> **Stipend is a cliff, not a slope:** 6 of 6 tasks → ₹3,000. Exactly 5 → ₹500. Fewer
> than 5 → ₹0. Everyone receives a certificate either way. There is no partial credit
> between 5 and 6, so the sixth task is worth ₹2,500 on its own.

> **Rubric note:** *"All tasks must be fully integrated to meet the 100% completion
> requirement."* This is why everything lives in one `main.ino` rather than six sketches.

> **Originality note:** *"Maintain code quality, originality, and best practices."* Be
> ready to explain any part of the design in your own words — the mentors may ask. The
> rationale for every non-obvious decision is in `ARCHITECTURE.md`. Note also the honest
> caveat on role-based access in `TASK_MAPPING.md` Task 2 — be ready to explain the
> difference between topic separation (shipped) and broker-enforced ACLs (not shipped) if
> asked, rather than overclaiming it.

---

## Phase 1 — Code and simulation

- [x] `main.ino` pasted into Wokwi; simulation runs without error
- [x] `diagram.json` pasted; all 24 parts present, no floating wires
- [x] All 9 libraries from `libraries.txt` added
- [x] Default broker config verified — shipped default is `USE_HIVEMQ 1` (public
      `broker.hivemq.com`, no credentials needed). The alternate `USE_HIVEMQ 0` path for
      Adafruit IO exists in `main.ino` but is **not** the one exercised for this
      submission; if you switch to it, `IO_USERNAME`/`IO_KEY` must be replaced first.
- [x] Serial prints `[BOOT] 9 FreeRTOS tasks started`
- [x] OLED cycles all five pages
- [x] Ran 5+ minutes with no watchdog reset and stable free heap (~170 KB) — see
      `TEST_RESULTS.md`
- [x] Wokwi project **saved and set to public** — share URL below

**Wokwi URL:** `https://wokwi.com/projects/473060533558119425`

---

## Phase 2 — Dashboards

- [x] MQTT topics live on `broker.hivemq.com` under prefix `rprms-hl-8842` (see
      `DASHBOARD_SETUP.md` for the HiveMQ-vs-Adafruit-IO decision)
- [x] Medical Staff Dashboard built — gauges + text + alert stream + chart + controls
- [x] Facility Management Dashboard built — gauges + text + alert stream + chart
- [x] **Verified: no `medical.*` block appears on the Facility dashboard** — this is
      topic-level separation, not broker-enforced access control; see the caveat in
      `TASK_MAPPING.md` Task 2 before describing this as "role-based access" to an
      evaluator
- [x] Sliders confirmed working end-to-end (drag → OLED changes)
- [x] Aggregation JSON appearing on both `summary` feeds

---

## Phase 3 — Testing

Work through `TEST_PLAN.md`. Actual results are in `TEST_RESULTS.md` and are reproduced
verbatim in `PROJECT_REPORT.md` §10 — several are genuinely confirmed, a few are honestly
marked as not (yet) exercised or inconclusive rather than rubber-stamped Pass:

- [x] TC-01 Boot and task creation — **Pass**
- [ ] TC-02 Alarms unblocked during network stall — not exercised (needs a simultaneous
      temp-alarm + network-stall setup)
- [ ] TC-03 Sensor disconnect → DEGRADED — not exercised (needs precise in-circuit switch
      toggling)
- [x] TC-04 Role separation (topic-level) — **Pass**
- [ ] TC-05 Gauge thresholds — not exercised (needs precise in-circuit potentiometer
      dragging)
- [x] TC-06 Periodic aggregation — **Pass**
- [x] TC-07 Dosage rate limiting — **Pass**
- [x] TC-08 Dosage critical confirmation (both timeout and confirm paths) — **Pass**,
      triple-confirmed
- [x] TC-09 Bed smooth transition — **Pass**
- [x] TC-10 Automatic bed adjustment — **Pass** for manual override; auto-adjust
      direction confirmed via Node-RED simulator, not re-confirmed against the live
      device this run
- [ ] TC-11 Adaptive sampling — inconclusive (MQTT's once-per-period readback cadence
      didn't confirm the change within the observation window)
- [x] TC-12 Offline detection, buffering and resync — **Pass** for the backoff sequence;
      full resync/replay loop independently observed in a separate boot, same session

If more time is available before the deadline, re-running TC-02/03/05/11 against the live
device (rather than leaving them as documented gaps) would strengthen the submission, but
none of them block it — the gaps are disclosed, not hidden.

---

## Phase 4 — Screenshots

All 12 required + 3 bonus, captured against the live device — **done**:

- [x] 01 Medical dashboard, normal
- [x] 02 Medical dashboard, critical + alert stream
- [x] 03 Facility dashboard, normal
- [x] 04 Facility dashboard, AQI alert
- [x] 05 Dosage slider mid-ramp
- [x] 06 OLED `>CONFIRM` + red LED
- [x] 07 Bed control + servo horn
- [x] 08 Adaptive sampling at 5 s
- [x] 09 OLED `LOGGING OFFLINE`
- [x] 10 `system.backlog` resync
- [x] 11 Full Wokwi circuit
- [x] 12 Serial monitor boot lines
- [x] 13 (bonus) Node-RED flow editor
- [x] 14 (bonus) Architecture diagram
- [x] 15 (bonus) Workflow diagram

Stored in `screenshots/` (repo) and `submission_pdfs/15_Screenshots/` (Drive staging).

---

## Phase 5 — Demo video — NOT DONE (out of scope for this pass)

- [ ] Recorded following `DEMO_VIDEO_SCRIPT.md` (~6 minutes)
- [ ] All six tasks visibly demonstrated
- [ ] Audio clear; OLED readable when referenced
- [ ] Uploaded to Google Drive (or YouTube unlisted) with link sharing **on**

**Video URL:** `_______________________________________________`

This is the one required deliverable deliberately left for you to do yourself — everything
else in this checklist is ready. `DEMO_VIDEO_SCRIPT.md` has the shot-by-shot script.

---

## Phase 6 — GitHub repository

- [x] Public repo created: `RemotePatientRiskMonitoring`
- [x] Pushed: `main.ino`, `diagram.json`, `libraries.txt`, `wokwi.toml`, `README.md`,
      `docs/`, `dashboards/`, `screenshots/`
- [x] README renders correctly on GitHub
- [x] Both SVG diagrams display in the browser
- [x] Wokwi link added to the README
- [ ] Demo video link added to the README — pending Phase 5

**GitHub URL:** `https://github.com/Hrishikesh1009/RemotePatientRiskMonitoring`

---

## Phase 7 — PDFs for the Drive folder

The submission form asks for **PDFs**. All rendered and current in `submission_pdfs/`:

- [x] `PROJECT_REPORT.md` → `01_Project_Report.pdf`
- [x] `architecture-diagram.svg` → `02_Architecture_Diagram.pdf`
- [x] `workflow-diagram.svg` → `03_Workflow_Diagram.pdf`
- [x] `TASK_MAPPING.md` → `04_Task_Mapping.pdf`
- [x] `ARCHITECTURE.md` → `05_Architecture_Design.pdf`
- [x] `TEST_PLAN.md` → `06_Test_Plan.pdf`
- [x] `DASHBOARD_SETUP.md` → `07_Dashboard_Setup.pdf`
- [x] `DEMO_VIDEO_SCRIPT.md` → `08_Demo_Video_Script.pdf`
- [x] `SUBMISSION_CHECKLIST.md` (this file) → `09_Submission_Checklist.pdf`
- [x] `README.md` → `10_README.pdf`
- [x] `BUILD_VERIFICATION.md` → `11_Build_Verification.pdf`
- [x] `NODE_RED_SETUP.md` → `12_Node_RED_Setup.pdf`
- [x] `TEST_RESULTS.md` → `13_Test_Results.pdf`

All regenerated from current source as of the last doc edits (role-based-access caveat,
synced test results) — none are stale relative to the `.md` sources.

---

## Phase 8 — Google Drive folder — remaining step

Structure it so an evaluator finds everything in under a minute. `submission_pdfs/`
locally already mirrors this layout — it just needs uploading:

```
Hrishikesh_Lokhande_RPRMS/
├── 00_START_HERE.txt          ← GitHub + Wokwi + video links
├── 01_Project_Report.pdf
├── 02_Architecture_Diagram.pdf
├── 03_Workflow_Diagram.pdf
├── 04_Task_Mapping.pdf
├── 05_Architecture_Design.pdf
├── 06_Test_Plan.pdf
├── 07_Dashboard_Setup.pdf
├── 08_Demo_Video_Script.pdf
├── 09_Submission_Checklist.pdf
├── 10_README.pdf
├── 11_Build_Verification.pdf
├── 12_Node_RED_Setup.pdf
├── 13_Test_Results.pdf
├── 14_Source_Code/             ← main.ino, diagram.json, libraries.txt, node-red-flows.json
└── 15_Screenshots/              ← all 15 PNGs
```

`00_START_HERE.txt` already exists in `submission_pdfs/` with GitHub + Wokwi links; add
the video link once Phase 5 is done.

- [x] Folder created on Google Drive and populated (all 14 top-level files plus
      `14_Source_Code/` and `15_Screenshots/` uploaded and verified present)
- [x] **Sharing set to "Anyone with the link — Viewer"** (confirmed in the Share dialog)
- [ ] Verified in a private/incognito window that the link opens without sign-in — do
      this one yourself before submitting, since it needs a logged-out browser session

**Drive URL:** `https://drive.google.com/drive/folders/1tpl56Z8DgJtOQY_sncOU8mxls7EHA00L?usp=sharing`

---

## Phase 9 — Submit — do this yourself, last

- [ ] Every box above ticked (Phase 5 demo video is the one intentional exception until
      you record it)
- [ ] Drive link opens in incognito (verify yourself — needs a logged-out session)
- [x] GitHub repo is public
- [x] Wokwi project is public
- [ ] Video plays from the shared link

Then on the portal: **Submit your final project → Submit →** paste the Google Drive URL
and the project report field → **Submit project**.

This is a one-time, irreversible action — it is left for you to click yourself.

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
GitHub : https://github.com/Hrishikesh1009/RemotePatientRiskMonitoring
Wokwi  : https://wokwi.com/projects/473060533558119425
Drive  : https://drive.google.com/drive/folders/1tpl56Z8DgJtOQY_sncOU8mxls7EHA00L?usp=sharing

All six internship tasks are implemented in a single integrated Wokwi project
as required. Task-by-task evidence is in docs/TASK_MAPPING.md.
```
