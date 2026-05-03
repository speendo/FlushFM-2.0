# Event Contract Model

**Status:** Frozen / Approved (US-0025c) | **Updated:** 2026-05-03

## Overview

The SystemController uses a minimal generic event contract to coordinate state transitions across the system. This document defines:
- Event types and semantics
- Global vs. local state responsibilities
- Queue processing model
- Failure handling contract

---

## 1. Core Invariants

### SystemController as Single Source of Truth (SSOT)

**Global system state** is owned and managed exclusively by SystemController:
- `BOOTING` → `SLEEP` → `CONNECTING` → `READY` → `LIVE` → `ERROR`
- Only SystemController can call `setState(...)` or `transitionTo(...)`
- No component or external code may directly modify the global system state
- All state-change intents must go through the event queue as `SystemEvent`

### Component Local States as Mirrors

Components may maintain internal lifecycle states that mirror the global guarantees:
- **Component states are implementation details**, not driving logic
- Each SystemState defines guarantees that all components must satisfy
- Components are responsible for their own internal state consistency
- Components never push component-local state logic into SystemController

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

The SystemController event interface uses a minimal three-part model:

#### `STATE_REQUESTED` (External Intent)
- **Source:** Components, CLI, hardware interrupts (e.g., light sensor)
- **Payload:** Target system state (e.g., `READY`, `LIVE`, `SLEEP`)
- **Semantics:** "Request a transition to this state"
- **Processing:** Queue discipline + transition guards; may be deferred or ignored based on context

**Examples:**
```
STATE_REQUESTED(target=LIVE)   // Request: enter LIVE state
STATE_REQUESTED(target=SLEEP)  // Request: enter SLEEP state
```

#### `STATE_ENTERED` (Internal Orchestration Outcome)
- **Source:** Internal to SystemController orchestration
- **Semantics:** "We have successfully transitioned to this state; all prerequisites met"
- **Note:** Not exposed as a user-facing event; internal orchestration only

#### `STATE_FAILED` (Internal Orchestration Outcome)
- **Source:** Internal to SystemController orchestration (after component failure or timeout)
- **Semantics:** "Transition to target state failed; fallback applied"
- **Note:** Not exposed as a user-facing event; internal orchestration only

### No Intent vs. Domain Event Distinction

**Decision:** All events are treated uniformly in a single FIFO queue with transition-context guards.
- No separate "intent event" vs. "domain event" buffering path
- No deferred intent queue; single processing pipeline
- `isIntentEvent()` helper is removed entirely

### Legacy Event Names as Adapters

Component-specific events (e.g., `STREAM_LOST`, `WIFI_DISCONNECTED`) are:
- Mapped to the generic contract through adapter logic
- **Not** exposed as the long-term controller event surface
- Remain internal component-to-controller communication

---

## 3. Queue Processing Model

### Naming and Semantics

The primary event-draining entrypoint is named **`processEventQueue()`** (replacing ambiguous `dispatchPending()`).

**Contract:**
- FIFO queue discipline: events are processed in arrival order
- Each event in the current system state maps to exactly one outcome: `StateUnchanged`, `RequestTransition`, or `DirectTransition`
- Transition-context guards determine which events are valid in each state
- Stale outcomes (mismatched `transitionId`, invalid state) are ignored deterministically

### Failure Handling

On enqueue failure in `postEvent(...)`:
- **Minimum reaction:** Log `ERROR_LOG` with event context (type, target state, timestamp)
- **Example:** `[ERROR] postEvent failed: STATE_REQUESTED(LIVE) discarded`
- **Harder escalation** (automatic restart, ERROR-state transition) deferred to follow-up story (→ future "Error Recovery Policy")
- Every failed enqueue is logged; **no silent failures**

---

## 4. Naming Standards

All references to events, queues, and processing functions use consistent naming:

| Artifact | Standard Name | Notes |
| --- | --- | --- |
| Event queue draining | `processEventQueue()` | Declaration, implementation, debug output |
| External intent event | `STATE_REQUESTED` | Payload includes target state |
| Internal entry outcome | `STATE_ENTERED` | Internal orchestration only |
| Internal failure outcome | `STATE_FAILED` | Internal orchestration only |
| Legacy component events | `STREAM_LOST`, `WIFI_DISCONNECTED`, ... | Mapped through adapters; not long-term surface |
| Event policy enum | `EventPolicy` with `BestEffort`, `Critical` | Replaces ambiguous naming |
| Reason metadata | Static `const char*` | See section 5 |

---

## 5. Reason Metadata Contract

### SystemReason as Static Text

`SystemReason` is represented as **static diagnostic text** (`const char*` with static lifetime):
- **Purpose:** Logging and debug output only
- **Never** used for state-transition decisions, guard clauses, or arbitration
- Optional: can be `nullptr` for events that do not require a reason

**Examples:**
```cpp
postEvent(STATE_REQUESTED(READY), SystemReason::LIGHT_SENSOR_OFF);     // static text
postEvent(STATE_REQUESTED(LIVE), nullptr);                             // optional
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
1. **Queue order** (FIFO discipline)
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
