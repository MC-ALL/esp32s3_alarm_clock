# LVGL Home Alignment Design

## Goal

Align the firmware home screen with the existing `index.html` home layout.
This round only targets the home page in `LVGL`.

## Scope

Only `modules/interaction/display_service.c` is changed.
No settings page, alarm page, network page, shutdown page, or input-driven page switching is included.

## Home Layout Target

The `LVGL` home screen is aligned to the `index.html` home structure with these blocks:

1. Top-left time panel
2. Top-right status panel
3. Environment bar
4. Tips row
5. Alarm summary row
6. Todo list panel
7. Bottom key hint row

## LVGL Structure

Use a container-and-label structure only:

- root screen
- top row container
- time panel
- status panel
- environment panel
- tips panel
- alarm panel
- todo panel
- key hint panel

All content is composed using `lv_obj` and `lv_label`.
No custom drawing is introduced in this round.

## Data Strategy

Dynamic content kept in this round:

- current uptime-based time text
- environment snapshot from `environment_service`

Static placeholder content kept in this round:

- sound/audio status
- tips text
- alarm summary
- todo list
- bottom key hints

## Non-Goals

- No full multi-page LVGL UI
- No icon system
- No page navigation state machine
- No real alarm/todo/system-status data model integration
