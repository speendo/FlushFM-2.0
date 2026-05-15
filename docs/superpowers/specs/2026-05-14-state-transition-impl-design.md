# Design: SupervisorV2 State Transition Implementation (run() + Event Group Orchestration)

**Date:** 2026-05-14 (revised 2026-05-15)
**Status:** Draft

## Overview

Implement the active state transition loop (`run()`) for SupervisorV2 with a split-task architecture: a **state machine task** (Core 0) handles mailbox consumption and state stepping, and an **orchestration worker task** blocks on `xEventGroupWaitBits()` for component completion. Components run on any core and communicate via shared mailboxes (spinlock-guarded).

---

## 1. Architecture

```
Core 0 (State Machine)                      Core 0 (Orch Worker)          Core 0/1 (Components)
┌───────────────────────────┐              ┌─────────────────────┐      ┌──────────────────┐
│ Supervisor::run()         │   order      │ orchestrationWorker │      │ WiFiComponent    │
│ ┌───────────────────────┐ │ ──────────► │                     │      │ AudioRuntimeComp │
│ │ Consume Mailbox       │◄┼─spinlock─    │ xEventGroupWaitBits │      │ CLI              │
│ │ Consume ErrorEvent    │◄┼─spinlock─    │   (expectedBits,    │      │ BoardInfo        │
│ │ Step toward target    │ │              │    pdTRUE, pdTRUE,  │      └────────┬─────────┘
│ │ Write component mbx   ├─┼─spinlock───► │    deadlineMs)      │               │
│ │                        │ │   response  │                      │               │
│ │ checkOrchResponse()   │◄┼──────────── │ post response        │               │
│ └───────────────────────┘ │              └──────────┬───────────┘               │
│                            │                        │                           │
│                         writes:                     │ xEventGroupGetBits() ←────┤
│                         xEventGroupClearBits() ─────┤                           │
│                         completeTransition() ───────┤ xEventGroupSetBits() ←────┤
└───────────────────────────┘              └─────────────────────┘      └──────────────────┘
```

### Key principles
- State machine `run()` is a FreeRTOS task pinned to Core 0. Never blocks.
- Orchestration worker is a separate FreeRTOS task, also pinned to Core 0. Blocks on `xEventGroupWaitBits(expectedBits, ALL, timeout)`.
- Components run on any core and are FreeRTOS tasks.
- `postStateRequest()` / `postErrorEvent()` are called cross-core with spinlock protection.
- Component completion is signaled via a FreeRTOS event group (safe from any core).
- Both Supervisor and Components own a `SystemState` mailbox (last-write-wins, spinlock).
- All non-degraded components participate in every orchestration.
- State machine and worker communicate via two spinlock-guarded mailboxes: `orderMailbox_` (state machine writes, worker reads) and `responseMailbox_` (worker writes, state machine reads).
- No abort protocol needed: FATAL/new-request during orchestration self-corrects on the next tick.

---

## 2. Data Structures

### Existing (already in supervisor_v2.h)
- `Mailbox` — single-slot last-write-wins for state requests
- `ErrorEvent` — single-slot first-write-wins for async errors
- `RetryPolicy` — `maxRecoveries`, `recoveryCounter`, `isExhausted()`
- `ComponentStatusMap` — tracks `COMMITTED`, `FAILED`, `DEGRADED`
- `TransitionTimeoutConfig` — per-state forward/backward timeout arrays
- `ActiveTransition` — `transitionTarget`, `SubState` (PENDING/COMMITTED/FAILED)

### Component mailbox (existing, in component_types.h)
```cpp
struct ComponentMailbox {
    bool pending = false;
    SystemState targetState;
    portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;

    bool consumeNextState(SystemState& outTarget) { /* ... */ }
};
```

- Defined in `component_types.h` (shared by supervisor and components)
- Each component owns its own `ComponentMailbox` as a member
- During `registerComponent()`, the component passes a pointer to its mailbox
- The supervisor stores: `ComponentMailbox* componentMailboxes_[componentCount]` (initialized to `nullptr`)
- `postNextComponentState(id)` writes to `componentMailboxes_[id]` under the embedded spinlock
- The component reads its own mailbox locally (no cross-core read)

