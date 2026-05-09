# Comprehensive Alignment: Event Cleanup + Queue Removal + Data Structure Alignment

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Remove legacy event types (`PLAY_REQUESTED`, `STOP_REQUESTED`, `ENTER_SLEEP`, `RECOVER`), deduplicate `handleEvent()`, remove the queued transition (no-queue policy violation), and align data structures with `state-management.md`.

**Architecture:** Three stages. Stage A removes 4 enum values and migrates all callers to `STATE_REQUESTED`. Stage B removes the queued transition slot from the transition flow. Stage C documents data structure gaps against `state-management.md` (requires design decisions).

**Tech Stack:** C++17, PlatformIO, Unity test framework

---

## Audit Summary

**Event enum gaps:**
- 4 redundant event types: `PLAY_REQUESTED`, `STOP_REQUESTED`, `ENTER_SLEEP`, `RECOVER` — all replaced by `STATE_REQUESTED(targetState)`

**handleEvent duplication:**
- `ENTER_SLEEP` if-block identical to `STATE_REQUESTED(SLEEP)` case
- `STOP_REQUESTED` if-block identical to `STATE_REQUESTED(READY)` case
- `PLAY_REQUESTED` if-block identical to `STATE_REQUESTED(LIVE)` case
- Empty `switch (observedState_)` with no remaining event handlers (RECOVER was the only case body)

**Queue violation:**
- `hasQueuedStateTransition_` + `queuedStateTransition_` in supervisor.h — queues a transition during an active transition, violating "Never: Use queues within the Supervisor event and transition flow"

**Data structure misalignment with state-management.md (§3 Reference Pattern):**
1. `TargetMode` enum → code uses `SystemState` (same values, different name)
2. `SubState { PENDING, COMMITTED, FAILED }` → **missing entirely**
3. `ComponentStatus { COMMITTED, FAILED, DEGRADED }` → code has `ComponentLifecycleStatus { Unknown, Ready, Failed, Disabled }` and `TransitionStatus { Completed, Failed }`
4. `ActiveTransition { TargetMode target; SubState state; }` → **missing**
5. `ComponentStatusMap` typedef → code uses raw `std::array<ComponentRegistryEntry, ...>`
6. `RetryPolicy { maxRetries, recoveryCounter, isExhausted() }` → **not implemented**
7. `StateRequirement { TargetMode minState; TargetMode maxState; }` → code has `ComponentStateMatrix { uint32_t minState; uint32_t maxState; ... }` (different type)
8. `ComponentBase::transitionTo(TargetMode)` → code has `ISystemComponent::setOFF/setIDLE/setSTREAMING/setERROR` (different interface)

---

## File Map

| File | Stage A | Stage B | Stage C | Notes |
|---|---|---|---|---|---|
| `src/state_machine/supervisor.h` | modify | modify | modify | Enum, Mailbox, private members, TransitionRequestDecision |
| `src/state_machine/supervisor.cpp` | modify | modify | modify | handleEvent, requestTransition, finishTransition, auto-play calls |
| `src/main.cpp` | modify | - | - | boot auto-play → STATE_REQUESTED |
| `src/components/cli/cli.cpp` | modify | - | - | play/stop/reset → STATE_REQUESTED |
| `src/components/cli/debug_cli.cpp` | modify | - | - | debug transitions → STATE_REQUESTED |
| `test/test_state_transition_flow/test_main.cpp` | modify | - | - | all tests → STATE_REQUESTED |
| `test/test_component_types/test_main.cpp` | modify | modify | modify | event + decision enum test lists |
| `test/test_mailbox_contract/test_main.cpp` | modify | - | - | ENTER_SLEEP → STATE_REQUESTED(SLEEP) |
| `test/test_transition_timeout_watchdog/test_main.cpp` | modify | - | - | PLAY_REQUESTED → STATE_REQUESTED |
| `docs/event-contract-model.md` | modify | - | modify | reflect 2-event enum |

---

## Stage A: Event Enum Cleanup + handleEvent Dedup

