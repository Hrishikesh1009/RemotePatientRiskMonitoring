# Build Verification

What was built, with which toolchain, and what that does and does not prove. Written for the
**submitted revision** (13 October 2026); older numbers are kept only where they are labelled as
history.

---

## 1. The submitted revision

| | |
|---|---|
| `main.ino` | SHA-256 `9e69da719afebbf68aa48637d9e12146e0f486461ead55e3a46ef7f892e43739` · 2066 lines · 92 244 bytes (LF-normalised) |
| `diagram.json` | `3602f9cf74674d9281c8cb3c9ad7a2d817670b21b2a6119cffdc1db38bce77ed` · 26 parts · 64 connections |
| `libraries.txt` | `2e74856bc9d7a8f800c3651dca25c8ecdec1e58f0924c3ef36e9cddc446727ae` |
| Toolchain | **Wokwi's cloud build service** (the toolchain behind the public project) |
| Result | **HTTP 200**, firmware image **1 600 201 bytes**, 0 compile errors — captured by the rig on 2026-10-13 |
| Evidence | `docs/evidence/tested_files_sha256.txt`; `tools/verification/` types exactly these files into a Wokwi page and records the build response |

Every verification run in `docs/evidence/` starts from a **fresh Wokwi build of these files**, so
"it builds" is re-established each time rather than assumed. The hashes are of the LF-normalised
text, which is what Wokwi receives; the working-tree copy on Windows has CRLF line endings.

### What is *not* measured for this revision

* **Flash and RAM usage.** Wokwi's build does not report them and the final revision was **not**
  recompiled locally. (The Wokwi image size above is the only size figure.)
* **Compiler warnings.** `--warnings all` output exists only for the earlier revision in §3.
* **Library versions.** `libraries.txt` is not version-pinned; Wokwi resolves the newest release at
  build time and its Library Manager does not display the versions. §3 lists the versions the
  earlier local build resolved, for reference only.

If exact figures are wanted, §4 reproduces a local compile in a few commands.

---

## 2. Defect found by compiling (kept: it shaped the file layout)

Compiling caught one genuine error that reading would not reliably have surfaced:

```
error: variable or field 'aggReset' declared void
error: 'Aggregate' was not declared in this scope
```

**Cause.** The Arduino `.ino` preprocessor generates forward prototypes for every function and
inserts them near the *top* of the file, above the user's code. The `Aggregate` struct was defined
further down, so the generated prototypes referenced a type that did not yet exist. The identical
code in a `.cpp` file compiles, which is why it survives review.

**Fix.** Every struct used in a function signature is declared together in Section 4, above the
first function. A comment there explains why, so the ordering is not "tidied" back into a broken
state (`BUG_LOG.md` B-04).

---

## 3. History — the last local `arduino-cli` compile (earlier revision, August 2026)

This was run on an **earlier revision** (9 tasks, before the work of 13 October) on a different
machine and has **not** been repeated for the final one; it is kept to show the toolchain and
library set, not as a claim about today's sizes.

```
Sketch uses 980546 bytes (74%) of program storage space. Maximum is 1310720 bytes.
Global variables use 53840 bytes (16%) of dynamic memory, leaving 273840 bytes for local variables.
```

| Component | Version (August 2026) |
|-----------|-----------------------|
| arduino-cli | 1.5.2-rc.1 |
| esp32:esp32 core | 3.3.11 |
| Board | `esp32:esp32:esp32doit-devkit-v1` |
| OneWire / DallasTemperature | 2.3.8 / 4.0.6 |
| Adafruit GFX / SSD1306 / BusIO | 1.12.6 / 2.5.17 / 1.17.4 |
| Adafruit MQTT Library | 2.6.6 (CONNECT keepalive 300 s) |
| Adafruit Unified Sensor / DHT sensor library | 1.1.15 / 1.4.7 |
| ESP32Servo | 3.2.1 |

---

## 4. Reproduce a local compile

```bash
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index && arduino-cli core install esp32:esp32@3.3.11
arduino-cli lib install OneWire DallasTemperature "Adafruit GFX Library" "Adafruit SSD1306" "Adafruit BusIO" "Adafruit MQTT Library" "Adafruit Unified Sensor" "DHT sensor library" ESP32Servo
```

The sketch folder name must match the sketch file name, so copy `main.ino` to
`RemotePatientRiskMonitoring/RemotePatientRiskMonitoring.ino`, then:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32doit-devkit-v1 RemotePatientRiskMonitoring --warnings all
```

or open the public Wokwi project, press Play and watch the serial monitor print
`[BOOT] 10 FreeRTOS tasks started`.

---

## 5. Emulator attempt (Espressif QEMU) — recorded, not used

Before Wokwi was adopted the earlier revision was flashed into a 4 MB image and booted under
Espressif's `qemu-system-xtensa`. The bootloader loaded the image and `setup()` printed the banner,
then QEMU stopped it twice: it does not emulate the Wi-Fi PHY (`assert failed: esp_phy_enable`),
and it hit a flash-cache error inside ESP-IDF start-up before any application code. Neither is a
defect in the project; QEMU is simply not a viable runtime harness for this firmware, which is why
every functional result in `TEST_RESULTS.md` comes from Wokwi, whose models cover the OLED, DS18B20,
DHT22, servo and potentiometers and which provides the Wi-Fi/MQTT bridge.

---

## 6. What this does and does not prove

**Proven.** The final files compile on Wokwi's ESP32 toolchain into a bootable image and run (49 of
49 automated checks, `TEST_RESULTS.md`). Every library call matches its real signature, including
FreeRTOS, `esp_task_wdt` and the `ESP_IDF_VERSION_MAJOR` guard around the watchdog reconfiguration.

**Not proven.** Behaviour on real silicon; flash/RAM headroom of the final revision; compatibility
with library versions other than the ones Wokwi resolved on the day.
