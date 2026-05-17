# Supervisor Architecture Freeze — Story Lifecycle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Execute story lifecycle actions from `docs/superpowers/specs/2026-05-17-supervisor-architecture-freeze.md` §2 — cancel 3 stories, merge 2 stories, create 1 replacement story.

**Architecture:** This is a pure file-management plan. No code changes. Each task edits a story's status line and moves it between `open/` and `cancelled/` directories. New story files are written to `open/`.

**Tech Stack:** Bash (`mv`, `sed`), file writes.

---

## File Map

| Operation | Source | Destination |
|-----------|--------|-------------|
| Cancel | `open/US-0032.md` | `cancelled/US-0032.md` |
| Cancel | `open/US-0037.md` | `cancelled/US-0037.md` |
| Cancel (superseded) | `open/US-0033.md` | `cancelled/US-0033.md` |
| Merge | `open/US-0011.md` → new `open/US-0043.md` | `cancelled/US-0011.md`, `cancelled/US-0018.md` |
| Replace | new `open/US-0044.md` | — |

---

### Task 1: Cancel US-0032

**Files:**
- Modify: `requirements/user-stories/open/US-0032.md`
- Move: `requirements/user-stories/open/US-0032.md` → `requirements/user-stories/cancelled/US-0032.md`

- [ ] **Step 1: Update status line to cancelled**

Replace the status line in `requirements/user-stories/open/US-0032.md` from:
```
> **Status:** To Do | **Priority:** Medium | **Created:** 2026-05-08
```
to:
```
> **Status:** Cancelled — V2's `postStateRequest(target)` + `postErrorEvent(reason, source)` already provide a clean request/error API without a formal SystemEvent enum. A 2-event enum wrapper adds ceremony without changing behavior. | **Priority:** Medium | **Created:** 2026-05-08 | **Resolved:** 2026-05-17
```

