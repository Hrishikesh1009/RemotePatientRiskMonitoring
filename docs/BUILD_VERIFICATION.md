# Build Verification

The firmware has been **compiled successfully** against the official Espressif
Arduino-ESP32 toolchain — not merely reviewed. This document records the exact
toolchain, command and result so the build is reproducible.

---

## Result

```
Sketch uses 980546 bytes (74%) of program storage space. Maximum is 1310720 bytes.
Global variables use 53840 bytes (16%) of dynamic memory,
leaving 273840 bytes for local variables. Maximum is 327680 bytes.
```

- **0 errors**
- **0 warnings from the sketch** on a clean rebuild with `--warnings all`
- Flash: **74 %** used — 330 KB headroom
- Static RAM: **16 %** used — 267 KB free for task stacks and the Wi-Fi/TCP stack

The 53,840 bytes of static RAM includes the 9 task stacks (43,008 B), the 120-record
offline ring buffer (3,840 B), the SSD1306 framebuffer (1,024 B) and the queues.
The remaining 273,840 bytes comfortably covers the Wi-Fi/LWIP stack (~40 KB) and heap.

---

## Toolchain

| Component | Version |
|-----------|---------|
| arduino-cli | 1.5.2-rc.1 (commit `fef6e48df`) |
| esp32:esp32 core | 3.3.11 |
| Board FQBN | `esp32:esp32:esp32doit-devkit-v1` |
| Host | Windows 11 |

### Libraries resolved

| Library | Version |
|---------|---------|
| OneWire | 2.3.8 |
| DallasTemperature | 4.0.6 |
| Adafruit GFX Library | 1.12.6 |
| Adafruit SSD1306 | 2.5.17 |
| Adafruit BusIO | 1.17.4 |
| Adafruit MQTT Library | 2.6.6 |
| Adafruit Unified Sensor | 1.1.15 |
| DHT sensor library | 1.4.7 |
| ESP32Servo | 3.2.1 |
| Wire / WiFi / Networking / SPI | 3.3.11 (bundled with the core) |

---

## Reproducing the build

```bash
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

```bash
arduino-cli core update-index && arduino-cli core install esp32:esp32@3.3.11
```

```bash
arduino-cli lib install OneWire DallasTemperature "Adafruit GFX Library" "Adafruit SSD1306" "Adafruit BusIO" "Adafruit MQTT Library" "Adafruit Unified Sensor" "DHT sensor library" ESP32Servo
```

The sketch folder name must match the sketch file name, so copy `main.ino` to
`RemotePatientRiskMonitoring/RemotePatientRiskMonitoring.ino`, then:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32doit-devkit-v1 RemotePatientRiskMonitoring --warnings all
```

---

## Defect found and fixed by compiling

Compiling caught one genuine error that no amount of reading would reliably have
surfaced:

```
error: variable or field 'aggReset' declared void
error: 'Aggregate' was not declared in this scope
```

**Cause.** The Arduino `.ino` preprocessor auto-generates forward prototypes for every
function and inserts them near the *top* of the file, above the user's own code. The
`Aggregate` struct was defined further down, just before the aggregation functions that
use it — so the generated prototypes referenced a type that did not yet exist.

This is specific to the `.ino` build model; the identical code in a `.cpp` file compiles
without complaint, which is exactly why it survives review.

**Fix.** All structs used in function signatures are now declared together in Section 4,
above the first function definition. A comment at that point in `main.ino` explains why,
so the ordering is not "tidied up" back into a broken state later.

---

## Emulator attempt (Espressif QEMU) — partial

Beyond compiling, the firmware was flashed into a 4 MB image and booted under
Espressif's ESP32 emulator (`qemu-system-xtensa`, esp-develop-9.2.2-20260417):

```bash
qemu-system-xtensa -nographic -machine esp32 -drive file=RemotePatientRiskMonitoring.ino.merged.bin,if=mtd,format=raw
```

**What worked.** The second-stage bootloader validated and loaded the image, the entry
point ran, and `setup()` executed far enough to emit:

```
=== Remote Patient Risk Monitoring System ===
ElevanceSkills internship build -- 6 integrated tasks
[WIFI] not connected -- starting offline
```

That confirms the flash image is well-formed and bootable, and that `setup()` runs
through GPIO configuration, driver construction and the non-blocking Wi-Fi path.

**Where it stopped.** Two emulator limitations blocked a full run:

1. `assert failed: esp_phy_enable phy_init.c:336` — QEMU does not emulate the Wi-Fi
   PHY, so `WiFi.begin()` traps. Worked around in a throwaway test build by stubbing
   the radio; the shipped `main.ino` is unmodified.
2. `Guru Meditation Error: Core panic'ed (Cache error) — Cache disabled but cached
   memory region accessed`, with a backtrace inside ESP-IDF startup, *before* any
   application code. Rebuilding at `FlashFreq=40` did not change it.

Failure (2) sits in the emulator's SPI-flash cache model, which is a known gap for
Arduino-ESP32 images (Espressif's QEMU targets ESP-IDF apps with specific sdkconfigs).
It is **not** evidence of a defect in this project — it reproduces before `app_main`
hands control to Arduino's `setup()`, and the same image compiles clean and boots to
the banner.

**Conclusion:** QEMU is not a viable runtime harness for this firmware.
**Use Wokwi**, which emulates the peripherals this project actually needs — OLED,
DS18B20, DHT22, servo, potentiometers — and provides the Wi-Fi/MQTT bridge that QEMU
lacks. Run `TEST_PLAN.md` TC-01…TC-12 there.

---

## What this does and does not prove

**Proven**
- The code is syntactically and semantically valid C++ for the ESP32 target.
- It links into a valid, bootable 4 MB flash image, and `setup()` executes on an
  emulated ESP32 as far as the Wi-Fi stage.
- Every library API call (`Adafruit_MQTT`, `ESP32Servo`, `DHT`, `DallasTemperature`,
  `Adafruit_SSD1306`, FreeRTOS, `esp_task_wdt`) matches its real signature.
- The FreeRTOS and watchdog calls compile against ESP-IDF 5.x under Arduino core 3.x,
  including the `ESP_IDF_VERSION_MAJOR` guard around `esp_task_wdt_reconfigure()`.
- It fits comfortably in flash and RAM.

**Not proven — still requires running the simulation**
- Runtime behaviour: task scheduling, alarm timing, the dosage and servo ramps.
- Wi-Fi / MQTT connectivity and the dashboards.
- The twelve test cases in `TEST_PLAN.md`.

Run `TEST_PLAN.md` TC-01 through TC-12 in Wokwi before submitting. A clean compile means
the build will not fail on you in the simulator; it does not mean the logic is verified.
