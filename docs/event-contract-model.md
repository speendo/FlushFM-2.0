# Event Contract Model

**Status:** Updated (US-0025h) | **Updated:** 2026-05-09

## Overview

The Supervisor uses a minimal generic event contract to coordinate state transitions across the system. This document defines:
- Event types and semantics
- Global vs. local state responsibilities
- Mailbox processing model
- Failure handling contract

---

## 1. Core Invariants

### Supervisor as Single Source of Truth (SSOT)

**Global system state** is owned and managed exclusively by Supervisor:
- `BOOTING` → `SLEEP` → `CONNECTING` → `READY` → `LIVE` → `ERROR`
- Only Supervisor can call `setObservedStateImmediate(...)`
- No component or external code may directly modify the global system state
- All state-change intents must go through the Mailbox as `SystemEvent`

### Component Local States as Mirrors

Components may maintain internal lifecycle states that mirror the global guarantees:
- **Component states are implementation details**, not driving logic
- Each SystemState defines guarantees that all components must satisfy
- Components are responsible for their own internal state consistency
- Components never push component-local state logic into Supervisor

**Examples of mirrored local states:**
- **WiFi:** maintains `CONNECTED`/`DISCONNECTED`/`CONNECTING` local lifecycle; mirrors global `READY` guarantee when connected
- **AudioRuntime:** maintains `LIVE`/`SLEEP`/`ERROR` runtime local state; mirrors global `LIVE` guarantee when streaming
- **CLI/BoardInfo:** stateless or trivially ready; always satisfy SLEEP baseline guarantees

**State Guarantee Model:**

| SystemState | Component Guarantees |
| --- | --- |
| `BOOTING` | Start state for all components; WiFi disconnected; Audio may be initializing; components may still initialize themselves |
| `SLEEP` | All components initialized; WiFi disconnected; Audio muted; CLI ready; all components in lowest-power mode |
| `CONNECTING` | WiFi attempting connection; Audio muted; other components at least in SLEEP or above |
| `READY` | WiFi connected; Audio muted; other components satisfy SLEEP baseline guarantees |
| `LIVE` | All components fully active |
| `ERROR` | System in safe/recoverable failure state; all components respect ERROR semantics |

---

## 2. Event Types and Semantics

### Minimal Generic Contract

The Supervisor event interface uses a minimal three-part model:

> **Note:** `STATE_REQUESTED` / `STATE_ENTERED` / `STATE_FAILED` is the contract model. The event enum now contains only two entries: `COMPONENT_SETUP_FAILED` (failure signal) and `STATE_REQUESTED(targetState)` (all state intents). Legacy aliases (`PLAY_REQUESTED`, `STOP_REQUESTED`, `ENTER_SLEEP`) have been removed.

#### `STATE_REQUESTED` (External Intent)
- **Source:** Components, CLI, hardware interrupts (e.g., light sensor)
- **Payload:** Target system state (e.g., `READY`, `LIVE`, `SLEEP`)
- **Semantics:** "Request a transition to this state"
- **Processing:** Mailbox discipline + transition guards; may be deferred or ignored based on context

**Examples:**
```
STATE_REQUESTED(target=LIVE)   // Request: enter LIVE state
STATE_REQUESTED(target=SLEEP)  // Request: enter SLEEP state
```

`STATE_REQUESTED` is now a first-class `SystemEvent` enum value. The Mailbox carries a `targetState` payload (`SystemState`) that `handleEvent()` reads to dispatch the appropriate transition. Callers use `postEvent(SystemEvent::STATE_REQUESTED, reason, targetState)`. Legacy events (`PLAY_REQUESTED`, `STOP_REQUESTED`, `ENTER_SLEEP`) remain fully functional for backward compatibility.

#### `STATE_ENTERED` (Internal Orchestration Outcome)
- **Source:** Internal to Supervisor orchestration
- **Semantics:** "We have successfully transitioned to this state; all prerequisites met"
- **Note:** Not exposed as a user-facing event; internal orchestration only

#### `STATE_FAILED` (Internal Orchestration Outcome)
- **Source:** Internal to Supervisor orchestration (after component failure or timeout)
- **Semantics:** "Transition to target state failed; fallback applied"
- **Note:** Not exposed as a user-facing event; internal orchestration only

### No Intent vs. Domain Event Distinction

