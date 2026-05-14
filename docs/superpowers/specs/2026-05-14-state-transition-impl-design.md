# Design: SupervisorV2 State Transition Implementation (run() + Event Group Orchestration)

**Date:** 2026-05-14
**Status:** Draft

## Overview

Implement the active state transition loop (`run()`) for SupervisorV2, replacing the old cooperative polling model with a FreeRTOS task on Core 0. Components run on any core and communicate with the supervisor via shared mailboxes (spinlock-guarded) and a FreeRTOS event group for orchestration completion tracking.

---

## 1. Architecture

```
Core 0 (Supervisor task)                     Core 0/1 (Components)
┌──────────────────────────────┐            ┌────────────────────┐
│  Supervisor::run()           │            │  WiFiComponent     │
│  ┌────────────────────────┐  │            │  AudioRuntimeComp  │
│  │  Drain Mailbox         │◄─┼─spinlock───┤  CLI               │
│  │  Drain ErrorEvent      │◄─┼─spinlock───┤  BoardInfo         │
│  │  Step toward target    │  │            │  LightSensor (fut) │
│  │  Check state timeout   │  │            └────────┬───────────┘
│  │  Write component mbx   ├──┼──last-write-wins────┤
│  └────────────────────────┘  │            ┌────────▼───────────┐
│                               │            │  EventGroup        │
│  Reads: xEventGroupGetBits() ├────────────┤  (4+ bits)         │
└──────────────────────────────┘            └────────────────────┘
```

### Key principles
- Supervisor `run()` is a FreeRTops task pinned to Core 0
- Components run on any core and are FreeRTOS tasks
- `postStateRequest()` / `postErrorEvent()` are called cross-core with spinlock protection
- Component completion is signaled via a FreeRTOS event group (safe from any core)
- Both Supervisor and Components own a `SystemState` mailbox (last-write-wins, spinlock)
- All non-degraded components participate in every orchestration
- `isRequired` is a compile-time constant on each component class. The supervisor reads it from the component interface. Components self-report their required status at registration time via presence check-in, not a parameter.

---

## 2. Data Structures

### Existing (already in supervisor_v2.h)
- `Mailbox` — single-slot last-write-wins for state requests
- `ErrorEvent` — single-slot first-write-wins for async errors
- `RetryPolicy` — `maxRecoveries`, `recoveryCounter`, `isExhausted()`
- `ComponentStatusMap` — tracks `COMMITTED`, `FAILED`, `DEGRADED`
- `TransitionTimeoutConfig` — per-state forward/backward timeout arrays
- `ActiveTransition` — `transitionTarget`, `SubState` (PENDING/COMMITTED/FAILED)

### New
```cpp
struct ComponentMailbox {
    bool pending = false;
    SystemState targetState;       // last-write-wins
};
```

- Each component owns its own `ComponentMailbox` as a member
- During `registerComponent()`, the component passes a pointer to its mailbox
- The supervisor stores: `ComponentMailbox* componentMailboxes_[componentCount]` (initialized to `nullptr`)
- `postNextComponentState(id)` writes to `componentMailboxes_[id]` under spinlock (reads target from `nextState_.transitionTarget`)
- The component reads its own mailbox locally (no cross-core read)
- `isRequired` is a compile-time constant on each component class (`static constexpr bool isRequired = ...`). The supervisor reads it from the component interface; it is NOT passed to `registerComponent()`.

- The event group handle is stored as a member (`EventGroupHandle_t eventGroup_`)
- Target bits: `1 << static_cast<int>(componentId)` for each registered component
- FATAL housekeeping: `TickType_t fatalDeadlineMs_` — absolute deadline for deep sleep. Computed once as `now + fatalDwellMs` when FATAL is entered; checked each tick in `handleFatal()`. (fatalDwellMs is a compile-time constant, not stored as a member.)
- `SystemState lastTargetBeforeError_` — saved target for ERROR recovery placeholder. Automatically snapshotted by `setTargetState()` when transitioning to ERROR. TODO: remove once `determineRecoveryTarget()` is replaced with real recovery logic.

---

## 3. The `run()` Method