### Orchestration mailboxes (new, in supervisor_v2.h)
```cpp
enum class OrchestrationResult : uint8_t {
    COMPLETED,
    TIMED_OUT
};

struct OrchestrationOrder {
    bool pending = false;
    EventBits_t expectedBits = 0;
    TickType_t deadlineMs = 0;
    SystemState transitionTarget;
    portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;

    void post(EventBits_t bits, TickType_t deadline, SystemState target) {
        portENTER_CRITICAL(&spinlock);
        expectedBits = bits;
        deadlineMs = deadline;
        transitionTarget = target;
        pending = true;
        portEXIT_CRITICAL(&spinlock);
    }

    bool consume(EventBits_t& outBits, TickType_t& outDeadline, SystemState& outTarget) {
        portENTER_CRITICAL(&spinlock);
        if (!pending) { portEXIT_CRITICAL(&spinlock); return false; }
        outBits = expectedBits;
        outDeadline = deadlineMs;
        outTarget = transitionTarget;
        pending = false;
        portEXIT_CRITICAL(&spinlock);
        return true;
    }
};

struct OrchestrationResponse {
    bool pending = false;
    OrchestrationResult result = OrchestrationResult::COMPLETED;
    EventBits_t timedOutComponents = 0;
    portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;

    void post(OrchestrationResult r, EventBits_t timedOut = 0) {
        portENTER_CRITICAL(&spinlock);
        result = r;
        timedOutComponents = timedOut;
        pending = true;
        portEXIT_CRITICAL(&spinlock);
    }

    bool consume(OrchestrationResult& outResult, EventBits_t& outTimedOut) {
        portENTER_CRITICAL(&spinlock);
        if (!pending) { portEXIT_CRITICAL(&spinlock); return false; }
        outResult = result;
        outTimedOut = timedOutComponents;
        pending = false;
        portEXIT_CRITICAL(&spinlock);
        return true;
    }
};
```

### Removed members (no longer needed on SupervisorV2)
- `expectedBits_` — moved into `OrchestrationOrder`
- `orchestrationDeadlineMs_` — moved into `OrchestrationOrder`

### New members on SupervisorV2
- `OrchestrationOrder orderMailbox_` — state machine writes, worker reads
- `OrchestrationResponse responseMailbox_` — worker writes, state machine reads
- `TaskHandle_t workerTaskHandle_` — handle for the orchestration worker task

### Unchanged members
- `EventGroupHandle_t eventGroup_` — still used by worker and `completeTransition()`
- `StaticEventGroup_t eventGroupBuffer_` — backing storage (static allocation)
- `hasActiveOrchestration_` — tracks whether a worker is running
- `fatalDeadlineMs_` — absolute deadline for deep sleep after FATAL
- `lastTargetBeforeError_` — saved target for ERROR recovery placeholder

---

## 3. The `run()` Method

Called once per iteration of the FreeRTOS state machine task. Never blocks.

```
void SupervisorV2::run() {
    // 1. Event processing — skipped in FATAL
    if (observedState_ != FATAL) {
        consumeErrorEvent();
        consumeStateRequest();
    }

    // 2. State stepping — skipped in FATAL
    if (observedState_ != FATAL) {
        if (targetState_ != observedState_ && !hasActiveOrchestration()) {
            stepTowardTarget();
        } else if (hasActiveOrchestration()) {
            checkOrchestrationResponse();    // <- replaces polling
        } else if (observedState_ == ERROR) {
            SystemState recoveryTarget = determineRecoveryTarget();
            if (recoveryTarget != observedState_) {
                postStateRequest(recoveryTarget);
            }
        }
    }

    // 3. FATAL housekeeping
    if (observedState_ == FATAL) {
        handleFatal();
    }
}
```

Key change: the two `if` blocks from the old design (state stepping and state timeout) merge into one — `checkOrchestrationResponse()` handles both completion and timeout by reading the response mailbox.