**Decision:** All events are treated uniformly through the single-slot Mailbox with transition-context guards.
- No separate "intent event" vs. "domain event" buffering path
- No deferred intent queue; single processing pipeline
- `isIntentEvent()` helper is removed entirely

### Legacy Event Names as Adapters

Component-specific events (e.g., `STREAM_LOST`, `WIFI_DISCONNECTED`) are:
- Mapped to the generic contract through adapter logic
- **Not** exposed as the long-term controller event surface
- Remain internal component-to-controller communication

---

## 3. Mailbox Processing Model

### Naming and Semantics

The primary event-draining entrypoint is named **`processMailbox()`** (replacing `dispatchPending()`).

**Contract:**
- Single-slot Mailbox: last-write-wins when multiple events arrive before processing
- Each event in the current system state maps to exactly one outcome: `StateUnchanged`, `RequestTransition`, or `DirectTransition`
- Transition-context guards determine which events are valid in each state
- Stale outcomes (mismatched `transitionId`, invalid state) are ignored deterministically

### Failure Handling

The Mailbox always succeeds (single-slot, always overwrites). No enqueue failure path exists. The `ERROR_LOG` on failure is moot and is documented here only for historical context — escalation policy is deferred to a follow-up story (→ future "Error Recovery Policy").

---

## 4. Naming Standards

All references to events, queues, and processing functions use consistent naming:

| Artifact | Standard Name | Notes |
| --- | --- | --- |
| Mailbox draining | `processMailbox()` | Declaration, implementation, debug output |
| External intent event | `STATE_REQUESTED` | Payload includes target state (`postEvent(event, reason, target)`) |
| Internal entry outcome | `STATE_ENTERED` | Internal orchestration only |
| Internal failure outcome | `STATE_FAILED` | Internal orchestration only |
| Legacy component events | `STREAM_LOST`, `WIFI_DISCONNECTED`, ... | Mapped through adapters; not long-term surface |
| Reason metadata | Static `const char*` | See section 5 |

---

## 5. Reason Metadata Contract

### SystemReason as Static Text

`SystemReason` is represented as **static diagnostic text** (`const char*` with static lifetime):
- **Purpose:** Logging and debug output only
- **Never** used for state-transition decisions, guard clauses, or arbitration
- Optional: can be `nullptr` for events that do not require a reason

**Examples (current API):**
```cpp
postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST);  // current API
```

### Not a Causal Input

Failure control-path logic **must not depend on reason text**:
- ✗ **Wrong:** `if (reason == TIMEOUT) { restart(); }`
- ✓ **Correct:** Timeout is determined by orchestration state + timeout value; reason is diagnostics only

---

## 6. Readiness Model

### Single Source: Required Component Lifecycle

`READY` eligibility is derived from required component lifecycle status:
- Check each required component: is it in a safe/ready state?
- If all required components are ready, system may enter `READY`
- No separate startup read flags (`startupWiFiReady_`, `startupAudioReady_`, etc.)

**Decision:** One source of truth for readiness; no duplicate tracking booleans.

---

## 7. Event Ordering and Arbitration

### Deterministic Model Without Priority Table

Event ordering is determined by:
1. **Mailbox order** (single-slot, last-write-wins)
2. **Transition-context guards** (state + in-flight orchestration checks)
3. **Stale outcome detection** (`transitionId` mismatch, active transition state)

**No static priority table:** Events are not assigned inherent priorities; ordering is entirely context-dependent.

**Follow-up:** Per-state/per-event outcome matrix template is defined here; the full implementation matrix with all event×state cells is test-backed in US-0025h.

### Per-State/Per-Event Outcome Matrix

Each combination of (SystemState, SystemEvent) maps to exactly one normalized outcome:
- `state unchanged` → No transition; event is ignored or logged
- `request transition` → Orchestration begins transition to target state
- `direct transition` → Immediate state change (rare; documented explicitly)

---

## 8. Related Guidelines

- [State Management](../requirements/guidelines/state-management.md): SSOT, observer pattern, graceful degradation
- [Modularity](../requirements/guidelines/modularity.md): Component encapsulation, DI, interaction contract
- [Concurrency](../requirements/guidelines/concurrency.md): Core 0 event processing, queue sync
- [Testing](../requirements/guidelines/testing.md): Unit test strategy for state/event combinations