Called once per iteration of the FreeRTOS supervisor task. Never blocks (no queue wait).

```
void SupervisorV2::run() {
    // 1. Event processing — skipped in FATAL (system is dead, no new input)
    if (observedState_ != FATAL) {
        consumeErrorEvent();       // spinlock inside, logs, increments counter, FATAL if exhausted
        consumeStateRequest();     // spinlock inside, if pending -> setTargetState()
    }

    // 2. State stepping — skipped in FATAL (FATAL is absorbent)
    if (observedState_ != FATAL) {
        if (targetState_ != observedState_ && !hasActiveOrchestration()) {
            stepTowardTarget();
        } else if (hasActiveOrchestration()) {
            checkOrchestrationCompletion();
        } else if (observedState_ == ERROR) {
            SystemState recoveryTarget = determineRecoveryTarget();
            if (recoveryTarget != observedState_) {
                postStateRequest(recoveryTarget);
            }
        }
    }

    // 3. State timeout for active orchestration (any state, even FATAL orchestrations)
    if (hasActiveOrchestration()) {
        checkStateTimeout();
    }

    // 4. FATAL housekeeping — runs on same core, handles deep sleep transition
    if (observedState_ == FATAL) {
        handleFatal();
    }
}
```

### Per-step details

#### 3.1 FATAL behavior
- FATAL does NOT halt `run()`. Components may still work (e.g., LED blinking).
- Mailbox and error event processing are skipped: no new input is accepted in FATAL.
- State stepping is skipped: FATAL is absorbent, no automatic transitions out.
- `getNextState(FATAL, X)` returns FATAL for any target — defensive catch for callers.

#### 3.2 Consume ErrorEvent (inside spinlock)
- Called from `run()` on every non-FATAL tick
- Acquires error event spinlock, then calls into `consumeErrorEvent()` logic:
  - If `errorEvent_.pending`, logs error, increments recovery counter, sets FATAL if exhausted
  - Clears pending and payload
- Releases spinlock

#### 3.3 Consume Mailbox (inside spinlock)
- Called from `run()` on every non-FATAL tick
- Acquires mailbox spinlock
- If `stateRequestMailbox_.pending`, reads target, clears pending, releases spinlock, calls `setTargetState()`
- If no request pending, releases spinlock

#### 3.4 Step Toward Target
- Call `getNextState(observedState_, targetState_)` to get the intermediate stepping state
- If the result is the same as `observedState_` (already at target), no-op
- Otherwise, call `startOrchestration(nextState)` which sets `nextState_.transitionTarget`, `nextState_.subState = SubState::PENDING`, writes component mailboxes, and arms the event group
- **Note:** `getNextState()` must explicitly return FATAL when `current == FATAL` (defensive guard). The `run()` stepping block is already guarded against FATAL, so this is belt-and-suspenders.

#### 3.5 Check Orchestration Completion
- On each tick while an orchestration is in flight, check event group bits
- If all required, non-DEGRADED components have set their bit:
  - Set `nextState_.subState = SubState::COMMITTED`
  - Advance `observedState_` via `setObservedState()` (logs, clears active orchestration, calls `resetRecoveryIfOutOfError()`)
  - On the next `run()` tick, `targetState_ != observedState_` is re-evaluated. If more steps remain toward the final target, another step begins (the implicit loop).
- On failure of a required component: post an error event internally via `postErrorEvent()`
- On failure of an optional component: mark as DEGRADED, recompute expected bits, keep waiting

#### 3.6 Error Recovery (after ERROR orchestration completes)
- When `observedState_ == ERROR` and no orchestration is in flight, call `determineRecoveryTarget()`
- Placeholder: returns the state saved in `lastTargetBeforeError_` (TODO: remove once real recovery logic is implemented)
- `lastTargetBeforeError_` is automatically snapshotted by `setTargetState()` when transitioning to ERROR: before overwriting `targetState_`, if the old target was not ERROR and the new target is ERROR, the old target is saved
- Future: poll light sensor, WiFi state, and other input components
- Result is posted via `postStateRequest()` — goes through the mailbox, consumed on the next tick