### 3.1 FATAL behavior
- FATAL does NOT halt `run()`. Components may still work (e.g., LED blinking).
- Mailbox and error event processing are skipped: no new input is accepted in FATAL.
- State stepping is skipped: FATAL is absorbent, no automatic transitions out.
- `getNextState(FATAL, X)` returns FATAL for any target — defensive catch for callers.

### 3.2 Consume ErrorEvent (unchanged)
- Called from `run()` on every non-FATAL tick
- Acquires error event spinlock, then calls into `consumeErrorEvent()` logic
- If `errorEvent_.pending`, logs error, increments recovery counter, sets FATAL if exhausted, otherwise sets ERROR
- Clears pending and payload

### 3.3 Consume Mailbox (unchanged)
- Called from `run()` on every non-FATAL tick
- Acquires mailbox spinlock
- If `stateRequestMailbox_.pending`, reads target, clears pending, releases spinlock, calls `setTargetState()`

### 3.4 Step Toward Target
- Call `getNextState(observedState_, targetState_)` to get the intermediate stepping state
- If the result is the same as `observedState_` (already at target), no-op
- Otherwise, call `startOrchestration(nextState)` which:
  - Computes `expectedBits` from required, non-degraded registered components
  - Clears the event group
  - Writes all component mailboxes with the stepping target
  - Computes per-state timeout deadline
  - Posts `OrchestrationOrder` to `orderMailbox_` (wakes the worker)
  - Sets `nextState_` and `hasActiveOrchestration_ = true`

### 3.5 Check Orchestration Response (replaces old 3.5 + 3.8)
- Read `responseMailbox_` (non-blocking, spinlock inside)
- If no response yet, return — the worker is still waiting
- If `COMPLETED`:
  - Set `nextState_.subState = SubState::COMMITTED`
  - Advance `observedState_` via `setObservedState()` (logs, clears active orchestration, resets recovery counter)
  - On the next tick, `targetState_ != observedState_` is re-evaluated for the next step
- If `TIMED_OUT`:
  - Clear `hasActiveOrchestration_` (orchestration is done, even though it failed)
  - For each timed-out component:
    - Required: mark `FAILED`, call `postErrorEvent("transition timeout", id)`
    - Optional: mark `DEGRADED`
  - On the next tick, `consumeErrorEvent()` processes the posted errors, setting `targetState_ = ERROR` or `FATAL`
  - Components that completed successfully remain in their target state; the next orchestration toward ERROR will reset them

### 3.6 Error Recovery (unchanged)
- When `observedState_ == ERROR` and no orchestration is in flight, call `determineRecoveryTarget()`
- Placeholder: returns `lastTargetBeforeError_`
- Result is posted via `postStateRequest()` — goes through the mailbox, consumed on the next tick

### 3.7 FATAL Housekeeping (unchanged)
- `handleFatal()` runs on each tick while `observedState_ == FATAL`
- On first call: `fatalDeadlineMs_ = xTaskGetTickCount() + 60000`
- On subsequent ticks: if deadline reached, commence deep sleep

---

## 4. Component Mailbox Pattern (unchanged)

Each component gets a `ComponentMailbox` (one array entry per `ComponentID`).

- **Supervisor writes** to the component's mailbox when starting an orchestration: `postNextComponentState(ComponentID id)`
- **Component reads** on its own task loop: `mailbox_.consumeNextState(target)`
- **Component reacts** by calling the appropriate handler

### Boot presence discovery (unchanged)
At boot time, each component calls `registerComponent(id, &mailbox, isRequired)`. Missing required components trigger `postErrorEvent()`.
The discovery window ends when the first orchestration starts (BOOTING → CONNECTING).

---

## 5. Event Group Orchestration (split-task)

### Setup (unchanged)
- `StaticEventGroup_t eventGroupBuffer_` as class member
- `EventGroupHandle_t eventGroup_` as class member
- In `setup()`: `eventGroup_ = xEventGroupCreateStatic(&eventGroupBuffer_)`

