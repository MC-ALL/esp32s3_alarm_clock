# DHT11 Replacement And Display Flicker Design

## Goal

Handle two persistent bring-up issues:

1. Replace the fragile local DHT11 bit-bang path with a more stable driver route.
2. Reduce visible display flicker by tightening the software refresh path.

## Scope

This round affects:

- `modules/sensing/environment_service.c`
- `modules/interaction/display_service.c`
- related public headers only if required

## DHT11 Direction

The current local DHT11 implementation is timing-sensitive and repeatedly fails at the handshake stage.
Instead of continuing to tune the manual bit-bang path, the preferred direction is:

1. adopt a stable community or official-style DHT11 / onewire implementation
2. keep the current environment snapshot interface unchanged
3. preserve BH1750 logic as-is

This keeps the external data contract stable while reducing low-level timing risk.

## Display Flicker Direction

The current display path still updates too aggressively for a small SPI panel.
The preferred direction is:

1. reduce periodic LVGL handling pressure
2. refresh the home screen only when content changes
3. avoid unnecessary text writes every task loop
4. only investigate deeper DMA completion semantics if software throttling is not enough

## Non-Goals

- No multi-page UI redesign
- No new visual design work
- No hardware rewiring changes in this round
