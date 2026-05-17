# ESP-IDF 6.0.1 Adaptation Design

## Goal

Adapt the current firmware codebase to `ESP-IDF 6.0.1`.
This round targets build compatibility first. It does not expand feature scope or redesign module boundaries.

## Scope

The adaptation scope is limited to these modules:

- `modules/connectivity/net_service.c`
- `modules/interaction/display_service.c`
- `modules/audio/audio_service.c`
- `modules/sensing/environment_service.c`

The target is a clean `ESP-IDF 6.0.1` build path.
Backward compatibility with `ESP-IDF 5.x` is not preserved in this round.

## Strategy

Use the smallest possible source changes:

1. Keep current module boundaries unchanged.
2. Keep public headers and service APIs stable unless build compatibility requires otherwise.
3. Only replace or adjust interfaces that are version-sensitive in `ESP-IDF 6.0.1`.
4. Avoid feature work while doing compatibility work.

## Module Plan

### net_service

- Replace legacy `esp_sntp_*` calls with the `ESP-IDF 6.0.1` SNTP API.
- Keep current Wi-Fi state flow unchanged.
- Review nearby Wi-Fi config fields for version-sensitive usage.

### display_service

- Keep the current minimal `LVGL + esp_lcd + ST7789` path.
- Fix only the parts likely to break against `ESP-IDF 6.0.1` and the bundled `LVGL` version.
- Do not implement the full product UI in this round.

### audio_service

- Keep the current `I2S` test-tone implementation.
- Adjust `I2S std mode` setup only if `ESP-IDF 6.0.1` requires different symbols or fields.

### environment_service

- Keep the current `BH1750 + DHT11` implementation.
- Adjust `I2C master bus` usage only where `ESP-IDF 6.0.1` changed type names or API details.

## Execution Order

1. `net_service`
2. `display_service`
3. `audio_service`
4. `environment_service`

This order clears the already-known SNTP break first, then the most version-sensitive display path, then audio and sensor code.

## Validation

Validation is performed by local `ESP-IDF 6.0.1` build results:

```bash
. /path/to/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
```

When new errors remain, fix them using the first real compiler error, not later cascade errors.

## Non-Goals

- No `ESP-IDF 5.x` compatibility layer
- No hardware behavior redesign
- No full LVGL page implementation
- No networking feature expansion
