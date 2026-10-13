# tools/verification — the measurement rig

This folder is the rig that produced the numbers in [`docs/TEST_RESULTS.md`](../../docs/TEST_RESULTS.md).
It is a **development aid, not part of the product**: nothing in `main.ino` depends on it, and the
test cases in [`docs/TEST_PLAN.md`](../../docs/TEST_PLAN.md) can all be followed by hand in the public
Wokwi project. The rig exists because *"I watched it and it looked right"* is not a verification
method — it makes each check a measurement with a recorded value.

## What it does

```
 headless Chrome (anonymous)            simserver.js  (http://127.0.0.1:9444)         suite.ps1
 ┌──────────────────────────┐  CDP     ┌───────────────────────────────────┐  HTTP   ┌──────────────────┐
 │ Wokwi project page       │◀────────▶│ loads main.ino/diagram.json/       │◀───────▶│ 49 checks, one   │
 │  · editors (Monaco)      │          │ libraries.txt from this repo,      │         │ PASS/FAIL line   │
 │  · serial monitor        │          │ presses Play, then serves:         │         │ each, with the   │
 │  · OLED canvas           │          │  /serial /status (sim clock)       │         │ measured value   │
 │  · LEDs, buzzers, servo  │          │  /oled /shot  /events (LED, buzzer,│         │                  │
 │  · switches, pots        │          │  servo sampler, 30 ms)  /switch    │         └──────────────────┘
 └──────────────────────────┘          │  /pot /part  /pub  /mqtt           │
                                       └──────────────┬────────────────────┘
                                                      │ MQTT (independent client)
                                              broker.hivemq.com  ◀── the device under test
```

* The page is opened **anonymously** and the repository files are typed into its editors, so the
  Wokwi account/project is never modified; Wokwi builds the firmware on its own cloud toolchain.
* Everything the suite knows about the device it reads as **data**: serial text (the `t=… ms`
  stamps are simulated time), the simulator clock, the SSD1306 canvas, the `value` of each LED, the
  `hasSignal` of each buzzer, the servo `angle`, and the messages an *independent* MQTT client
  receives from the broker.
* Switches are operated by real mouse events at the part's on-screen position, potentiometers by
  setting their value and dispatching the same `input` event the UI would. The controller
  **verifies that a switch reached the requested state and re-clicks if the click was lost**.
* **Durations that belong to the device are measured on the simulator's own clock**, never with a
  wall-clock sleep: Wokwi ran at roughly 35–90 % of real time on the test machine and the speed
  moves with host load, so `Start-Sleep 4` is anywhere between 1 and 4 simulated seconds.
  (Two checks first failed for exactly this reason — see `TEST_RESULTS.md`, "Run history".)

## Requirements

| | |
|---|---|
| OS | Windows 10/11 with Windows PowerShell 5.1 (the scripts use `Invoke-RestMethod`) |
| Node.js | **22 or newer** (uses the built-in `WebSocket`); v24.19 was used |
| Browser | Google Chrome or Microsoft Edge (`CHROME_PATH` overrides the auto-detected location) |
| Network | Internet access to `wokwi.com` and `broker.hivemq.com:1883` |
| Packages | `npm install` in this folder (installs `mqtt` 5.15.2) |

No Wokwi login is needed. The first build of a session can take a minute or two on Wokwi's free
queue and occasionally returns HTTP 524 (a gateway timeout; `GET /status` lists it under `builds`
and the simulation never starts). Restarting the controller — or clicking Play again through
`POST /eval` — normally succeeds.

## Run it

```bash
cd tools/verification
npm install
node simserver.js
```

Wait for `READY: controller up, simulation requested` in the console, then (in a second terminal):

```bash
cd tools/verification
powershell -NoProfile -ExecutionPolicy Bypass -File suite.ps1
```

