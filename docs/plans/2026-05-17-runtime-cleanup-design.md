# Runtime Cleanup Design

## Goal

Clean up non-DHT11 runtime issues observed during bring-up.
This round does not touch sensor wiring or DHT11 timing logic.

## Scope

Two items are handled:

1. Align firmware flash-size configuration with the observed `16 MB` hardware.
2. Reduce misleading Wi-Fi warning noise when credentials are intentionally left empty during bring-up.

## Changes

### Flash Size

- Add explicit flash-size defaults for `ESP-IDF` so the binary image header matches the actual board flash capacity.
- Keep the current partition layout unchanged.

### Wi-Fi Empty Credentials

- When `APP_WIFI_STA_SSID` is empty, treat this as an expected bring-up state.
- Keep Wi-Fi initialization logic unchanged.
- Downgrade the log from warning to info to avoid masking real faults.

## Non-Goals

- No DHT11 driver changes
- No network feature expansion
- No partition layout redesign