- [ ] **Step 2: Move file to cancelled/**

```bash
mv requirements/user-stories/open/US-0032.md requirements/user-stories/cancelled/US-0032.md
```

- [ ] **Step 3: Verify the move**

```bash
ls requirements/user-stories/cancelled/US-0032.md
```
Expected: file exists at cancelled path.

```bash
ls requirements/user-stories/open/US-0032.md
```
Expected: "No such file or directory"

- [ ] **Step 4: Commit**

```bash
git add requirements/user-stories/cancelled/US-0032.md requirements/user-stories/open/US-0032.md
git commit -m "US-0032: cancel — V2 already has clean request/error API"
```

---

### Task 2: Cancel US-0037

**Files:**
- Modify: `requirements/user-stories/open/US-0037.md`
- Move: `requirements/user-stories/open/US-0037.md` → `requirements/user-stories/cancelled/US-0037.md`

- [ ] **Step 1: Update status line to cancelled**

Replace the status line in `requirements/user-stories/open/US-0037.md` from:
```
> **Status:** To Do | **Priority:** Medium | **Created:** 2026-05-09
```
to:
```
> **Status:** Cancelled — V2 already has rank-based getNextState(), separate targetState_/observedState_, immediate ERROR/FATAL handling, stepTowardTarget() with orchestration continuation, and last-write-wins mailbox. Remaining gaps are cosmetic naming or intentionally left to caller discipline. | **Priority:** Medium | **Created:** 2026-05-09 | **Resolved:** 2026-05-17
```

- [ ] **Step 2: Move file to cancelled/**

```bash
mv requirements/user-stories/open/US-0037.md requirements/user-stories/cancelled/US-0037.md
```

- [ ] **Step 3: Verify the move**

```bash
ls requirements/user-stories/cancelled/US-0037.md && ls requirements/user-stories/open/US-0037.md
```
Expected: cancelled/ exists, open/ does not.

- [ ] **Step 4: Commit**

```bash
git add requirements/user-stories/cancelled/US-0037.md requirements/user-stories/open/US-0037.md
git commit -m "US-0037: cancel — V2 already has algorithmic step-through"
```

---

### Task 3: Cancel US-0033 (superseded)

**Files:**
- Modify: `requirements/user-stories/open/US-0033.md`
- Move: `requirements/user-stories/open/US-0033.md` → `requirements/user-stories/cancelled/US-0033.md`

- [ ] **Step 1: Update status line to cancelled (superseded)**

Replace the status line in `requirements/user-stories/open/US-0033.md` from:
```
> **Status:** To Do | **Priority:** High | **Created:** 2026-05-09
```
to:
```
> **Status:** Cancelled — superseded by US-0044. V2 satisfies the spec's core requirements (RetryPolicy, DEGRADED, single-slot Mailbox, ErrorEvent flag+payload, Core 0 exclusivity, per-component required/optional). Remaining structural gaps captured in US-0044. | **Priority:** High | **Created:** 2026-05-09 | **Resolved:** 2026-05-17
```

- [ ] **Step 2: Move file to cancelled/**

```bash
mv requirements/user-stories/open/US-0033.md requirements/user-stories/cancelled/US-0033.md
```

- [ ] **Step 3: Verify the move**

```bash
ls requirements/user-stories/cancelled/US-0033.md && ls requirements/user-stories/open/US-0033.md
```
Expected: cancelled/ exists, open/ does not.

- [ ] **Step 4: Commit**

```bash
git add requirements/user-stories/cancelled/US-0033.md requirements/user-stories/open/US-0033.md
git commit -m "US-0033: cancel superseded by US-0044"
```

---

### Task 4: Merge US-0011 + US-0018 → US-0043

**Files:**
- Create: `requirements/user-stories/open/US-0043.md`
- Modify: `requirements/user-stories/open/US-0011.md`
- Modify: `requirements/user-stories/open/US-0018.md`
- Move: `requirements/user-stories/open/US-0011.md` → `requirements/user-stories/cancelled/US-0011.md`
- Move: `requirements/user-stories/open/US-0018.md` → `requirements/user-stories/cancelled/US-0018.md`

- [ ] **Step 1: Write the merged story US-0043**

Write `requirements/user-stories/open/US-0043.md`:

```markdown
# US-0043: Logging Cleanup — Hygiene and Unified State Transition Format

> **Status:** To Do | **Priority:** Medium | **Created:** 2026-03-28 | **Merged:** 2026-05-17 (supersedes US-0011, US-0018)

## User Story
As a **developer**, I want **application logging to be readable, policy-compliant, and consistently formatted for state transitions**, so that **important state and error information is visible without avoidable noise and I can trace transitions easily**.

## Acceptance Criteria

### Logging Hygiene (from US-0011)
- [ ] Third-party informational callbacks are mapped to `DEBUG_LOG` (not `PROD_LOG`) unless they are hard errors
- [ ] Direct `Serial.print*` usage in runtime logic is replaced by project logging macros
- [ ] State transition logging is not duplicated across multiple layers unless each log has a distinct diagnostic purpose
- [ ] Architecture decision applied: transition logs are owned by the Supervisor; duplicate observer logs are removed unless they add unique context
- [ ] Logging behavior complies with `requirements/guidelines/debug.md` (tiers, throttling, no noisy continuous output)

### Unified State Transition Logging (from US-0018)
- [ ] Log message format defined: `[TransitionId] Component <name>: <TransitionSubState> (<ComponentStatus if relevant>, <debugReason if Failed>)`
- [ ] Logs emitted at key points: start of component.setXXX(), each reportCompletion(), timeout event, optional-component DEGRADED marking, final commit
- [ ] Example log trace for "BOOTING → CONNECTING → READY → LIVE" transition visible in serial monitor
- [ ] Debug text (debugReason) is static const char* (never dynamic strings)
- [ ] Successful completions require no extra debug output in normal flow (clean logs)
- [ ] Failed transitions always log debug reason (no silent failures)
- [ ] Use existing DEBUG_LOG, PROD_LOG, ERROR_LOG macros (no new logging framework)
- [ ] transitionId is printed in all logs for correlation

### Validation
- [ ] Debug build compiles successfully after changes (no regression)
- [ ] Native unit tests pass after changes (no regression)
- [ ] Test: capture full log of "BOOTING → CONNECTING → READY → LIVE → READY → SLEEP" and verify logs are complete and readable

## Notes
- Merged from US-0011 and US-0018 per `docs/superpowers/specs/2026-05-17-supervisor-architecture-freeze.md`
- Specific findings from US-0011 investigation:
  - `audio_info`, `audio_showstation`, `audio_showstreamtitle`, `audio_bitrate` use `PROD_LOG` in `src/audio_callbacks.cpp` — should be `DEBUG_LOG`
  - `wifi_manager::connect()` uses direct `Serial.print('.')` — bypasses logging tiers
  - State transitions logged in both Supervisor and CliComponent — consolidate in Supervisor
- Logs should tell the story of what happened during transition without being verbose
- No timestamp duplication (ESP32 framework already adds millis-like markers)

## Related
- Guidelines: `requirements/guidelines/debug.md`
- Guidelines: `requirements/guidelines/state-management.md`
- Stories: `US-0016`, `US-0017`
- Spec: `docs/superpowers/specs/2026-05-17-supervisor-architecture-freeze.md`
```

- [ ] **Step 2: Update US-0011 status to cancelled (superseded)**

Replace the status line in `requirements/user-stories/open/US-0011.md` from:
```
> **Status:** To Do  
> **Priority:** Medium  
> **Created:** 2026-03-28
```
to:
```
> **Status:** Cancelled — superseded by US-0043 (merged with US-0018) | **Priority:** Medium | **Created:** 2026-03-28 | **Resolved:** 2026-05-17
```

- [ ] **Step 3: Update US-0018 status to cancelled (superseded)**

Replace the status line in `requirements/user-stories/open/US-0018.md` from:
```
> **Status:** To Do  
> **Priority:** Medium  
> **Created:** 2026-03-29
```
to:
```
> **Status:** Cancelled — superseded by US-0043 (merged with US-0011) | **Priority:** Medium | **Created:** 2026-03-29 | **Resolved:** 2026-05-17
```

- [ ] **Step 4: Move both originals to cancelled/**

```bash
mv requirements/user-stories/open/US-0011.md requirements/user-stories/cancelled/US-0011.md
mv requirements/user-stories/open/US-0018.md requirements/user-stories/cancelled/US-0018.md
```

- [ ] **Step 5: Verify**

```bash
ls requirements/user-stories/open/US-0043.md requirements/user-stories/cancelled/US-0011.md requirements/user-stories/cancelled/US-0018.md
```
Expected: all three files exist.

- [ ] **Step 6: Commit**

```bash
git add requirements/user-stories/open/US-0043.md requirements/user-stories/cancelled/US-0011.md requirements/user-stories/cancelled/US-0018.md requirements/user-stories/open/US-0011.md requirements/user-stories/open/US-0018.md
git commit -m "US-0011, US-0018: merge into US-0043 logging cleanup"
```

---

### Task 5: Replacement Story for US-0033 → US-0044

**Files:**
- Create: `requirements/user-stories/open/US-0044.md`

- [ ] **Step 1: Write replacement story US-0044**

Write `requirements/user-stories/open/US-0044.md`:

```markdown
# US-0044: Supervisor Pattern — Remaining Structural Gaps

> **Status:** To Do | **Priority:** Medium | **Created:** 2026-05-17 | **Supersedes:** US-0033

## User Story
As a **maintainer**, I want **the remaining structural gaps from the state-management guideline to be implemented in SupervisorV2**, so that **the supervisor contract fully aligns with the specified pattern for component state ranges, absent-component handling, and SLEEP sequencing**.

## Acceptance Criteria

### Component Min/Max State Matrix
- [ ] Per-component table indexed by `systemState` defines valid state range `{ minState, maxState }` for each component
- [ ] On state transitions, supervisor writes target state range (not exact target) to component mailboxes
- [ ] Enables the lazy/busy transition mode optimization (US-0041)

### Absent-Component Handling
- [ ] `optional` components not present at boot (null mailbox pointer) are treated as permanently `DEGRADED`
- [ ] `required` components not present at boot trigger immediate ERROR recovery
- [ ] Absence check runs during first orchestration (already hooked via `checkComponentPresence()`)

### SLEEP Contract
- [ ] `esp_deep_sleep_start()` called only after all components report COMMITTED for SLEEP
- [ ] V2 orchestration already ensures sequential completion; gap is ensuring the deep sleep trigger fires at the correct point in `run()` or `checkOrchestrationResponse()`

### Validation
- [ ] Debug and production builds succeed
- [ ] Native unit tests pass

## Notes
- Replaces US-0033 (pattern compliance audit) per `docs/superpowers/specs/2026-05-17-supervisor-architecture-freeze.md` §2.3
- SubState enum (PENDING/COMMITTED/FAILED) was intentionally removed in commit `dbaccbf` as dead code — not included here
- Min/max matrix is a prerequisite for US-0041 (lazy/busy mode)

## Related
- Spec: `docs/superpowers/specs/2026-05-17-supervisor-architecture-freeze.md` §2.3
- Guidelines: `requirements/guidelines/state-management.md`
- Stories: `US-0041` (lazy/busy — depends on min/max matrix)
```

- [ ] **Step 2: Verify the file exists**

```bash
ls requirements/user-stories/open/US-0044.md
```
Expected: file exists.

- [ ] **Step 3: Commit**

```bash
git add requirements/user-stories/open/US-0044.md
git commit -m "US-0033: replace with US-0044 structural gaps story"
```