### Task A1: Remove 4 events from SYSTEM_EVENT_X

**Files:**
- Modify: `src/state_machine/supervisor.h:45-51`

- [ ] **Step 1: Edit the macro**

Current:
```cpp
#define SYSTEM_EVENT_X(V) \
    V(COMPONENT_SETUP_FAILED) \
    V(PLAY_REQUESTED) \
    V(STOP_REQUESTED) \
    V(RECOVER) \
    V(ENTER_SLEEP) \
    V(STATE_REQUESTED)
```

New:
```cpp
#define SYSTEM_EVENT_X(V) \
    V(COMPONENT_SETUP_FAILED) \
    V(STATE_REQUESTED)
```

### Task A2: Update handleEvent — remove legacy if-chain and empty state switch

**Files:**
- Modify: `src/state_machine/supervisor.cpp:476-586`

- [ ] **Step 1: Only STATE_REQUESTED switch and COMPONENT_SETUP_FAILED remain**

The current code has:

```
lines 476-520:  STATE_REQUESTED switch
lines 522-557:  legacy if-chain (ENTER_SLEEP, STOP_REQUESTED, PLAY_REQUESTED) ← DELETE
lines 559-562:  COMPONENT_SETUP_FAILED handler ← KEEP
lines 564-586:  empty switch(observedState_) with RECOVER in ERROR case ← DELETE
```

Replace lines 522-586 with just the COMPONENT_SETUP_FAILED handler:

```cpp
    if (event == SystemEvent::COMPONENT_SETUP_FAILED) {
        transitionTo(observedState_ == SystemState::ERROR ? SystemState::FATAL : SystemState::ERROR, event, reason);
        return;
    }
```

That's it — no more `switch (observedState_)`.

### Task A3: Update auto-play handleEvent calls in supervisor.cpp

**Files:**
- Modify: `src/state_machine/supervisor.cpp:234,322`

Two sites call `handleEvent(SystemEvent::PLAY_REQUESTED, ...)` directly to re-trigger play after READY is reached. They need `mailbox_.targetState` set first.

- [ ] **Step 1: Change line 234**

Current:
```cpp
handleEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST);
```

New:
```cpp
mailbox_.targetState = SystemState::LIVE;
handleEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST);
```

- [ ] **Step 2: Change line 322** (same change)

Current:
```cpp
handleEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST);
```

New:
```cpp
mailbox_.targetState = SystemState::LIVE;
handleEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST);
```

### Task A4: Update main.cpp

**Files:**
- Modify: `src/main.cpp:64-66`

- [ ] **Step 1: Replace boot auto-play**

Current:
```cpp
(void)s_system.postEvent(SystemEvent::PLAY_REQUESTED,
                         SystemReason::USER_REQUEST);
```

New:
```cpp
(void)s_system.postEvent(SystemEvent::STATE_REQUESTED,
                         SystemReason::USER_REQUEST,
                         SystemState::LIVE);
```

### Task A5: Update cli.cpp

**Files:**
- Modify: `src/components/cli/cli.cpp:247-258`

- [ ] **Step 1: Replace play command**

Current:
```cpp
if (strcmp(cmd, "play") == 0 && result.key == cli_output::MessageKey::CONNECTING_STREAM) {
    (void)s_controller->postEvent(SystemEvent::PLAY_REQUESTED,
                                    SystemReason::USER_REQUEST);
```

New:
```cpp
if (strcmp(cmd, "play") == 0 && result.key == cli_output::MessageKey::CONNECTING_STREAM) {
    (void)s_controller->postEvent(SystemEvent::STATE_REQUESTED,
                                    SystemReason::USER_REQUEST,
                                    SystemState::LIVE);
```

- [ ] **Step 2: Replace stop command**

Current:
```cpp
} else if (strcmp(cmd, "stop") == 0 && result.key == cli_output::MessageKey::STREAM_STOPPED) {
    (void)s_controller->postEvent(SystemEvent::STOP_REQUESTED,
                                    SystemReason::USER_REQUEST);
```

