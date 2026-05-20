# Clock Web Design

## 1. Goal

This document defines the lightweight LAN web design for editing `Todo List` data used by the smart clock.

### 1.1 Why This Web Exists

The smart clock currently uses four physical keys as its only reliable local input path.
That input model is acceptable for:
- page switching
- toggles
- short numeric editing
- simple confirmations

It is not a good fit for long-form text editing.
`Todo List` content is fundamentally text-heavy, and forcing users to enter or edit Todo text on-device would create a poor interaction experience and significantly increase firmware-side UI complexity.

Because of that, the project deliberately separates responsibilities:
- text editing happens on a LAN web page running on the user's own computer
- the smart clock acts as a lightweight display-and-sync client

This gives the project a practical path to ship Todo editing without overloading the clock UI with a text-entry workflow.

This repository does **not** implement the web service itself.
It only provides the architecture, interface contract, and integration expectations for the external web project.

The target direction is:
- run the web service on the user's own computer
- expose a simple browser UI for Todo editing
- persist data in a local JSON file
- let the smart clock fetch Todo data over LAN using HTTP

## 2. Scope

In scope:
- LAN deployment model
- backend and frontend responsibility split
- Todo data model
- HTTP API design
- local JSON persistence design
- smart clock pull-sync expectations
- failure handling expectations

Out of scope:
- implementation in this repository
- database design
- cloud sync
- account/login system
- public internet access
- real-time push/WebSocket sync

## 3. Recommended Stack

Recommended lightweight stack:
- Backend: `Python + FastAPI`
- Frontend: simple server-rendered or static page served by FastAPI
- Persistence: local `JSON` file

Recommended reason:
- no database required
- simple local deployment
- easy debugging
- easy future migration to SQLite if needed

## 4. Deployment Model

### 4.0 Expected Role Split

The web service is expected to be the Todo source of truth.
The clock is expected to:
- periodically fetch Todo data
- cache the last successful result locally
- render a compact Todo summary on screen
- later consume the same data for reminder logic if needed

The web service is expected to:
- provide the editable Todo management UI
- validate and persist Todo data
- expose a stable HTTP contract for the clock

### 4.1 Runtime Placement

The web service runs on the user's own computer.
The smart clock and the computer must be on the same LAN.

Example:
- computer IP: `192.168.1.100`
- service port: `8080`
- clock fetch URL: `http://192.168.1.100:8080/api/todos`

### 4.2 Service Availability Assumption

This design assumes:
- the user's computer is powered on when Todo sync is expected
- the service process stays running while the clock needs to sync
- the clock and the computer remain on the same LAN segment

### 4.3 Addressing Assumption

The firmware side should not depend on discovery for the first version.
The simplest first version is:
- the web service binds to a fixed port such as `8080`
- the user configures the computer LAN IP into the clock firmware or a future settings entry
- the clock fetches from a fixed URL

Recommended first-version URL form:
- `http://<pc-lan-ip>:8080/api/todos`

Future extensions such as mDNS/service discovery may be added later, but are intentionally excluded from the first version.

This design assumes:
- the user's computer is powered on when Todo sync is expected
- the service process stays running while the clock needs to sync

If the service is down:
- the clock keeps the last successful local Todo cache
- the clock reports sync failure in UI/logs
- the web side does not need to support offline queueing for the clock

## 5. Architecture

### 5.1 Split

The web project should contain two parts:
- Backend service
- Browser UI

Backend responsibilities:
- read/write `todos.json`
- expose HTTP API
- validate input data
- keep Todo ordering stable

Frontend responsibilities:
- render Todo editor UI
- call backend API
- provide simple, low-friction editing flow

Clock responsibilities:
- periodically fetch Todo list
- cache latest successful result locally
- display the first few items on screen
- optionally trigger reminder logic later

### 5.2 Sync Direction

Current recommended sync direction is one-way:
- Web -> Clock

Meaning:
- the web service is the source of truth
- the clock does not edit Todo text
- the clock only fetches and consumes Todo data

### 5.3 Why Web Should Be The Source Of Truth

This project explicitly prefers the web service as the Todo source of truth instead of using the clock as the Todo storage owner.

Reasoning:
- Todo editing is text-heavy and browser-side editing is much more usable than on-device key-based editing
- the clock firmware should stay focused on network fetch, cache, display, and reminder behavior
- letting the clock expose editable Todo APIs would force the firmware to become a LAN server, which increases complexity and maintenance cost
- a single local JSON-backed web service is enough for the current LAN-only product phase

Expected role split:
- Web side: edit, validate, persist, expose API
- Clock side: fetch, cache, display, later remind

## 6. Data Model

Use a minimal model first.