The suite takes about 25–30 minutes of real time (≈ 10 simulated minutes). While the simulation
is still running, `supplement.ps1` adds four checks that read the OLED framebuffer as pixels
(5–8 minutes; same `-EvidenceDir` option):

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File supplement.ps1
```

`suite.ps1` writes to `docs/evidence/` (override with `-EvidenceDir <folder>`):

| File | Content |
|---|---|
| `final_pass.log` | one `[PASS]`/`[FAIL]` line per check with the measured value, then a summary |
| `final_pass_results.json` | the same, machine-readable |
| `final_pass_serial_full.txt` | the complete serial monitor text of the session |
| `tested_files_sha256.txt` | SHA-256 (LF-normalised) of `main.ino`, `diagram.json`, `libraries.txt` as tested |
| `supplement_pass.log`, `supplement_pass_results.json` | the four pixel-level checks of `supplement.ps1` |
| `*.png` | OLED frames and circuit shots taken during the checks |

Stop the controller with `Invoke-RestMethod http://127.0.0.1:9444/quit`. Do **not** use the machine
heavily while it runs: the simulator is CPU-bound, and although every window is in simulated time,
a starved simulator can still turn a slow reconnect into a timeout.

## HTTP API (for ad-hoc probing)

| Route | Purpose |
|---|---|
| `GET /status` | simulation clock `mm:ss.mmm`, speed %, serial length, build results, running flag |
| `GET /serial?from=N` , `?tail=N` | serial monitor text |
| `GET /events?since=N` | LED / buzzer / servo changes with wall and simulation timestamps |
| `GET /switch?id=swWifiCut&state=1` | operate a slide switch (verified, re-clicked if lost) |
| `GET /pot?id=potAqi&value=650` | set a potentiometer (0–1023) |
| `GET /pub?feed=control.dosage&value=95` | publish a control message to the device |
| `GET /mqtt?since=<ms>&topic=…` , `/mqtt/last` | what the independent subscriber saw |
| `GET /oled?name=x&scale=6` | OLED framebuffer as a PNG (waits for an untorn frame) |
| `GET /shot?name=x&region=circuit` | page / circuit screenshot |
| `GET /part?id=bedServo` | read a part's properties (`angle`, `value`, `hasSignal`, …) |
| `POST /eval` (body = JavaScript) | evaluate an expression in the Wokwi page — debugging only |
| `GET /restart` , `/quit` | restart the simulation / shut the rig down |

## Check → test case map

`TC-01a…h` boot, tasks, watchdog, resets, the five OLED pages · `TC-15` command burst ·
`TC-11a…e` adaptive sampling · `TC-03` probe disconnect (temp, HR, SpO₂) · `TC-07`, `TC-14`,
`TC-08A/A2/B` dosage · `TC-09a…d`, `TC-10a…d` bed · `TC-12a…f` Wi-Fi outage · `TC-13a…c` broker
outage · `TC-02` alarm cadence during an outage · `TC-05a…d`, `TC-04`, `TC-06` facility inputs and
aggregation · `TC-16a` stability (no reset / watchdog / panic over the whole run).
`supplement.ps1`: `S-14a/b/c` (the dose footer band is identical on all five OLED pages, differs
between dose 0 and 60, and is on the offline banner — this is the measured version of `TC-14`,
whose suite line only points at a frame) and `S-09b` (the Auto-bed row of the THERAPY page flips
ON → OFF → ON — the measured version of `TC-09b`). The free-heap
figure in `TC-16` is read by eye from the two OLED SYSTEM-page frames the suite saves
(`TC01_oled_system_start.png`, `TC16_oled_system_end.png`); it is not asserted automatically.

## Honest limits of the rig

* It tests the firmware **as simulated by Wokwi**, not real silicon. Timing is the simulator's
  clock, not a logic analyser.
* The LED/buzzer sampler polls every 30 ms of real time, so it cannot resolve every 60 ms toggle at
  low simulator speed; checks that depend on them count *bursts* or *envelopes*, not edges.
* It depends on Wokwi's page structure (element ids, `aria-label`s, the Monaco editor). A Wokwi UI
  update can break the selectors without the firmware changing.
* The public MQTT broker and Wokwi's IoT gateway occasionally drop the session. The suite waits for
  the device to be ONLINE before each section (`Ensure-Online`) and restarts the simulation if it
  does not recover; any such restart is printed in the log.
* The copy in this folder differs from the one used for the recorded runs only in path handling
  (relative paths, `-EvidenceDir`, an automatic hash file).