#### 3.7 FATAL Housekeeping
- `handleFatal()` runs on each tick while `observedState_ == FATAL`
- On first call: `fatalDeadlineMs_ = xTaskGetTickCount() + 60000` (dwell is a compile-time constant)
- On each subsequent tick: if `xTaskGetTickCount() >= fatalDeadlineMs_`, commence deep sleep sequence
- Deep sleep sequence: log final message, call any registered shutdown hooks, then `esp_deep_sleep_start()`

#### 3.8 State Timeout
- Each orchestration records `transitionStartMs` and the per-state timeout
- If `(now - transitionStartMs) >= timeoutMs`, orchestration timed out
- Timeout action: mark all pending components as FAILED, handle consequences (required -> error, optional -> degraded)

---

## 4. Component Mailbox Pattern

Each component gets a `ComponentMailbox` (one array entry per `ComponentID`).

- **Supervisor writes** to the component's mailbox when starting an orchestration: `postNextComponentState(ComponentID id)`
  - Acquires per-component spinlock, sets `pending = true`, writes `targetState = nextState_.transitionTarget`, releases
- **Component reads** on its own task loop: `consumeNextState()`
  - Returns `true` and the target state if pending, clears mailbox
- **Component reacts** by calling the appropriate method (`setOFF`, `setIDLE`, `setSTREAMING`, `setERROR`)

This means the old `invokeComponentTransition()` with its virtual method calls is replaced by a shared-memory handoff.

### Boot presence discovery
At boot time, each component that is physically present calls `registerComponent(id, &mailboxSlot)` to check in with the supervisor. The supervisor marks the slot as present (`componentMailboxSlots_[id] != nullptr`).

After a discovery window (or when the first boot orchestration begins), the supervisor checks:
- Missing required component → `postErrorEvent()` → ERROR immediately
- Missing optional component → mark DEGRADED, exclude from quorum

The discovery window ends when the first orchestration starts (BOOTING → CONNECTING). Any component that hasn't registered by then is considered absent.

### Component-side pattern
```cpp
void WiFiComponent::loop() {
    SystemState target;
    if (mailbox_.consumeNextState(target)) {
        switch (target) {
            case SystemState::SLEEP:
            case SystemState::BOOTING: setOFF(transitionId); break;
            case SystemState::READY:
            case SystemState::CONNECTING: setIDLE(transitionId); break;
            case SystemState::LIVE: setSTREAMING(transitionId); break;
            case SystemState::ERROR: setERROR(transitionId); break;
            default: break;
        }
    }
    // ... existing async work ...
}
```

---

## 5. Event Group Orchestration

### Setup
- `StaticEventGroup_t eventGroupBuffer_` as class member (no heap allocation)
- `EventGroupHandle_t eventGroup_` as class member
- In `setup()`: `eventGroup_ = xEventGroupCreateStatic(&eventGroupBuffer_)`

### Starting an orchestration
1. Determine `expectedBits_`: OR of `(1 << id)` for each non-degraded, required component
2. `xEventGroupClearBits(eventGroup_, 0xFFFF)` — clear all bits (FreeRTOS: pass all used bits to avoid race)
3. Write each component's mailbox with the target state
4. Look up the per-state timeout from `TransitionTimeoutConfig`, compute `deadline = now + timeout`, store as `orchestrationDeadlineMs_`
5. Set `hasActiveOrchestration_ = true`

### Checking completion (on each run() tick)
1. `EventBits_t bits = xEventGroupGetBits(eventGroup_)`
2. If `(bits & expectedBits_) == expectedBits_`, orchestration is complete
3. On completion: set `SubState` to `COMMITTED`, clear active orchestration flag

### Timeout handling (during active orchestration)
- On each `run()` tick, if `hasActiveOrchestration_`:
  - Check `orchestrationDeadlineMs_ != 0 && xTaskGetTickCount() >= orchestrationDeadlineMs_`
  - If timed out:
    - For each component that has not set its event group bit: mark as FAILED
    - If FAILED component is required: `postErrorEvent()` → enter ERROR
    - If FAILED component is optional: mark DEGRADED, remove from expected bits, continue waiting for remaining components (they may still complete)