```json
{
  "id": "todo-001",
  "text": "10:00 前确认会议纪要并发给项目组",
  "done": false,
  "updated_at": "2026-05-19T10:30:00+08:00"
}
```

Field meanings:
- `id`: stable unique identifier, string
- `text`: Todo content, string
- `done`: whether the item is completed
- `updated_at`: ISO-8601 timestamp string

Recommended first-version constraints:
- `id`: backend-generated, not editable by UI
- `text`: max `96` visible characters recommended
- `done`: boolean
- `updated_at`: backend-managed

Optional future fields, not required now:
- `priority`
- `due_at`
- `category`
- `repeat_rule`

## 7. Local Storage

### 7.1 Storage Path

Recommended path in the web project:
- `data/todos.json`

### 7.2 File Format

Store as a JSON array.

Example:

```json
[
  {
    "id": "todo-001",
    "text": "10:00 前确认会议纪要并发给项目组",
    "done": false,
    "updated_at": "2026-05-19T10:30:00+08:00"
  },
  {
    "id": "todo-002",
    "text": "记得取快递",
    "done": true,
    "updated_at": "2026-05-19T10:32:00+08:00"
  }
]
```

### 7.3 Write Strategy

Recommended:
- read full file into memory
- modify in memory
- write to a temp file
- rename temp file to `todos.json`

This avoids partial-write corruption.

## 8. API Design

Base path:
- `/api/todos`

### 8.1 Get All Todos

`GET /api/todos`

Response `200`:

```json
{
  "items": [
    {
      "id": "todo-001",
      "text": "10:00 前确认会议纪要并发给项目组",
      "done": false,
      "updated_at": "2026-05-19T10:30:00+08:00"
    }
  ],
  "updated_at": "2026-05-19T10:35:00+08:00"
}
```

Notes:
- `items` order is meaningful
- order should match UI order
- the clock should consume `items` only

### 8.2 Create Todo

`POST /api/todos`

Request:

```json
{
  "text": "记得给项目组发周报"
}
```

Response `201`:

```json
{
  "id": "todo-003",
  "text": "记得给项目组发周报",
  "done": false,
  "updated_at": "2026-05-19T10:40:00+08:00"
}
```

### 8.3 Update Todo

`PUT /api/todos/{id}`

Request:

```json
{
  "text": "记得给项目组发最终版周报",
  "done": true
}
```

Response `200`:

```json
{
  "id": "todo-003",
  "text": "记得给项目组发最终版周报",
  "done": true,
  "updated_at": "2026-05-19T10:45:00+08:00"
}
```

### 8.4 Delete Todo

`DELETE /api/todos/{id}`

Response `204`

### 8.5 Reorder Todos

Recommended first version supports explicit order update.

`PUT /api/todos/reorder`

Request:

```json
{
  "ids": ["todo-002", "todo-001", "todo-003"]
}
```

Response `200`:

```json
{
  "ok": true,
  "updated_at": "2026-05-19T10:50:00+08:00"
}
```

If you want the simplest possible first version, this endpoint may be deferred.
But the frontend UI should still preserve stable order locally.

## 9. Frontend UI Design

The UI should be simple and fast to use.
The web page is not meant to be a general project-management app.
It is a focused local editor for the clock's Todo data.

### 9.1 UX Goal

The ideal first-version user experience is:
- open page
- immediately see current Todo list
- quickly add, edit, reorder, complete, or delete items
- save is either automatic or one-click obvious
- no hidden workflow

### 9.2 Recommended Page Sections

Recommended page sections:
- header
- create input row
- todo list
- item action row
- save/sync status hint

### 9.3 Recommended Behavior

Recommended behavior:
- create Todo with one input + submit button
- inline edit existing Todo text
- checkbox or toggle for `done`
- delete button per item
- drag-sort optional; if omitted, provide up/down buttons
- keep ordering stable after refresh

### 9.4 Recommended Interaction Priority

Priority of implementation:
1. list current items
2. add item
3. edit text
4. mark done / undone
5. delete item
6. reorder items

### 9.5 Visual Direction

Recommended visual direction:
- clean list editor
- no heavy design system
- desktop-first, but mobile browser usable
- low interaction cost
- readable dense layout is preferred over decorative design

## 10. Clock-Side Integration Expectations

### 10.1 Pull Strategy

Recommended clock polling interval:
- `30s` to `60s`

First version recommendation:
- `60s`

Recommended clock-side pull behavior:
- do not poll faster than `30s`
- use a finite HTTP timeout, recommended `3s` to `5s`
- if one sync is still in flight, do not start a second one
- if fetch fails, keep the previous cache untouched

### 10.2 Parse Contract

