# Smart Clock (ESP-IDF)

This repository contains the firmware for an `ESP32-S3` smart clock project built on `ESP-IDF`.

The current codebase is no longer just a basic bring-up scaffold. Core display, sensing, Todo/config sync, audio playback, local settings, status reporting, and page interaction paths are already running in the firmware.

## 1. Current Status

### 1.1 Current Visible Effect

The current UI is driven by `LVGL` and organized around these real main pages:

- `HOME`
- `ALARM`
- `TODO`
- `ENV`
- `WIFI`
- `LOW_CLOCK`

The firmware already supports:

- time and date display
- local alarm browsing and editing
- Todo list display and Todo item actions
- environment data display
- network status display
- low-disturbance clock page when the user is away

### 1.2 Current Functional State

Already implemented:

- `ESP-IDF` project structure and module lifecycle wiring
- LCD / backlight / key input
- local settings model + NVS persistence
- `BH1750` light sensing
- `DHT11` temperature / humidity reading
- `LD2410C` presence detection path
- WAV-based audio playback through `I2S`
- Wi-Fi STA connection
- `SNTP` time sync
- Todo HTTP sync, local Todo cache, Todo item operations
- device configuration pull, status report, and event report paths

Partially implemented:

- `DHT11` still has stability limits
- Todo JSON parsing is still lightweight string-based parsing
- network target addressing still depends on configured host / IP

Not yet fully mature:

- stronger network environment adaptation
- more robust Todo protocol parsing
- more complete reminder strategy refinement
- product-level runtime hardening

## 2. Directory Guide

### `src/`

ESP-IDF application component, bootstrap, module orchestration, and feature modules.

- `src/main.c`
- `src/app_boot.c`
- `src/app_module.c`
- `src/modules/interaction/`
  - display, backlight, input, UI model
- `src/modules/sensing/`
  - environment sampling, presence detection
- `src/modules/connectivity/`
  - Wi-Fi, `SNTP`, config sync, status report, event report
- `src/modules/audio/`
  - WAV playback through `I2S`
- `src/modules/config/`
  - settings and persistence
- `src/modules/core/`
  - lifecycle / fault / timebase scaffolding
- `src/modules/reminder/`
  - reminder triggering logic

### `include/`

Public headers and shared configuration.

Key files:

- `include/hw_config.h`
- `include/app_config.h`
- `include/net_service.h`
- `include/settings_model.h`

### `assets/`

Source assets used by the firmware and documentation.

- audio clips under `assets/audio/`

### `docs/`

Design, specification, API handoff, hardware notes, and reference material.

## 3. Build And Run

### 3.1 Prerequisites

- `ESP-IDF 6.0.1`
- target board: `ESP32-S3`
- local serial access to the board

### 3.2 Build

```bash
. /path/to/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
```

### 3.3 Flash And Monitor

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Replace the serial port with your local device.

## 4. Current Runtime Notes

### Time Sync

Time sync currently uses:

- `SNTP`

Behavior:

- sync once after Wi-Fi gets IP
- periodically re-sync later
- local timezone is configured in firmware

### Todo Sync

Todo/config sync currently works by:

- HTTP pull from the configured Web endpoint
- local cache replacement after successful parse
- NVS cache persistence
- manual sync trigger support
- device-side Todo done / delete operations through HTTP
- device-side alarm / voice setting changes immediately request Web-side source updates

Current sync topology includes:

- configuration pull: `Web -> device`
- status report: `device -> Web`
- event report: `device -> Web`

Current default sync intervals:

- configuration pull: `30s`
- status report: `10s`
- event report: immediate on event

Todo semantics:

- the device only stores and displays unfinished Todo items
- completed Todo is archived on the Web side and is not pulled back to the device
- deleted Todo is permanently removed on the Web side

### Audio

Audio playback currently uses:

- embedded `WAV`
- `PCM`
- `I2S`
- `MAX98357A`

Current event types include:

- welcome
- Wi-Fi connected
- Todo sync-up
- environment alerts
- rest reminder
- alarm reminder

### Presence And Low Clock

Presence uses `LD2410C` with:

- UART data
- `OUT` pin fallback

The `LOW_CLOCK` page is the current low-disturbance display mode. It is entered and exited based on configured presence timing thresholds.

## 5. Local Configuration

Main runtime config lives in:

- `include/app_config.h`

This currently includes:

- Wi-Fi SSID / password
- SNTP server
- Web host / port
- Todo API path
- device config/status/events API paths

Pin mapping lives in:

- `include/hw_config.h`

## 6. Handoff Notes

When reporting runtime issues, the most useful inputs are:

1. first `idf.py build` error block
2. serial boot / runtime logs
3. screen behavior description
4. wiring differences relative to `hw_config.h`

This repo is best understood as a working embedded prototype with the main user-facing paths already implemented, not just a hardware bring-up snapshot.
