# Supervisor Architecture Freeze — Cleanup and Future Ideas

> **Created:** 2026-05-17 | **Author:** Design session review of open stories

## 1. Frozen Architecture

SupervisorV2 (`src/supervisor/supervisor_v2.h`) is the production supervisor. Its public API is frozen as-is:

| Method | Purpose |
|--------|---------|
| `postStateRequest(target)` | External state intent, last-write-wins mailbox |
| `postErrorEvent(reason, source)` | Component failure signal, first-writer-wins |
| `registerComponent(id, mailbox, isRequired)` | Boot-time component registration |
| `completeTransition(id, status)` | Component reports transition outcome |
| `setup()` | Initialize event group, spawn worker, load config |
| `run()` | State machine tick (called from FreeRTOS task) |
| `getObservedState()` | Read current confirmed state |
| `getTargetState()` | Read current target state |

**No interface changes.** The existing methods, their signatures, and their semantics are the frozen contract.

---

## 2. Story Lifecycle Actions

### 2.1 Cancelled Stories

| Story | Status | Reason |
|-------|--------|--------|
| **US-0032** (STATE_REQUESTED event type) | Cancelled | V2's `postStateRequest(target)` + `postErrorEvent(reason, source)` already provide a clean request/error API without a formal `SystemEvent` enum. A 2-event enum wrapper adds ceremony without changing behavior. |
| **US-0037** (Algorithmic step-through) | Cancelled | V2 already has: rank-based `getNextState()`, separate `targetState_`/`observedState_`, immediate ERROR/FATAL handling, `stepTowardTarget()` with `checkOrchestrationResponse()` continuation, and last-write-wins mailbox (handles CONNECTING deferral naturally). Remaining gaps are cosmetic naming or intentionally left to caller discipline. |
| **US-0033** (Pattern compliance audit) | Cancelled — superseded | V2 satisfies the spec's core requirements (RetryPolicy, DEGRADED, single-slot Mailbox, ErrorEvent flag+payload, Core 0 exclusivity, per-component required/optional). Remaining structural gaps will be captured in a new focused story (see §2.3). |

### 2.2 Merged Story

**US-0011** (Logging hygiene) and **US-0018** (Unified transition logging) → merged into a single logging cleanup story. Both target log quality and developer experience; splitting them is unnecessary overhead.

### 2.3 Replacement Story

Replace **US-0033** with a new, focused story capturing the remaining structural gaps from the state-management guideline that V2 does not yet implement:

- **Component min/max state matrix** — per-component table indexed by `systemState`. Defines each component's valid state range for every system-level state. V2 currently sends an exact target state to every component; the matrix allows components to stay within a range, enabling the lazy/busy optimization (see §3.3).
- **Absent-component handling** — `optional` components not present at boot are not yet treated as permanently `DEGRADED`; `required` absent components don't trigger immediate ERROR
- **SLEEP contract** — Clarify that `esp_deep_sleep_start()` is called only after all components have committed to SLEEP. V2's orchestration handles the PENDING→COMMITTED progression; the gap is ensuring the actual deep sleep trigger fires at the right point.

These are deferred implementation items, documented for a future story.

### 2.4 Kept As-Is (Deferred)

| Story | Reason |
|-------|--------|
| **US-0008** (LED) | New component, unrelated to supervisor freeze |
| **US-0009** (Audio buffer) | Audio/decoder domain |
| **US-0019** (Event deduplication) | Sensor-level hysteresis, different problem domain |
| **US-0020** (Orchestration tests) | Tests needed but not a supervisor contract concern |
| **US-0023** (Firmware size) | Build optimization |
| **US-0024** (Audio fade) | AudioRuntime domain |
| **US-0038** (NVS persistence) | Deferred placeholder — hooks exist (`loadTransitionTimeoutConfig`, `setMaxRecoveries`), no implementation planned yet |
| **US-0039** (Class extraction) | Cosmetic file split — state_machine.cpp and orchestrator.cpp exist but contain `SupervisorV2::method()` implementations |

---

## 3. Future Ideas

These are design notes for potential future evolution of the supervisor. They are not planned for immediate implementation and exist to capture architectural thinking. If and when they are implemented, they will require design review and updated stories.

### 3.1 Component-Driven Maximum Achievable State (Cap)