### Starting an orchestration (state machine side)
1. Determine `expectedBits`: OR of `(1 << id)` for each non-degraded, required component
2. `xEventGroupClearBits(eventGroup_, 0xFFFF)` — clear all bits
3. Write each component's mailbox with the target state
4. Look up the per-state timeout, compute `deadline = now + timeout`
5. Post `OrchestrationOrder(expectedBits, deadline, target)` to `orderMailbox_`
6. Set `nextState_.transitionTarget = target`, `nextState_.subState = PENDING`, `hasActiveOrchestration_ = true`

### Worker side
The `orchestrationWorker` task runs a loop:
1. Block on reading `orderMailbox_` (vTaskDelay if no order)
2. Call `xEventGroupWaitBits(eventGroup_, expectedBits, pdTRUE, pdTRUE, deadline - now)`
3. If all bits set: post `OrchestrationResponse(COMPLETED)`
4. If timeout: compute which bits are missing, post `OrchestrationResponse(TIMED_OUT, missingBits)`
5. Loop back to step 1

### Completion detection (state machine side)
- `checkOrchestrationResponse()` reads `responseMailbox_` (non-blocking)
- Handles COMPLETED or TIMED_OUT as described in 3.5

### Component failure (explicit)
- Required: `postErrorEvent()` — sets ERROR as target. The current orchestration finishes normally; the next orchestration steps toward ERROR.
- Optional: mark as DEGRADED, set the component's event group bit (so the worker considers it "done"). The bit satisfies the worker's wait; the DEGRADED status excludes this component from future orchestrations.

### Report completion (component -> event group)
```cpp
void SupervisorV2::completeTransition(ComponentID id, TransitionStatus status) {
    if (status == TransitionStatus::Completed) {
        xEventGroupSetBits(eventGroup_, 1 << static_cast<int>(id));
    } else if (!isRequired_[static_cast<int>(id)]) {
        componentStatuses_[static_cast<int>(id)] = ComponentStatus::DEGRADED;
        xEventGroupSetBits(eventGroup_, 1 << static_cast<int>(id));
    } else {
        postErrorEvent("component failed", id);
    }
}
```
Safe to call from any core (FreeRTOS event group API is ISR-safe).

---

## 6. Error Recovery Flow (unchanged)

See original spec.

---

## 7. Spinlock Guards (unchanged)

All spinlocks are embedded in the structs they protect:
- `Mailbox::spinlock` — guards `stateRequestMailbox_`
- `ErrorEvent::spinlock` — guards `errorEvent_`
- `ComponentMailbox::spinlock` — embedded in each component's mailbox
- `OrchestrationOrder::spinlock` — guards `orderMailbox_`
- `OrchestrationResponse::spinlock` — guards `responseMailbox_`

---

## 8. New Methods on SupervisorV2

### Public
| Method | Purpose |
|--------|---------|
| `void run()` | Main tick function, called by FreeRTOS state machine task |
| `void completeTransition(ComponentID id, TransitionStatus status)` | Called by components to signal completion |
| `void registerComponent(ComponentID id, ComponentMailbox* mailbox, bool isRequired)` | Component presence check-in |

### Private
| Method | Purpose |
|--------|---------|
| `void startOrchestration(SystemState target)` | Compute bits/clean event group/write mailboxes, post order to worker |
| `void checkOrchestrationResponse()` | Read worker response, handle COMPLETED or TIMED_OUT |
| `void setObservedState(SystemState state)` | Commit observed state, log, clear orchestration, reset recovery counter |
| `SystemState determineRecoveryTarget()` | Decide what to aim for after ERROR |
| `void postNextComponentState(ComponentID id)` | Write target from nextState_ to component mailbox |
| `void handleFatal()` | Manage deep sleep transition after FATAL dwell |

### Removed from private API
- `checkOrchestrationCompletion()` — replaced by `checkOrchestrationResponse()`
- `checkStateTimeout()` — timeout handled by worker's `xEventGroupWaitBits(timeout)`, result reported in response