New:
```cpp
} else if (strcmp(cmd, "stop") == 0 && result.key == cli_output::MessageKey::STREAM_STOPPED) {
    (void)s_controller->postEvent(SystemEvent::STATE_REQUESTED,
                                    SystemReason::USER_REQUEST,
                                    SystemState::READY);
```

- [ ] **Step 3: Replace reset command**

Current:
```cpp
} else if (strcmp(cmd, "reset") == 0 && result.key == cli_output::MessageKey::SESSION_RESET) {
    (void)s_controller->postEvent(SystemEvent::STOP_REQUESTED,
                                    SystemReason::USER_REQUEST);
```

New:
```cpp
} else if (strcmp(cmd, "reset") == 0 && result.key == cli_output::MessageKey::SESSION_RESET) {
    (void)s_controller->postEvent(SystemEvent::STATE_REQUESTED,
                                    SystemReason::USER_REQUEST,
                                    SystemState::READY);
```

### Task A6: Update debug_cli.cpp

**Files:**
- Modify: `src/components/cli/debug_cli.cpp:48,62`

- [ ] **Step 1: Replace debug "ready" transition** (line 48)

Current:
```cpp
(void)s_controller->postEvent(SystemEvent::STOP_REQUESTED, SystemReason::USER_REQUEST);
```

New:
```cpp
(void)s_controller->postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::READY);
```

- [ ] **Step 2: Replace debug "sleep" transition** (line 62)

Current:
```cpp
(void)s_controller->postEvent(SystemEvent::ENTER_SLEEP, SystemReason::USER_REQUEST);
```

New:
```cpp
(void)s_controller->postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::SLEEP);
```

Note: The "error" debug transition at line 68 uses `COMPONENT_SETUP_FAILED` — this stays.

### Task A7: Update test_state_transition_flow

**Files:**
- Modify: `test/test_state_transition_flow/test_main.cpp`

Every `PLAY_REQUESTED` → `STATE_REQUESTED(..., LIVE)`. Every `STOP_REQUESTED` → `STATE_REQUESTED(..., READY)`. Every `ENTER_SLEEP` → `STATE_REQUESTED(..., SLEEP)`.

- [ ] **Step 1: Update TransitionHooksFixture::reachSleep()** (line 53)
```cpp
controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::SLEEP);
```

- [ ] **Step 2: Update standalone reachSleep()** (line 62)
```cpp
c.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::SLEEP);
```

- [ ] **Step 3: Replace PLAY_REQUESTED** (lines 71, 86, 102, 106, 118, 135, 152, 209, 224, 229, 244, 251, 268, 288, 302, 345, 379)
Each → `postEvent(SystemEvent::STATE_REQUESTED, ..., SystemState::LIVE)`

- [ ] **Step 4: Replace STOP_REQUESTED** (lines 123, 158, 212)
Each → `postEvent(SystemEvent::STATE_REQUESTED, ..., SystemState::READY)`

- [ ] **Step 5: Replace ENTER_SLEEP** (lines 140, 162)
Each → `postEvent(SystemEvent::STATE_REQUESTED, ..., SystemState::SLEEP)`

- [ ] **Step 6: Update comments** (lines 228, 250)
`PLAY_REQUESTED` → `STATE_REQUESTED(LIVE)` in comments.

### Task A8: Update test_mailbox_contract

**Files:**
- Modify: `test/test_mailbox_contract/test_main.cpp`

- [ ] **Step 1: Replace all ENTER_SLEEP calls** (lines 9, 11, 14, 20)

Line 9:
```cpp
controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::SLEEP);
```
Line 11:
```cpp
controller.postEventBuffered(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::SLEEP);
```
Line 14 comment:
```
// STATE_REQUESTED(SLEEP) (the last buffered event) was processed: SLEEP→SLEEP is ignored.
```
Line 20:
```cpp
controller.postEventBuffered(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::SLEEP);
```

### Task A9: Update test_component_types

**Files:**
- Modify: `test/test_component_types/test_main.cpp:77-83`

- [ ] **Step 1: Update events test array**