**Problem**: When a required component is healthy but blocked by a resolvable external condition, the system should not escalate to ERROR recovery. The canonical case: WiFi has no credentials, so the system can never reach READY/LIVE. But this is solvable (user enters credentials via CLI) — it is not a FATAL error.

**Proposed model**: Each component knows its own state ceiling. The supervisor's system-wide cap is the minimum of all component caps.

```
// Component signals its cap to the Supervisor
WiFiComponent:  setCap(id, CONNECTING)   // "I can only reach CONNECTING"
AudioComponent: setCap(id, LIVE)         // "I can reach LIVE"

// Supervisor recomputes
systemCap = min(CONNECTING, LIVE) = CONNECTING
```

**Supervisor behavior with caps:**

`targetState_` remains last-write-wins — a new state request always overwrites the stored intent, even when the previous intent was beyond the cap.

| Scenario | Behavior |
|----------|----------|
| `targetState <= systemCap` | Normal: step toward target, orchestrate |
| `targetState > systemCap` | Step toward `systemCap` as effective target, store original intent in `targetState_` for when cap rises |
| `systemCap drops < observedState` | Trigger downward step to new cap |
| Downward request (any target) | Always allowed, cap irrelevant |

**Unblocking:** Components push cap updates. When a condition clears (e.g., WiFi connects via callback), the component calls `setCap(id, LIVE)` — no polling from either side. The supervisor recomputes the cap and, if `targetState_` is now within reach, resumes stepping.

**Open design questions:**
- Method signature: should there be `setCap(id, state)` (set to specific state) or `raiseCap(id)` / `lowerCap(id, state)`?
- Should the cap be tracked as a stored variable or derived from component statuses?
- How does a cap interact with the existing `DEGRADED` mechanism for optional components?

**Relationship to BLOCKED:** An alternative model would add `ComponentStatus::BLOCKED` alongside `COMMITTED`, `FAILED`, `DEGRADED`. The cap-based model is preferred because it avoids the open question of "who is responsible for unblocking?" (supervisor polling vs. component self-checking). With caps, the component pushes `setCap(id, state)` when conditions change — the same push model already used by `completeTransition` and `postErrorEvent`.

### 3.2 Flexible Transition Timeouts (Component-Driven)

**Problem**: The current `TransitionTimeoutConfig` matrix uses uniform per-state timeouts (5 seconds everywhere). Different components need different durations for different transitions. WiFi connection may take 15 seconds, but the current timeout forces either false timeouts or unnecessarily long error-detection delays.

**Proposed model**: Components return timeout estimates from their `setXXX()` methods. The supervisor uses the maximum of all component timeouts for the orchestration deadline. This replaces the timeout matrix entirely — timings come from components, not from supervisor configuration.

**Doubt (unresolved)**: For truly open-ended operations like WiFi connection, the component cannot predict how long it will take. A `setCap()`-based model (idea 3.1) already handles this differently: instead of extending the timeout, the component signals it needs more time by lowering its cap. The two ideas may overlap in practice.

**Assessment**: Idea 3.1 (caps) likely covers the motivating use case. Flexible timeouts are noted here for completeness but have a higher bar for justification. If implemented, they would replace the `TransitionTimeoutConfig` matrix, not augment it.

### 3.3 Lazy / Busy Transition Mode

**Problem**: V2 sends the exact target system state to every component. Every component transitions, even if it's already in a valid state for that target. This causes unnecessary work (e.g., display turns off and back on during a recovery cycle).

**Proposed model**: On a state transition request, the supervisor tells each component whether to be lazy or busy. This requires the min/max state matrix (§2.3) — each component defines its valid range per system state.

| Instruction | Meaning |
|-------------|---------|
| **Lazy** | Minimal steps. If in range → stay put. If out of range → just reach the nearest boundary. |
| **Busy** | Best effort. Go as far as possible in the direction of movement. |

**Examples:**

*Display component, LIVE→ERROR, range [ERROR, LIVE]:*
- Lazy: stays at LIVE (already in range) → **display stays on**
- Busy: strives for ERROR (go as low as possible) → **display tries to turn off**

*Audio component, BOOTING→READY, range [CONNECTING, READY]:*
- Lazy: reaches CONNECTING (just hit min) → audio decoders init but don't connect to stream
- Busy: strives for READY (go as high as possible) → audio fully connects to host

**Prerequisite:** The min/max state matrix (§2.3). Without per-component ranges, lazy/busy has nothing to work with.

---