### Free function (not a member)
```cpp
void orchestrationWorker(void* param);
```
- Receives `SupervisorV2*` as parameter
- Loops: reads `orderMailbox_`, calls `xEventGroupWaitBits()`, writes `responseMailbox_`
- Declared as `friend` in `SupervisorV2` to access private members

### Removed from public API (unchanged)
- `setComponentTransitionHooks()` — replaced by mailbox pattern
- `reportCompletion()` — replaced by `completeTransition()`

---

## 9. State Timeouts

The timeout config (`TransitionTimeoutConfig`) stores per-state forward/backward timeouts. Default: 5000ms uniform.

When starting an orchestration:
- Determine direction (forward if `target > observed`, backward otherwise)
- Look up `timeoutConfig_.forwardTimeouts[idx]` or `.backwardTimeouts[idx]`
- Compute `deadline = xTaskGetTickCount() + timeout`
- Post deadline to `orderMailbox_` as part of the order

The worker calls `xEventGroupWaitBits(eventGroup_, expectedBits, pdTRUE, pdTRUE, deadline - xTaskGetTickCount())`. FreeRTOS handles the timeout internally — the function returns either with all bits set or with a timeout code.

In the response:
- COMPLETED: all required non-degraded components set their bits before the deadline
- TIMED_OUT: the deadline elapsed before all bits were set. `timedOutComponents` is a mask of missing bits.

---

## 10. Testing Strategy

All testable on `pio test -e native` without FreeRTOS:

- **State machine logic:** Test `getNextState()` rank-based stepping (already done, step 4)
- **Mailbox drain logic:** Test `consumeStateRequest()` / `consumeErrorEvent()` with spinlock stubs (already done, step 3)
- **Orchestration response handling:** Write directly to `responseMailbox_` and call `checkOrchestrationResponse()` — test COMPLETED and TIMED_OUT paths
- **Orchestration start:** Call `startOrchestration()` and verify order mailbox contents + component mailbox writes
- **Worker function:** Test on hardware only (involves blocking FreeRTOS calls)
- **Component mailbox:** Test `postNextComponentState()` / `consumeNextState()` round-trip (already done, step 2)
- **Integration:** Full `run()` tick sequence with mock responses

### Native vs hardware
- Spinlock macros stubbed for native (`portENTER_CRITICAL` → no-op)
- Event group works with upgraded native stubs (bitmap-backed `xEventGroupSetBits`/`GetBits`)
- Order/response mailboxes use the same embedded spinlock pattern (no-ops on native)
- The `orchestrationWorker` FreeRTOS task is hardware-only

---

## 11. Implementation Order

1. Add `ComponentMailbox` to `component_types.h` with embedded spinlock, add spinlocks to `Mailbox`/`ErrorEvent`, update pointer array and registerComponent signature in `supervisor_v2.h` ✅ (step 1)
2. Implement `registerComponent()`, `completeTransition()`, component mailbox methods, and boot presence check ✅ (step 2)
3. Add spinlock guards to `consumeStateRequest()` and `consumeErrorEvent()` ✅ (step 3)
4. Update `getNextState()` to explicitly return FATAL when `current == FATAL` ✅ (step 4)
5. Split `supervisor_v2.cpp` into three `.cpp` files — `supervisor_v2.cpp` (config), `orchestrator.cpp` (cross-core I/O), `state_machine.cpp` (state logic) — **step 4.1**
6. Add `OrchestrationOrder`/`OrchestrationResponse`/`OrchestrationResult` structs, replace polling members/methods with split-task equivalents, add native stubs, implement `startOrchestration()`, `checkOrchestrationResponse()`, `orchestrationWorker()` in `orchestrator.cpp`, wire worker task in `setup()` — **step 5**
7. Implement `determineRecoveryTarget()`, and `handleFatal()`; enhance `setObservedState()` with logging and `resetRecoveryIfOutOfError()` — **step 6**
8. Implement `run()` — the full tick sequence — **step 7**
9. Write unit tests for remaining paths — **step 8**
10. Wire up FreeRTOS tasks in `main.cpp` — **step 9**
11. Update components to use the new mailbox pattern — **step 10**