Current (6 events) → New (2 events):
```cpp
const std::array<EnumLabelCase<SystemEvent>, 2> events = {{
    {SystemEvent::COMPONENT_SETUP_FAILED, "COMPONENT_SETUP_FAILED"},
    {SystemEvent::STATE_REQUESTED, "STATE_REQUESTED"},
}};
```

### Task A10: Update test_transition_timeout_watchdog

**Files:**
- Modify: `test/test_transition_timeout_watchdog/test_main.cpp:25,52`

These use `SystemEvent::PLAY_REQUESTED`. Replace with `SystemEvent::STATE_REQUESTED`.

### Task A12: Update event-contract-model.md

**Files:**
- Modify: `docs/event-contract-model.md`

- [ ] **Step 1: Update the event count note** (line 57 area)

Current:
```
> **Note:** `STATE_REQUESTED` / `STATE_ENTERED` / `STATE_FAILED` is the contract model. `STATE_REQUESTED(targetState)` is implemented as a `SystemEvent` enum value with a `SystemState` payload in the Mailbox. Legacy specific event names (`PLAY_REQUESTED`, `STOP_REQUESTED`, `ENTER_SLEEP`) coexist as concrete aliases for backward compatibility.
```

New:
```
> **Note:** `STATE_REQUESTED` / `STATE_ENTERED` / `STATE_FAILED` is the contract model. The event enum now contains only two entries: `COMPONENT_SETUP_FAILED` (failure signal) and `STATE_REQUESTED(targetState)` (all state intents). Legacy aliases have been removed.
```

- [ ] **Step 2: Update the naming standards table** (line 119-127 area)

Remove the `PLAY_REQUESTED`/`STOP_REQUESTED`/`ENTER_SLEEP` entries from the "Legacy component events" row. Keep `COMPONENT_SETUP_FAILED` as the only non-STATE_REQUESTED event.

### Task A13: Build and Verify

- [ ] **Step 1: Build production**

Run: `~/.platformio/penv/bin/platformio run -e production`
Expected: SUCCESS

- [ ] **Step 2: Run native tests**

Run: `~/.platformio/penv/bin/platformio test -e native`
Expected: All passing tests still pass. 4 legacy tests still ERROR (unchanged).

---

## Stage B: Remove TransitionRequestDecision Enum + Queued Transition

The entire `TransitionRequestDecision` enum is removed. `requestTransition()` returns `bool` instead. `Started` and `Superseded` both mean "true" — the caller doesn't distinguish. `beginOrchestration()` simplifies to `if (!requestTransition(...))`.

### Task B1: Remove TransitionRequestDecision enum from supervisor.h

**Files:**
- Modify: `src/state_machine/supervisor.h:99-128`

- [ ] **Step 1: Delete the entire TransitionRequestDecision block**

Delete lines 99-128:
```cpp
#define TRANSITION_REQUEST_DECISION_X(V) \
    V(Ignored) \
    V(Started) \
    V(Superseded) \
    V(Queued)

#define TRANSITION_REQUEST_DECISION_ENUM(name) name,
enum class TransitionRequestDecision : uint8_t {
    TRANSITION_REQUEST_DECISION_X(TRANSITION_REQUEST_DECISION_ENUM)
};

inline const char* toString(TransitionRequestDecision decision) {
    switch (decision) {
#define TRANSITION_REQUEST_DECISION_STRING(name) case TransitionRequestDecision::name: return #name;
        TRANSITION_REQUEST_DECISION_X(TRANSITION_REQUEST_DECISION_STRING)
#undef TRANSITION_REQUEST_DECISION_STRING
    }
    return "UNKNOWN";
}

#undef TRANSITION_REQUEST_DECISION_ENUM
#undef TRANSITION_REQUEST_DECISION_X
```

- [ ] **Step 2: Update requestTransition declaration in supervisor.h**

Current:
```cpp
TransitionRequestDecision requestTransition(SystemState from, SystemState target, uint32_t transitionId);
```

New:
```cpp
bool requestTransition(SystemState from, SystemState target, uint32_t transitionId);
```

