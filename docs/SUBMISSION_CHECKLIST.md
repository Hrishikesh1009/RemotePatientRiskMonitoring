# Submission Checklist

## ⚠️ Read this first

The portal states that submission is **one-time**: *you cannot change, edit, replace or update
anything afterwards*, and submitting also ends the internship (the end date becomes the day you
submit). **Do not press Submit until every box in Parts B and C is ticked.**

| | |
|---|---|
| **Deadline** | The task page shows **25 Oct 2026**; the submit dialog shows "October 26, 2026" with a countdown (12 d 4 h left on the evening of 13 Oct) that ends at **00:00 IST on 26 Oct — i.e. the end of 25 Oct**. Submit well before that; a late submission earns no stipend. |
| **Submit form** (re-checked on 13 Oct 2026 by opening the dialog and cancelling) | **Website Hosted URL** · **GitHub Repository** · **Project report** (a rich-text box) · buttons *Cancel* / *Submit project* |
| **Stipend rule** | 6 of 6 tasks → ₹3,000 · exactly 5 → ₹500 · fewer → ₹0; everyone gets the certificate. The portal counter should read **6/6** — check it. |
| **Also required by the instructions dialog** | Technical report with the listed sections · mandatory screenshots · demonstration video · README, **Bug Log**, **Assumptions**, **Self-Attestation** · every link public and working *without logging in* |
| **"Assignment document"** | The dialog says the folder structure and screenshot names must follow it. **No such document exists anywhere on the portal** (checked on 13 Oct). The repository keeps its existing layout and contains every file the dialog names. If a mentor sends you that document, compare names before submitting. |

**Public links**

* Wokwi (live simulation): https://wokwi.com/projects/473060533558119425
* GitHub: https://github.com/Hrishikesh1009/RemotePatientRiskMonitoring
* Drive folder (optional mirror): https://drive.google.com/drive/folders/1tpl56Z8DgJtOQY_sncOU8mxls7EHA00L

---

## Part A — Finished and verified (nothing for you to do)

| ✔ | Item | Evidence |
|---|------|----------|
| ✔ | All six tasks implemented **inside one integrated `main.ino`** (10 FreeRTOS tasks), circuit `diagram.json` (26 parts), `libraries.txt` | `docs/TASK_MAPPING.md` |
| ✔ | Builds on Wokwi's cloud toolchain from exactly the repository files (HTTP 200, image 1,600,201 bytes) | `docs/BUILD_VERIFICATION.md` |
| ✔ | **16 test cases run as 49 automated checks — all pass**, plus 4 pixel-level supplementary checks, fever test, GPIO15 experiment; the full run history (including the failures and their causes) is kept | `docs/TEST_RESULTS.md`, `docs/evidence/` |
| ✔ | Technical report with Overview, Architecture, Design, State-Machine Design, Verification Methodology, Simulation Results, Challenges, Conclusions | `docs/TECHNICAL_REPORT.md` |
| ✔ | **Bug Log** (38 defects + 5 test-rig defects, each with cause, fix, measurement), **Assumptions** (A1–E) | `docs/BUG_LOG.md`, `docs/ASSUMPTIONS.md` |
| ✔ | **Self-Attestation** drafted with the evidence column filled in — *the boxes and the signature are yours* (Part C) | `docs/SELF_ATTESTATION.md` |
| ✔ | 15 screenshots (12 required + 3 bonus), taken on the final build | `screenshots/README.md` |
| ✔ | Architecture and workflow diagrams (SVG and PNG) | `docs/architecture-diagram.svg`, `docs/workflow-diagram.svg` |
| ✔ | Two role-separated Node-RED dashboards (84-node flow), setup guides, topic map | `dashboards/`, `docs/NODE_RED_SETUP.md` |
| ✔ | Demo-video script that covers overview → implementation → simulation → verification → working demonstration | `docs/DEMO_VIDEO_SCRIPT.md` |
| ✔ | Measurement rig and how to re-run every number | `tools/verification/README.md` |

---

## Part B — Public copies that must match the repository

The final firmware is `main.ino` SHA-256 `9e69da719afebbf68aa48637d9e12146e0f486461ead55e3a46ef7f892e43739`
(2066 lines). **The live Wokwi project must contain exactly these three files.** An evaluator who
presses Play on the Wokwi link otherwise runs an older sketch — the previous public project still
had the pre-fix wiring (BUG_LOG B-14 – B-16, B-23).