Clock expects:
- valid JSON response
- top-level `items`
- each item contains at least `id`, `text`, `done`, `updated_at`
- item order is already final display order from the web source

The clock should not attempt to re-sort Todo items in the first version.

### 10.3 Clock Display Rule

Recommended display rule on the clock:
- show top `3` Todo items on home screen
- if more items exist, show `+N`
- if sync fails, keep the previous cache
- completed items may either remain in order or be filtered by the web service; the first version should avoid duplicate sorting policy on both ends

### 10.4 Error Rule

If fetch fails:
- log sync error
- keep last successful Todo cache
- do not clear UI immediately
- expose a readable sync status to the network page or diagnostics path

Recommended first-version clock-side sync state fields:
- `last_sync_ok`
- `last_sync_at`
- `last_sync_error`

## 11. Validation Rules

Backend should validate:
- `text` must be non-empty after trim
- `text` should not exceed reasonable size
- `done` must be boolean
- `id` must exist for update/delete
- reorder `ids` must match known Todo ids

Recommended response codes:
- `200` success
- `201` created
- `204` deleted
- `400` invalid request
- `404` item not found
- `500` local storage failure

## 12. Security Assumptions

This is LAN-only first version.

Recommended minimum:
- bind to local network interface intentionally
- do not expose to public internet
- no authentication required in first version if only used in trusted LAN

Optional future hardening:
- simple token
- IP allowlist
- local HTTPS reverse proxy

## 13. Future Extensions

Possible future upgrades:
- SQLite storage
- due date field
- priority field
- push updates to clock
- multi-device editing
- public internet sync
- clock-side simple done/clear operations

## 14. Firmware-Side Integration Notes

This section is for the firmware-side implementer.

Recommended first-version firmware behavior:
- store a fixed Todo endpoint URL in config
- periodically issue `GET /api/todos`
- parse top-level `items`
- replace local Todo cache only after a full successful parse
- keep previous cache on transport or parse failure
- display top `3` Todo items on the home screen
- show `+N` if more items exist
- treat the web service as authoritative ordering and authoritative edit source

### 14.1 Compatibility With Current Firmware Direction

This integration model fits the current firmware direction well because:
- Wi-Fi and fixed-hotspot logic already exist
- the clock is already moving toward network pull and local cache behavior
- Todo editing is intentionally not pushed into the 4-key local UI
- the web service can evolve independently from the firmware repository

Recommended first-version HTTP expectations:
- method: `GET`
- content type: `application/json`
- response body small enough for embedded parsing
- no authentication
- no compression required

Not recommended for first version:
- clock-side Todo mutation APIs
- push sync
- websocket session handling
- conflict resolution

## 15. Summary

Recommended first version:
- `FastAPI` backend
- browser-based lightweight Todo editor
- local `data/todos.json` persistence
- LAN HTTP polling by the clock
- minimal Todo schema
- web service is the Todo source of truth
- no database
- no cloud
- no account system
- no clock-side text editing

This gives the project a practical, low-cost path to ship Todo editing without forcing text entry onto the clock device itself, while keeping the firmware aligned with its current strengths: fetch, cache, display, and reminder behavior.


## 16. Backend Implementation Breakdown

Recommended backend module split:
- `app.py`
  - FastAPI entry
  - router mounting
  - static page serving
- `models.py`
  - request / response schema definitions
- `storage.py`
  - read/write `todos.json`
  - atomic write helper
- `service.py`
  - Todo list business operations
  - id generation
  - validation and normalization
- `web/`
  - frontend page assets or templates
- `data/todos.json`
  - Todo persistence file

Recommended backend startup behavior:
- ensure `data/` exists
- if `data/todos.json` does not exist, create it with `[]`
- reject malformed file content with explicit startup error log
- do not silently discard corrupted Todo data

## 17. Frontend Interaction Flow

Recommended first-version interaction flow:

### 17.1 Page Load
- browser opens the page
- frontend requests `GET /api/todos`
- frontend renders Todo list in returned order
- if load fails, show explicit error banner and retry button

### 17.2 Add Todo
- user types text in create input
- user clicks add button or presses enter
- frontend sends `POST /api/todos`
- backend returns created item
- frontend appends or refreshes list from response

### 17.3 Edit Todo
- user edits Todo text inline
- frontend sends `PUT /api/todos/{id}`
- backend updates `updated_at`
- frontend refreshes row or full list

### 17.4 Toggle Done
- user checks or unchecks done state
- frontend sends `PUT /api/todos/{id}` with `done`
- backend updates `updated_at`
- frontend updates row state

### 17.5 Delete Todo
- user clicks delete
- frontend sends `DELETE /api/todos/{id}`
- frontend removes item from list after success