### Task B2: Remove queued state from supervisor private members

**Files:**
- Modify: `src/state_machine/supervisor.h:255-256`

- [ ] **Step 1: Delete queued transition fields**

Delete:
```cpp
    bool hasQueuedStateTransition_ = false;
    StateTransitionInfo queuedStateTransition_{};
```

### Task B3: Simplify requestTransition to return bool

**Files:**
- Modify: `src/state_machine/supervisor.cpp`

- [ ] **Step 1: Replace the entire function**

Current:
```cpp
TransitionRequestDecision Supervisor::requestTransition(SystemState from,
                                                              SystemState target,
                                                              uint32_t transitionId) {
    if (from == target) {
        return TransitionRequestDecision::Ignored;
    }
    if (!hasActiveStateTransition_) {
        hasActiveStateTransition_ = true;
        activeStateTransition_ = StateTransitionInfo{transitionId, from, target};
        return TransitionRequestDecision::Started;
    }
    if (target == activeStateTransition_.from) {
        const StateTransitionInfo previous = activeStateTransition_;
        (void)previous;
        activeStateTransition_ = StateTransitionInfo{transitionId, from, target};
        hasQueuedStateTransition_ = false;
        return TransitionRequestDecision::Superseded;
    }
    queuedStateTransition_ = StateTransitionInfo{transitionId, from, target};
    hasQueuedStateTransition_ = true;
    return TransitionRequestDecision::Queued;
}
```

New:
```cpp
bool Supervisor::requestTransition(SystemState from,
                                        SystemState target,
                                        uint32_t transitionId) {
    if (from == target) {
        return false;
    }
    if (!hasActiveStateTransition_) {
        hasActiveStateTransition_ = true;
        activeStateTransition_ = StateTransitionInfo{transitionId, from, target};
        return true;
    }
    if (target == activeStateTransition_.from) {
        activeStateTransition_ = StateTransitionInfo{transitionId, from, target};
        return true;
    }
    return false;
}
```

### Task B4: Simplify finishTransition

**Files:**
- Modify: `src/state_machine/supervisor.cpp`

- [ ] **Step 1: Remove queue promotion logic**

Current:
```cpp
bool Supervisor::finishTransition(uint32_t transitionId) {
    if (!hasActiveStateTransition_) return false;
    if (activeStateTransition_.transitionId != transitionId) return false;
    if (hasQueuedStateTransition_) {
        activeStateTransition_ = queuedStateTransition_;
        hasQueuedStateTransition_ = false;
        if (activeStateTransition_.target == observedState_) {
            hasActiveStateTransition_ = false;
        }
        return true;
    }
    hasActiveStateTransition_ = false;
    return true;
}
```

New:
```cpp
bool Supervisor::finishTransition(uint32_t transitionId) {
    if (!hasActiveStateTransition_) return false;
    if (activeStateTransition_.transitionId != transitionId) return false;
    hasActiveStateTransition_ = false;
    return true;
}
```

### Task B5: Simplify beginOrchestration decision check

**Files:**
- Modify: `src/state_machine/supervisor.cpp`

- [ ] **Step 1: Change the return-value check**

Current (within `beginOrchestration`):
```cpp
const TransitionRequestDecision decision = requestTransition(observedState_, target, transitionId);
if (decision == TransitionRequestDecision::Ignored || decision == TransitionRequestDecision::Queued) {
    return false;
}
```

New:
```cpp
if (!requestTransition(observedState_, target, transitionId)) {
    return false;
}
```

### Task B6: Remove queued transition public accessors from supervisor.h

**Files:**
- Modify: `src/state_machine/supervisor.h`

- [ ] **Step 1: Delete accessor declarations**

Delete:
```cpp
    bool hasQueuedTransition() const;
    uint32_t queuedTransitionId() const;
    SystemState queuedTransitionFrom() const;
    SystemState queuedTransitionTarget() const;
```

### Task B7: Remove queued transition accessor implementations

**Files:**
- Modify: `src/state_machine/supervisor.cpp`

- [ ] **Step 1: Delete implementations**