### Component failure (explicit, without timeout)
- A component may signal failure at any time via `completeTransition(id, TransitionStatus::Failed)`
  - If required: `postErrorEvent()` internally, transition to ERROR
  - If optional: mark as DEGRADED in `componentStatuses_`, recompute `expectedBits_` (exclude this component), keep waiting

### Report completion (component -> supervisor)
```cpp
void SupervisorV2::completeTransition(ComponentID id, TransitionStatus status) {
    if (status == TransitionStatus::Completed) {
        xEventGroupSetBits(eventGroup_, 1 << static_cast<int>(id));
    } else {
        // Handle failure inline — see above
    }
}
```
This is safe to call from any core (FreeRTOS event group API is ISR-safe).

---

## 6. Error Recovery Flow

### Entering ERROR
1. `errorEvent_` is pending, `consumeErrorEvent()` in `run()`:
   - Logs the error
   - Increments `retryPolicy_.recoveryCounter`
   - If `isExhausted()`: `setTargetState(FATAL)`, return
   - Otherwise: `setTargetState(ERROR)`

2. On the tick where ERROR becomes the current `targetState_`, orchestration toward ERROR begins
3. All non-degraded components get `setERROR()` via their mailbox
4. Components settle into ERROR mode

### Leaving ERROR (recovery)
1. When ERROR orchestration completes, `determineRecoveryTarget()` is called
2. Placeholder: returns `lastTargetBeforeError_` (TODO: remove once real recovery logic is implemented). This member is auto-snapshotted by `setTargetState()` when entering ERROR — before overwriting `targetState_`, if the old target was non-ERROR and the new one is ERROR, the old target is saved.
3. Future: poll light sensor (and other input components) to decide the highest justifiable state
4. The result is passed to `postStateRequest()`, which goes through the mailbox and is consumed on the next tick
5. When the recovery orchestration completes, `setObservedState()` resets the recovery counter automatically via `resetRecoveryIfOutOfError()`

### FATAL is terminal
- Once FATAL is entered, there is no recovery path. No event processing, no stepping.
- `handleFatal()` manages the deep sleep transition after the configured dwell time.
- Deep sleep is a one-way trip — the only way back is a hardware reset (wake timer, external pin).

---

## 7. Spinlock Guards

Two shared data structures need protection against cross-core access:

### Mailbox spinlock
```cpp
portMUX_TYPE mailboxSpinlock_ = portMUX_INITIALIZER_UNLOCKED;
```
Used by:
- `postStateRequest()` — write to `stateRequestMailbox_`
- `consumeStateRequest()` in `run()` — read and clear `stateRequestMailbox_`

### ErrorEvent spinlock
```cpp
portMUX_TYPE errorSpinlock_ = portMUX_INITIALIZER_UNLOCKED;
```
Used by:
- `postErrorEvent()` — write to `errorEvent_`
- `consumeErrorEvent()` in `run()` — read and clear `errorEvent_`

### Per-component mailbox spinlock
```cpp
// Owned by each component, supervisor holds a pointer
struct ComponentMailboxSlot {
    ComponentMailbox mailbox;
    portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;
};

// In supervisor:
ComponentMailboxSlot* componentMailboxSlots_[componentCount] = {};
```

Usage pattern (supervisor writes cross-core to component mailbox):
```cpp
void SupervisorV2::postStateRequest(SystemState target) {
    portENTER_CRITICAL(&mailboxSpinlock_);
    stateRequestMailbox_.pending = true;
    stateRequestMailbox_.requestedTarget = target;
    portEXIT_CRITICAL(&mailboxSpinlock_);
}

void SupervisorV2::postNextComponentState(ComponentID id) {
    auto* slot = componentMailboxSlots_[static_cast<int>(id)];
    if (slot == nullptr) return;
    portENTER_CRITICAL(&slot->spinlock);
    slot->mailbox.pending = true;
    slot->mailbox.targetState = nextState_.transitionTarget;
    portEXIT_CRITICAL(&slot->spinlock);
}
```

---

## 8. New Methods on SupervisorV2

