# docs/evidence

Raw output of the verification runs behind [`../TEST_RESULTS.md`](../TEST_RESULTS.md). Nothing in
this folder was edited after the run; where a frame or a run is *not* the headline record, the
reason is given below.

## Final run — build `main.ino` sha256 `9e69da719afebbf68aa48637d9e12146e0f486461ead55e3a46ef7f892e43739`

| File | What it is |
|---|---|
| `final_pass.log` | **49 PASS, 0 FAIL, 49 checks** — one line per check with the measured value (finished 2026-10-13 17:36:06) |
| `final_pass_results.json` | the same, machine-readable |
| `final_pass_serial_full.txt` | serial monitor text of the session up to the end of the 49 checks |
| `final_session_serial_full_incl_supplementary.txt` | the complete serial text of the same session (49 checks + supplementary checks + fever test): 13 min 56 s simulated, **1** boot banner, 0 watchdog/panic/abort lines, 0 failed publishes, 0 keepalive drops |
| `tested_files_sha256.txt` | SHA-256 (LF-normalised, as typed into Wokwi) of `main.ino`, `diagram.json`, `libraries.txt` as tested |
| `supplement_pass.log`, `supplement_pass_results.json` | **4 PASS** supplementary checks run in the same session: the OLED dose footer is identical on all five pages, differs between dose 0 and 60, is present on the offline banner (S-14a/b/c); the Auto-bed row flips ON → OFF → ON (S-09b) |
| `supplementary_S1_fever_serial.txt` | fever path on the final build: DS18B20 39.0 °C → CRITICAL, rate 30 s → 5 s; 36.8 °C → "Patient stable", 5 s → 30 s after 3 samples |
| `supplementary_S2_gpio15_experiment.txt` | BUG_LOG B-14 re-verification: LED on GPIO15 never lights in Wokwi, GPIO19 does (run on the build that was final at that moment, `9d169735…`; the later change B-38 does not touch the LED path) |
| `wokwi_public_project_verification.txt`, `wokwi_public_project_logged_out.png` | The **public Wokwi project** after the final files were saved into it, opened by a fresh anonymous browser: its three files hash-match the repository, the build is HTTP 200 / 1,600,201 bytes and it boots to `[BOOT] 10 FreeRTOS tasks started` |
| `TC*.png` | OLED frames (6× the 128×64 framebuffer) and circuit shots saved by the suite. `TC08_pending_footer_conf.png` reads `DOSE 9.6 mg/hr CONF`; `TC01_oled_system_start.png` / `TC16_oled_system_end.png` give the free heap **166 KB → 165 KB** |

## Run history (`run_history/`) — kept so the path to 49/49 is visible

| Folder | Build | Result | Why it did not count |
|---|---|---|---|
| `run_A_build_add99597/` | `add99597…` | 43 PASS / 6 FAIL | `TC-09a` was a **firmware bug** (slider to 90° ramped at the emergency rate, 0.73 s; BUG_LOG B-36, fixed). `TC-11b`, `TC-12b`, `TC-05a/b`, `TC-02` were rig defects (BUG_LOG R-1…R-4) |
| `run_B_build_7467ed71/` | `7467ed71…` | 46 PASS / 3 FAIL | all three were rig defects: `TC-01h`, `TC-02` (wall-clock windows while the simulator ran at 36–67 % speed; `events_dump_run_b.json` is the raw LED/buzzer sampler data used to diagnose `TC-02`) and `TC-03-temp` (a lost switch click — `simserver.log`: `SWITCH swTemp -> 0` after a request for 1) |
| `run_C_build_9d169735/` | `9d169735…` | 49 PASS / 0 FAIL | superseded: the firmware changed afterwards (B-38). Includes its frames and its fever test |
| `screenshot_session_serial_with_wdt_reset.txt` | `9d169735…` | — | the serial log of the session in which the task watchdog rebooted the chip (BUG_LOG **B-38**): note the missing `[MQTT] published` lines for 30 s before `task_wdt … Aborting` |
| `simserver_gpio15_variant.log` | variant | — | controller log of the GPIO15 experiment (S2) |

Reproduce everything with [`tools/verification/`](../../tools/verification/README.md).
