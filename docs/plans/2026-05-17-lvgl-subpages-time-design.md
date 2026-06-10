# LVGL Subpages And Time Source Design

## Goal

Improve the current LVGL prototype in two ways:

1. replace the uptime-style home time with a more stable page-return-safe time source
2. replace placeholder page switching with actual static LVGL page skeletons

## Scope

This round is limited to `src/modules/interaction/display_service.c`.
No full multi-page business state machine is introduced.

## Home Page Revisions

### Time Source

- prefer system time from `time()` and `localtime_r()`
- if system time is still invalid, fall back to uptime-derived placeholder time

This avoids the current "time looks reset after returning home" problem.

### Tips Row

- remove the `Tips` title label
- keep only the tip content
- render the tip content in plain white text

## Static Subpages

Add four static LVGL page skeletons:

1. `SETTINGS`
2. `ALARM`
3. `NETWORK`
4. `POWER OFF?`

These pages are visual skeletons only.
They do not yet implement the full per-page interaction model from the design docs.

## Navigation Scope

- on home page:
  - `K1 -> SETTINGS`
  - `K2 -> ALARM`
  - `K3 -> NETWORK`
  - `K4 -> POWER`
- on subpages:
  - `K4 -> HOME`

## Non-Goals

- no full settings editor
- no full alarm editor
- no full network module focus state machine
- no real todo/alarm data integration on subpages