### Public (new)
| Method | Purpose |
|--------|---------|
| `void run()` | Main tick function, called by FreeRTOS task |
| `void completeTransition(ComponentID id, TransitionStatus status)` | Called by components to signal completion |
| `void registerComponent(ComponentID id, ComponentMailboxSlot* slot)` | Component presence check-in, no isRequired param |

### Private (new)
| Method | Purpose |
|--------|---------|
| `void startOrchestration(SystemState target)` | Begin component transitions toward target |
| `void checkOrchestrationCompletion()` | Read event group, advance state if all bits set |
| `void checkStateTimeout()` | Detect stale orchestrations, handle timeout |
| `void setObservedState(SystemState state)` | Commit observed state, log, clear orchestration, reset recovery counter |
| `SystemState determineRecoveryTarget()` | Decide what to aim for after ERROR |
| `void postNextComponentState(ComponentID id)` | Write target from nextState_ to component mailbox |
| `void handleFatal()` | On first call: set fatalDeadlineMs_ = now + 60s. On subsequent calls: deep sleep if deadline reached |

### Removed from public API
- `setComponentTransitionHooks()` is replaced by the new mailbox pattern
- `reportCompletion()` is replaced by `completeTransition()`

---

## 9. State Timeouts

The timeout config (`TransitionTimeoutConfig`) stores per-state forward/backward timeouts. The default is 5000ms uniform.

When starting an orchestration:
- Determine direction (forward if `target > observed`, backward otherwise)
- Look up `timeoutConfig_.forwardTimeouts[idx]` or `.backwardTimeouts[idx]` for the target state
- Compute `orchestrationDeadlineMs_ = xTaskGetTickCount() + timeout`

On each `run()` tick:
- If `hasActiveOrchestration_`: check `orchestrationDeadlineMs_ != 0 && xTaskGetTickCount() >= orchestrationDeadlineMs_`
- If timed out:
  - For each component that has not yet set its event group bit: mark as FAILED
  - If FAILED component is required: `postErrorEvent()` → enter ERROR
  - If FAILED component is optional: mark DEGRADED, remove from expected bits, continue waiting for remaining components

---

## 10. Testing Strategy

All testable on `pio test -e native` without FreeRTOS:

- **State machine logic:** Test `getNextState()` rank-based stepping (already exists)
- **Mailbox drain logic:** Test `consumeStateRequest()` / `consumeErrorEvent()` with spinlock stubs (lock/unlock no-ops on native)
- **Orchestration completion:** Mock event group with a simple bitset (or event group stub), test completion detection with various bit patterns
- **Timeout detection:** Test timeout logic with fake clock
- **Component mailbox:** Test `postNextComponentState()` / `consumeNextState()` round-trip
- **Error recovery flow:** Test `consumeErrorEvent()` -> `determineRecoveryTarget()` chain
- **Integration:** Full `run()` tick sequence with mock components

### Native vs hardware
- Spinlock macros can be stubbed out for native (`portENTER_CRITICAL` → no-op, `portEXIT_CRITICAL` → no-op)
- Event group can use a real FreeRTOS event group stub or a simple `uint32_t` bitset wrapper
- The FreeRTOS task wrapper (the actual `xTaskCreatePinnedToCore`) is the only part that requires hardware testing

---

## 11. Implementation Order

1. Add `ComponentMailbox`, `ComponentMailboxSlot` structs, pointer arrays, event group handle, and FATAL members to supervisor_v2.h
2. Implement `registerComponent()`, `completeTransition()`, component mailbox methods, and boot presence check
3. Add spinlock guards to `consumeStateRequest()` and `consumeErrorEvent()`
4. Update `getNextState()` to explicitly return FATAL when `current == FATAL`
5. Implement `startOrchestration()`, `checkOrchestrationCompletion()`, `checkStateTimeout()`
6. Implement `setObservedState()`, `determineRecoveryTarget()`, and `handleFatal()`
7. Implement `run()` — the full tick sequence
8. Add spinlock stubs for native testing
9. Write unit tests for each path
10. Wire up FreeRTOS task in `main.cpp`
11. Update components to use the new mailbox pattern