### 17.6 Reorder Todo
- if drag-sort is implemented, frontend collects final ordered ids
- frontend sends `PUT /api/todos/reorder`
- backend rewrites stored order exactly as provided

## 18. JSON Contract Details

Recommended top-level response for `GET /api/todos`:

```json
{
  "items": [
    {
      "id": "todo-001",
      "text": "10:00 前确认会议纪要并发给项目组",
      "done": false,
      "updated_at": "2026-05-19T10:30:00+08:00"
    }
  ],
  "updated_at": "2026-05-19T10:35:00+08:00",
  "version": 1
}
```

Field meanings:
- `items`: ordered Todo array
- `updated_at`: last backend update time for the whole collection
- `version`: schema version for future extension

Recommended request constraints:
- reject blank `text`
- trim leading/trailing whitespace
- preserve internal whitespace
- keep UTF-8 text as-is
- reject overlong payloads with `400`

## 19. Clock-Side Pull Design

This section describes the intended firmware-side behavior once implementation starts.

### 19.1 Fixed Endpoint Configuration

Recommended first-version firmware config values:
- `TODO_WEB_HOST`
- `TODO_WEB_PORT`
- `TODO_WEB_PATH`

Example effective URL:
- `http://192.168.1.100:8080/api/todos`

This should be a fixed configuration in the first version.
Dynamic discovery is intentionally out of scope.

### 19.2 Polling Loop

Recommended behavior:
- start polling only after Wi-Fi is connected
- run every `60s`
- use `3s` to `5s` request timeout
- skip starting a new sync if previous sync is still active

Pseudo-flow:
- check Wi-Fi connected
- issue HTTP GET
- if response code is not `200`, record failure
- parse JSON
- validate all required fields
- only after full successful parse, replace local cache
- update `last_sync_ok`, `last_sync_at`

### 19.3 Cache Replacement Rule

Recommended rule:
- never partially mutate the active cache while parsing
- parse into a temporary structure first
- if and only if parse succeeds, replace active Todo cache in one step

This avoids inconsistent UI state if the payload is malformed.

### 19.4 Clock Display Mapping

Recommended home-screen mapping:
- show the first `3` Todo items in returned order
- if total count > 3, show `+N`
- if no items, show an explicit empty state

Recommended first-version rule for `done` items:
- simplest choice: backend filters them out before returning
- alternative: clock receives all items and locally skips done items

Preferred option:
- backend returns only items that should appear on the clock home screen

This keeps firmware logic lighter.

## 20. Failure Handling Design

### 20.1 Backend Failures

If `todos.json` cannot be read:
- return `500`
- include short human-readable error message
- do not return a fake empty list silently

If write fails:
- return `500`
- keep original file intact if temp-write strategy fails before rename

### 20.2 Frontend Failures

Frontend should show clear user-facing status for:
- loading failed
- save failed
- delete failed
- reorder failed

Recommended UX rule:
- do not hide errors in console only
- always surface a visible status banner or message line

### 20.3 Clock Failures

If clock fetch fails:
- keep previous Todo cache
- update sync state to failure
- do not erase home-screen Todo list immediately
- expose last failure reason in logs and optionally network page later

## 21. Non-Goals For First Version

The following should not block first implementation:
- multi-user editing
- merge/conflict resolution
- websocket push
- authentication
- remote/public internet access
- due-date reminder scheduling
- category filtering
- device-originated Todo edits

## 22. Recommended Implementation Order

Recommended order for the web-side implementer:
1. create backend project skeleton
2. implement `todos.json` read/write helpers
3. implement `GET /api/todos`
4. implement create / update / delete APIs
5. implement minimal web page
6. implement reorder behavior
7. test with real LAN IP and real browser
8. hand off fixed API URL and sample payload to firmware side

Recommended order for firmware-side implementer after web is ready:
1. add fixed endpoint config
2. add HTTP GET polling task/path
3. add JSON parse and temporary cache replacement
4. bind cache to home-screen Todo display
5. expose sync state in network diagnostics

## 23. Acceptance Criteria

This design should be considered implemented correctly when all of the following are true:
- the web page can add, edit, delete, and reorder Todo items
- Todo data persists across backend restart
- `GET /api/todos` returns stable ordered JSON
- the clock can fetch the Todo list over LAN using a fixed URL
- fetch failure does not erase the previous on-device Todo cache
- the top `3` items display correctly on the clock home screen
- no database is required
- no clock-side text editing is required

## 24. Handoff Notes

Another engineer should be able to start implementation directly from this document by following these assumptions:
- use `FastAPI`
- store Todo data in `data/todos.json`
- treat the web service as the Todo source of truth
- provide the API exactly as described unless an implementation constraint forces a change
- if the API shape changes, update this document before handing it to firmware integration