- [x] **Wokwi project updated and saved** (13 Oct, in your logged-in browser): `sketch.ino`,
      `diagram.json` and `libraries.txt` now hold the final files. The previous live content
      (the old 4-switch circuit) was backed up first. *`libraries.txt` was already identical; it
      holds one library per line without the `#` comment lines of the repository copy.*
- [x] **Verified logged out** (a fresh anonymous browser opened the public link, nothing injected):
      the three files hash-match the repository, the build is HTTP 200 / 1,600,201 bytes, serial
      prints `[BOOT] firmware 2026-10-13 final …` and `[BOOT] 10 FreeRTOS tasks started` —
      `docs/evidence/wokwi_public_project_verification.txt` and `…_logged_out.png`.
- [ ] **GitHub `main` updated** — the work is committed locally; publish it with
      `git push` from the repository folder (Git Credential Manager opens a browser window the
      first time: sign in as the repository owner).
- [ ] **Verify GitHub logged out** after the push — the README renders, both diagrams display,
      `docs/evidence/` and `tools/verification/` are present.
- [ ] *(optional)* **Drive folder refreshed** — `submission_pdfs/` holds up-to-date PDFs and a
      `00_START_HERE.txt`; upload its contents (the folder is git-ignored staging).

---

## Part C — Only you can do these

- [ ] **Record the demo video** (about 7 minutes) following `docs/DEMO_VIDEO_SCRIPT.md`, in your
      own words. Say plainly what is simulated (heart rate, SpO₂, blood pressure) and that role
      separation is by topic only. Upload it (Drive "Anyone with the link" or YouTube unlisted),
      open the link in a private window, then **add the link** to: `README.md` (status list),
      `docs/TECHNICAL_REPORT.md` (table at the top) and the Drive `00_START_HERE.txt`.
- [ ] **Complete `docs/SELF_ATTESTATION.md`** — tick only what is true, write the *Disclosure of
      assistance* in your own words (this project was built with AI coding assistants; the policy
      treats misrepresenting authorship as a disqualifying offence), sign and date it, commit it.
- [ ] **Log your daily report(s)** on the portal ("Log today's update" on the project card). The
      guidelines treat consistent reporting as part of the evaluation.
- [ ] **Be able to explain it.** Read `docs/ARCHITECTURE.md` and the limits below; mentors may ask.
- [ ] **Final link test**, all in a private window: Wokwi · GitHub · video · (Drive).
- [ ] **Submit** (below), then screenshot the confirmation.

### What to paste into the portal

| Field | Value |
|---|---|
| Website Hosted URL | `https://wokwi.com/projects/473060533558119425` |
| GitHub Repository | `https://github.com/Hrishikesh1009/RemotePatientRiskMonitoring` |
| Project report | the content of `docs/TECHNICAL_REPORT.md`. Easiest: open the file on GitHub, select the rendered text, copy, paste into the box, then check headings, tables and code blocks survived. Put the video link in the first table first. |

---

## Part D — Limits to be ready to explain (all disclosed in the repository)

* **Role-based access is topic separation only** — the public broker has no ACLs (`TASK_MAPPING.md` Task 2).
* **Heart rate, SpO₂ and blood pressure are simulated** (Wokwi has no such parts); ECG, O₂ and AQI come from potentiometers.
* **Thresholds are illustrative**, not clinically validated; this is not a medical device.
* **Not exercised in simulation:** DEGRADED caused by publish failures, the failing-write branch of the
  B-38 watchdog fix (the simulator cannot stall the link on demand), the optional SPIFFS mirror.
* **The public broker / Wokwi gateway sometimes stops answering the simulated device** after ~10–15
  simulated minutes. The firmware shows `LOGGING OFFLINE` and keeps retrying (it behaved correctly in
  the natural outage in `docs/evidence/supplementary_S3_…`); pressing **Restart** in Wokwi clears it.
* **Flash/RAM usage of the final revision was not measured** (Wokwi does not report it).
* **Dashboards are local** (`http://127.0.0.1:1880/ui`), evidenced by screenshots and the importable flow.

---

## Optional — email the mentors

Queries go to **training@elevanceskills.com** with name, domain, GitHub link and Wokwi link:

```
Subject: Final Project Submission - Hrishikesh Lokhande - IoT Domain

Name   : Hrishikesh Lokhande
Domain : IoT / Embedded Systems
Project: Remote Patient Risk Monitoring System
GitHub : https://github.com/Hrishikesh1009/RemotePatientRiskMonitoring
Wokwi  : https://wokwi.com/projects/473060533558119425

All six internship tasks are implemented in a single integrated Wokwi project.
Task-by-task evidence: docs/TASK_MAPPING.md. Measured results: docs/TEST_RESULTS.md.
```