Delete:
```cpp
bool Supervisor::hasQueuedTransition() const { return hasQueuedStateTransition_; }
uint32_t Supervisor::queuedTransitionId() const { return queuedStateTransition_.transitionId; }
SystemState Supervisor::queuedTransitionFrom() const { return queuedStateTransition_.from; }
SystemState Supervisor::queuedTransitionTarget() const { return queuedStateTransition_.target; }
```

### Task B8: Update test_component_types — remove decision enum tests entirely

**Files:**
- Modify: `test/test_component_types/test_main.cpp`

- [ ] **Step 1: Remove the `decisions` array and its assertion call**

In `test_state_machine_labels_round_trip_and_are_unique`, delete these lines:
```cpp
    const std::array<EnumLabelCase<TransitionRequestDecision>, 4> decisions = {{
        {TransitionRequestDecision::Ignored, "Ignored"},
        {TransitionRequestDecision::Started, "Started"},
        {TransitionRequestDecision::Superseded, "Superseded"},
        {TransitionRequestDecision::Queued, "Queued"},
    }};
```
and the line:
```cpp
    assert_round_trip_and_uniqueness(decisions);
```

- [ ] **Step 2: Remove invalid decision reference**

In `test_state_machine_invalid_values_map_to_unknown`, delete:
```cpp
    const auto invalidDecision = static_cast<TransitionRequestDecision>(255);
```
and the line:
```cpp
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", toString(invalidDecision));
```

### Task B9: Fix test_transition_arbitration legacy test

**Files:**
- Modify: `test/test_transition_arbitration/test_main.cpp`

- [ ] **Step 1: Read and fix or remove Queued/TransitionRequestDecision references**

This file is already legacy-broken (ERROR at build). Remove any `TransitionRequestDecision` references so it at least compiles to a link error rather than a type-not-found error.

### Task B10: Build and Verify

- [ ] **Step 1: Build production**

Run: `~/.platformio/penv/bin/platformio run -e production`
Expected: SUCCESS

- [ ] **Step 2: Run native tests**

Run: `~/.platformio/penv/bin/platformio test -e native`
Expected: All passing tests still pass. 4 legacy tests still ERROR.

---

## Stage C: Data Structure Alignment

**This stage documents gaps and requires design decisions.** Not directly implementable until decisions are made.

### Gap C1: `TargetMode` vs `SystemState` naming

- **Guideline:** `enum class TargetMode` with 7 flat values
- **Code:** `enum class SystemState : uint8_t` with rank values (FATAL=0, ERROR=10, ..., LIVE=60)
- **Question:** Same enum, different name + values. The rank values are needed for `stateRank()` comparisons. Does `TargetMode` need rank values too?

### Gap C2: Missing `SubState` + `ActiveTransition` struct

- `SubState { PENDING, COMMITTED, FAILED }` — not in code
- `ActiveTransition { TargetMode target; SubState state; }` — not in code
- The code handles this implicitly via `orchestration_` context and `pendingTransitions_` array. Should these be replaced with an explicit `ActiveTransition`?

### Gap C3: `ComponentStatus` vs `ComponentLifecycleStatus`

- Guideline: `{ COMMITTED, FAILED, DEGRADED }`
- Code: `{ Unknown, Ready, Failed, Disabled }`
- Aligning would require all component reporting code to change.

### Gap C4: `StateRequirement` vs `ComponentStateMatrix`

- Guideline uses `TargetMode`-typed min/max fields
- Code uses `uint32_t` fields with timeout values mixed in
- Aligning would change the state matrix API and all component definitions.

### Gap C5: `RetryPolicy` (deferred)

- Not implemented. Error recovery is a placeholder. Defer to error recovery story.

- [ ] **Step 1: Present gaps for decision** (this document serves as the record)

---

## Verification

After all stages:
```bash
~/.platformio/penv/bin/platformio run -e production
~/.platformio/penv/bin/platformio test -e native
```
Expected: Production SUCCESS. All component test suites PASS. Legacy ERROR suites remain ERROR.
