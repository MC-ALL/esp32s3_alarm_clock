# Smart Clock (ESP-IDF)

This repository is an `ESP32-S3` smart clock firmware project based on `ESP-IDF`.
The current code is in bring-up stage: drivers and service owners are wired, then hardware tuning and field validation follow.
The current adaptation target is `ESP-IDF 6.0.1`.

## 1. Current Status

### 1.1 Current Visible Effect
- LCD is already driven by `SPI + ST7789 + LVGL`.
- Home screen is no longer a plain bring-up page. It already shows:
  - date / time
  - sound / audio status
  - environment row
  - tips row
  - alarm summary
  - todo list
  - bottom key hints
- `K1-K4` page switching is wired:
  - `K1 -> Settings`
  - `K2 -> Alarm`
  - `K3 -> Network`
  - `K4 -> Power`
- Static LVGL skeleton pages already exist for:
  - `SETTINGS`
  - `ALARM`
  - `NETWORK`
  - `POWER OFF?`

### 1.2 Functional Completion vs Expected Design
- Already done
  - ESP-IDF project skeleton and module split
  - LCD / backlight / key input / Wi-Fi base / NVS / BH1750 / LD2410C UART / I2S test tone
  - LVGL home screen skeleton
  - basic page switching
  - system-time-first display fallback (`UNSYNC` when time is not valid)
- Partially done
  - radar presence path:
    UART is alive, `OUT` status is readable, home page `DETECTED` indicator is wired
  - environment data path:
    `BH1750` is usable, `DHT11` is still unstable
- Not done yet
  - full per-page interaction state machine
  - real settings editing
  - real alarm CRUD
  - real network scan/connect flow
  - proper LD2410C frame parsing with meaningful target metrics
  - stable DHT11 replacement driver
  - polished LVGL visuals fully matching `ui-design.md`

### 1.3 Important Gaps To Know Before Handoff
- `DHT11` currently still uses a fragile local bit-bang implementation.
- Radar presence result is still a bring-up version, not a full protocol-decoded result.
- Some LVGL page content is still placeholder text.
- Home page layout is usable, but not yet final-polish quality.

## 2. Directory Quick Guide

### `CMakeLists.txt`
- Root build entry of this firmware project.
- Includes `ESP-IDF` build system and defines project name.

### `main/`
- Main component build definition.
- `main/CMakeLists.txt` registers source files from `src/`, `modules/`, and include paths.
- Also declares required `ESP-IDF` components (`driver`, `esp_lcd`, `lvgl`, `esp_wifi`, `nvs_flash`, etc.).

### `src/`
- Application bootstrap and module lifecycle orchestration.
- Key files:
  - `src/main.c`: `app_main()` entry.
  - `src/app_boot.c`: boot flow.
  - `src/app_module.c`: module registration table and init/start/stop order.

### `modules/`
- Feature owners, split by responsibility:
  - `modules/interaction/`: display, backlight, key input, UI model.
  - `modules/sensing/`: environment sensors, presence/radar.
  - `modules/connectivity/`: Wi-Fi and network bootstrap.
  - `modules/audio/`: I2S audio output.
  - `modules/config/`: persistence and settings-related services.
  - `modules/core/`: lifecycle/timebase/fault scaffolding.
  - `modules/reminder/`: reminder service scaffolding.

### `include/`
- Public headers and shared hardware/config constants.
- Key files:
  - `include/app/hw_config.h`: board pin mapping and bus-level constants.
  - `include/app/app_config.h`: project-level runtime placeholders (Wi-Fi SSID/password, SNTP server).
  - Service API headers in `include/app/*.h`.

### `components/`
- Local ESP-IDF component area.
- Current repo contains a directory named `components/Use an existing ESP-IDF directory/`.
- This directory looks like an import/scaffold artifact rather than a clean product component.
- If future teammates do not use it, they should treat it carefully and avoid assuming it is production code.

### `sdkconfig.defaults`
- Default `ESP-IDF` config baseline.
- Used to seed config when first running `idf.py` commands.
- Current baseline is intended for `ESP-IDF 6.0.1`.
- Default flash size is aligned to `16 MB` hardware.

### `partitions.csv`
- Custom flash partition table.
- Currently includes `nvs`, `phy_init`, `factory`.

### `docs/`
- Design, API handoff, and hardware reference material.
- Important subfolders:
  - `docs/api/`: API/interface handoff docs.
  - `docs/hw_datasheet/`: hardware datasheets and notes.
  - `docs/plans/`: planning and architecture evolution.

## 3. Build And Run

Current first-pass code paths include:
- `SPI + ST7789 + LVGL` display path.
- Backlight PWM (`LEDC`).
- 4-key GPIO input with ISR + queue.
- Presence radar (`LD2410C`, UART + OUT pin).
- Environment sensors (`BH1750` + `DHT11`).
- Audio output (`I2S` test tone path).
- Network base (`Wi-Fi STA` + event loop + SNTP skeleton).
- Local persistence init (`NVS`).

### 3.1 Prerequisites
- `ESP-IDF 6.0.1` installed locally.
- Board target: `ESP32-S3`.
- USB serial access to dev board.

### 3.2 Build
Run in project root:

```bash
. /path/to/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
```

### 3.3 Flash and Monitor

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Replace serial port with your local device (`/dev/ttyUSB0`, `/dev/ttyACM0`, `COMx`, etc.).

### 3.4 First Validation Checklist
1. Boot logs show module init/start sequence without fatal errors.
2. LCD powers on and shows the structured LVGL home screen.
3. `K1-K4` can switch between home / settings / alarm / network / power pages.
4. Backlight duty responds to service startup defaults.
5. Key GPIO interrupts produce logs on press/release.
6. Radar path (`LD2410C`) shows UART activity, and home page can show `DETECTED` when presence path is active.
7. Environment logs update with BH1750/DHT11 samples.
8. I2S path plays startup test tone.
9. Wi-Fi path starts, and SNTP sync status logs can be observed after network is configured.

## 4. Local Configuration Notes

### Wi-Fi Credentials
Set in:
- `include/app/app_config.h`

Current placeholders must be replaced for real Wi-Fi/SNTP validation.
If credentials are intentionally left empty during bring-up, the firmware will skip connect and continue booting.

### Pin Mapping
If board wiring differs, update:
- `include/app/hw_config.h`

### Version Adaptation Notes
Current `ESP-IDF 6.0.1` adaptation record:
- `docs/plans/2026-05-17-idf-6-adaptation-design.md`
- `docs/plans/2026-05-17-lvgl-home-alignment-design.md`
- `docs/plans/2026-05-17-lvgl-subpages-time-design.md`
- `docs/plans/2026-05-17-dht11-flicker-design.md`

## 5. Runtime Notes

### Presence / Radar
- Current radar path is still in bring-up mode.
- UART is alive.
- `OUT` pin is wired into software.
- Presence logs currently focus on:
  - `state`
  - `out`
  - `uart`
  - `rx_ms`
- Full protocol-decoded LD2410C metrics are not finished yet.

### Environment
- `BH1750` is currently the more trustworthy source.
- `DHT11` remains unstable and should not yet be treated as production-ready.

### Time
- Home page prefers real system time.
- If time is not valid yet, UI shows `UNSYNC`.

## 6. Handoff Notes

When hardware teammate reports build/runtime issues, provide:
1. `idf.py build` first error block.
2. Boot logs from `idf.py monitor`.
3. Board wiring differences versus `include/app/hw_config.h`.
4. If UI/radar behavior is wrong, also provide a short description of what is shown on screen and the related `presence:` log lines.

With these three inputs, software-side fixes can be applied quickly.
