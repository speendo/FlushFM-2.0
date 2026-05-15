# US-0037: Algorithmic Step-Through for STATE_REQUESTED — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the per-target `switch` block in `handleEvent()`'s `STATE_REQUESTED` handler with a hierarchy-driven step-through algorithm that uses rank comparison (`>`/`<`) to determine direction, then steps one orchestration at a time via `targetMode_` continuation.

**Architecture:** A new private method `stepTowardTarget()` encodes the stepping logic. `handleEvent()` sets `targetMode_` on each external request and invokes the stepper. `reportCompletion()` generalizes its continuation check from a hardcoded `READY+LIVE` guard to `observedState_ != targetMode_`, re-invoking `stepTowardTarget()` after each orchestration step.

**Tech Stack:** C++17, PlatformIO, Unity test framework (native target)

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `src/state_machine/supervisor.h` | Modify | Add `setup()`, `stepTowardTarget()`, rename `transitionTo` → `setObservedStateImmediate` |
| `src/state_machine/supervisor.cpp` | Modify | Core algorithm: guard `postEvent`, rework `handleEvent` STATE_REQUESTED block, new `stepTowardTarget()`, generalized `reportCompletion` continuation, `setup()` body |
| `src/main.cpp` | Modify | Call `s_system.setup()` instead of manually posting events from components' `setup()` loops |
| `test/test_state_transition_flow/test_main.cpp` | Modify | Update existing tests, add new tests for the algorithm boundaries |
| `docs/supervisor-api.md` | None | Already references `setObservedStateImmediate` and `setup()` — no changes needed |

---

### Task 1: Add `setup()` and rename `transitionTo` → `setObservedStateImmediate` in header

**Files:**
- Modify: `src/state_machine/supervisor.h`

- [ ] **Step 1: Rename `transitionTo` in declaration**

Replace:
```cpp
     void transitionTo(SystemState next, SystemEvent trigger, SystemReason reason, uint32_t transitionId = 0);
```
With:
```cpp
     void setObservedStateImmediate(SystemState next, SystemEvent trigger, SystemReason reason, uint32_t transitionId = 0);
```

- [ ] **Step 2: Add `setup()` and `stepTowardTarget()` declarations**

After the `void processMailbox();` line, add:

```cpp
     void setup();
```

In the private section, after `setObservedStateImmediate`, add:

```cpp
     void stepTowardTarget(SystemEvent event, SystemReason reason);
```

- [ ] **Step 3: Commit**

```bash
git add src/state_machine/supervisor.h
git commit -m "US-0037: add setup() and stepTowardTarget() declarations, rename transitionTo to setObservedStateImmediate"
```

---

### Task 2: Add `postEvent` guards rejecting BOOTING and CONNECTING

**Files:**
- Modify: `src/state_machine/supervisor.cpp`

- [ ] **Step 4: Add rejection guard to the three-parameter `postEvent` overload**

Replace the three-param `postEvent` with:

```cpp
bool Supervisor::postEvent(SystemEvent event, SystemReason reason, SystemState target) {
    if (event == SystemEvent::STATE_REQUESTED &&
        (target == SystemState::BOOTING || target == SystemState::CONNECTING)) {
        PROD_LOG(kLogSource, "Rejected STATE_REQUESTED for transient state %s", toString(target));
        return false;
    }
#if !defined(ARDUINO)
    mailbox_.targetState = target;
    handleEvent(event, reason);
    return true;
#else
    mailbox_.reason = reason;
    mailbox_.event = event;
    mailbox_.targetState = target;
    mailbox_.pending = true;
    return true;
#endif
}
```

- [ ] **Step 5: Commit**

```bash
git add src/state_machine/supervisor.cpp
git commit -m "US-0037: reject BOOTING and CONNECTING targets in postEvent"
```

---

### Task 3: Rename `transitionTo` → `setObservedStateImmediate` in implementation + all callers

**Files:**
- Modify: `src/state_machine/supervisor.cpp` (all references)

- [ ] **Step 6: Rename the method definition**

Replace:
```cpp
void Supervisor::transitionTo(SystemState next, ...
```
With:
```cpp
void Supervisor::setObservedStateImmediate(SystemState next, ...
```

- [ ] **Step 7: Rename all invocations of `transitionTo` inside `supervisor.cpp`**

Replace all occurrences of `transitionTo(` with `setObservedStateImmediate(` (six call sites: lines 90, 109, 219, 297, 462, 478, 488).

- [ ] **Step 8: Run tests to verify compilation**

Run: `pio test -e native`
Expected: All tests compile and pass (no behavioral changes yet).

- [ ] **Step 9: Commit**

```bash
git add src/state_machine/supervisor.cpp
git commit -m "US-0037: rename transitionTo to setObservedStateImmediate across all call sites"
```

---

### Task 4: Implement `stepTowardTarget()` — the hierarchy-driven stepping algorithm

**Files:**
- Modify: `src/state_machine/supervisor.cpp`

- [ ] **Step 10: Implement `stepTowardTarget()`**

Add this method after `checkTransitionTimeouts`:

```cpp
void Supervisor::stepTowardTarget(SystemEvent event, SystemReason reason) {
    // Do not step from ERROR/FATAL — recovery/exit is out of scope
    if (observedState_ == SystemState::ERROR || observedState_ == SystemState::FATAL) return;

    const uint8_t targetRank = stateRank(targetMode_);
    const uint8_t obsRank = stateRank(observedState_);

    if (targetRank == obsRank) {
        // LIVE replay: step to READY so auto-continuation brings us back to LIVE
        if (targetRank == 60) {
            uint32_t tid = nextTransitionId_;
            ++nextTransitionId_;
            if (nextTransitionId_ == 0) nextTransitionId_ = 1;
            (void)beginOrchestration(SystemState::READY, event, reason, tid);
        }
        return;
    }

    auto request = [this, event, reason](SystemState target) {
        uint32_t tid = nextTransitionId_;
        ++nextTransitionId_;
        if (nextTransitionId_ == 0) nextTransitionId_ = 1;
        (void)beginOrchestration(target, event, reason, tid);
    };

    if (targetRank > obsRank) {
        // Moving up: step through the L2 upward sequence
        if (obsRank <= 30) {
            setObservedStateImmediate(SystemState::CONNECTING, event, reason);
            request(SystemState::READY);
        } else {
            // obsRank == 50 (READY): step to LIVE
            request(SystemState::LIVE);
        }
    } else {
        // Moving down: obsRank > targetRank
        if (obsRank == 60) {
            // LIVE: step down through READY first
            request(SystemState::READY);
        } else {
            // READY to SLEEP: direct orchestration
            request(targetMode_);
        }
    }
}
```

- [ ] **Step 11: Commit**

```bash
git add src/state_machine/supervisor.cpp
git commit -m "US-0037: add stepTowardTarget() hierarchy-driven algorithm"
```

---

### Task 5: Rework `handleEvent()` STATE_REQUESTED block

**Files:**
- Modify: `src/state_machine/supervisor.cpp`

- [ ] **Step 12: Replace the `STATE_REQUESTED` handling in `handleEvent()`**

Replace lines 441–491 (the entire `if (event == SystemEvent::STATE_REQUESTED)` block through the `return;` just before `COMPONENT_SETUP_FAILED`):

```cpp
    if (event == SystemEvent::STATE_REQUESTED) {
        const SystemState target = mailbox_.targetState;

        // ERROR: immediate non-orchestrated state update
        if (target == SystemState::ERROR) {
            setObservedStateImmediate(observedState_ == SystemState::ERROR
                                          ? SystemState::FATAL : SystemState::ERROR,
                                      event, reason);
            return;
        }

        // FATAL: immediate non-orchestrated state update, halt all processing
        if (target == SystemState::FATAL) {
            targetMode_ = SystemState::FATAL;
            setObservedStateImmediate(SystemState::FATAL, event, reason);
            return;
        }

        // Orchestration in flight: store user intent, do not overwrite Mailbox targetState
        if (orchestration_.active) {
            targetMode_ = target;
            return;
        }

        // Set intent and step toward it (fixes READY bug: targetMode_ is now correct)
        targetMode_ = target;
        stepTowardTarget(event, reason);
        return;
    }
```

- [ ] **Step 13: Commit**

```bash
git add src/state_machine/supervisor.cpp
git commit -m "US-0037: replace per-target switch with hierarchy-driven STATE_REQUESTED handler"
```

---

### Task 6: Generalize `reportCompletion()` continuation logic

**Files:**
- Modify: `src/state_machine/supervisor.cpp`

- [ ] **Step 14: Replace the hardcoded READY+LIVE continuation in both locations**

In `reportCompletion()`, replace:

```cpp
        (void)finishTransition(transitionId);
        orchestration_.active = false;

        if (observedState_ == SystemState::READY && targetMode_ == SystemState::LIVE) {
            mailbox_.targetState = SystemState::LIVE;
            handleEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST);
        }
```

With:

```cpp
        (void)finishTransition(transitionId);
        orchestration_.active = false;

        if (observedState_ != targetMode_) {
            stepTowardTarget(orchestration_.trigger, orchestration_.reason);
        }
```

Also replace the same hardcoded pattern in `beginOrchestration()` at lines 301–303:

```cpp
        if (observedState_ == SystemState::READY && targetMode_ == SystemState::LIVE) {
            mailbox_.targetState = SystemState::LIVE;
            handleEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST);
        }
```

With:

```cpp
        if (observedState_ != targetMode_) {
            stepTowardTarget(orchestration_.trigger, orchestration_.reason);
        }
```

- [ ] **Step 15: Run tests**

Run: `pio test -e native`
Expected: Existing tests that test SLEEP→LIVE→... pass via the new continuation path.

- [ ] **Step 16: Commit**

```bash
git add src/state_machine/supervisor.cpp
git commit -m "US-0037: generalize reportCompletion continuation from READY+LIVE to observedState_ != targetMode_"
```

---

### Task 7: Update tests to match new behavior

**Files:**
- Modify: `test/test_state_transition_flow/test_main.cpp`

- [ ] **Step 17: Update `test_state_requested_booting_ignored` — now returns false**

```cpp
void test_state_requested_booting_ignored() {
    Supervisor controller;
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::BOOTING), static_cast<int>(controller.state()));

    TEST_ASSERT_FALSE(controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::BOOTING));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::BOOTING), static_cast<int>(controller.state()));
}
```

- [ ] **Step 18: Update `test_sleep_to_ready` — request READY from SLEEP**

```cpp
void test_sleep_to_ready() {
    TransitionHooksFixture fixture;
    fixture.install();
    fixture.reachSleep();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::READY));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING), static_cast<int>(fixture.controller.state()));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());

    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));
    TEST_ASSERT_FALSE(fixture.controller.isOrchestrationActive());
}
```

- [ ] **Step 19: Add new tests**

Add after the existing tests:

```cpp
void test_booting_rejected_by_postevent() {
    Supervisor controller;
    TEST_ASSERT_FALSE(controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::BOOTING));
}

void test_connecting_rejected_by_postevent() {
    Supervisor controller;
    TEST_ASSERT_FALSE(controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::CONNECTING));
}

void test_state_requested_during_orchestration_sets_targetmode() {
    TransitionHooksFixture fixture;
    fixture.install();
    fixture.reachSleep();
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::SLEEP));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING), static_cast<int>(fixture.controller.state()));

    fixture.completeAllActive();
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
}

void test_fatal_guard_blocks_state_requested() {
    Supervisor controller;
    controller.triggerFatal();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL), static_cast<int>(controller.state()));
    controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL), static_cast<int>(controller.state()));
}

void test_live_replay_continues_through_ready_back_to_live() {
    TransitionHooksFixture fixture;
    fixture.install();
    fixture.reachSleep();
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE));
    fixture.completeAllActive();
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));
    TEST_ASSERT_FALSE(fixture.controller.isOrchestrationActive());

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());

    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));
    TEST_ASSERT_FALSE(fixture.controller.isOrchestrationActive());
}

void test_sleep_target_from_live_steps_down_through_ready() {
    TransitionHooksFixture fixture;
    fixture.install();
    fixture.reachSleep();
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE));
    fixture.completeAllActive();
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::SLEEP));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));
    TEST_ASSERT_FALSE(fixture.controller.isOrchestrationActive());
}
```

Register them in `main()`:

```cpp
    RUN_TEST(test_booting_rejected_by_postevent);
    RUN_TEST(test_connecting_rejected_by_postevent);
    RUN_TEST(test_state_requested_during_orchestration_sets_targetmode);
    RUN_TEST(test_fatal_guard_blocks_state_requested);
    RUN_TEST(test_live_replay_continues_through_ready_back_to_live);
    RUN_TEST(test_sleep_target_from_live_steps_down_through_ready);
```

- [ ] **Step 20: Run tests to verify**

Run: `pio test -e native`
Expected: All 28 tests pass.

- [ ] **Step 21: Commit**

```bash
git add test/test_state_transition_flow/test_main.cpp
git commit -m "US-0037: update tests for algorithmic step-through and add boundary coverage"
```

---

### Task 8: Implement `Supervisor::setup()` and wire it in `main.cpp`

**Files:**
- Modify: `src/state_machine/supervisor.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 22: Implement `Supervisor::setup()`**

Add after the constructor definition:

```cpp
void Supervisor::setup() {
    if (observedState_ != SystemState::BOOTING) return;
    targetMode_ = SystemState::LIVE;
    setObservedStateImmediate(SystemState::CONNECTING,
                              SystemEvent::STATE_REQUESTED,
                              SystemReason::COMPONENT_SETUP);
    uint32_t tid = nextTransitionId_;
    ++nextTransitionId_;
    if (nextTransitionId_ == 0) nextTransitionId_ = 1;
    (void)beginOrchestration(SystemState::READY,
                             SystemEvent::STATE_REQUESTED,
                             SystemReason::COMPONENT_SETUP,
                             tid);
}
```

- [ ] **Step 23: Wire `s_system.setup()` in `main.cpp`**

Replace the component-setup loop:

```cpp
    for (ISystemComponent* component : s_components) {
        component->registerWithController(s_system);
        if (!component->setup()) {
            ERROR_LOG(kLogSource, "Component setup failed: %s", component->name());
            s_system.postEvent(SystemEvent::COMPONENT_SETUP_FAILED, SystemReason::COMPONENT_SETUP);
        }
        s_system.processMailbox();
    }

    PROD_LOG(kLogSource, "Boot auto-play: queue PLAY request");
    (void)s_system.postEvent(SystemEvent::STATE_REQUESTED,
                             SystemReason::USER_REQUEST,
                             SystemState::LIVE);
```

With:

```cpp
    for (ISystemComponent* component : s_components) {
        component->registerWithController(s_system);
    }

    (void)s_system.setup();
```

- [ ] **Step 24: Run `pio test -e native`**

Expected: All tests pass.

- [ ] **Step 25: Commit**

```bash
git add src/state_machine/supervisor.cpp src/main.cpp
git commit -m "US-0037: implement setup() idempotent boot entry, wire into main.cpp"
```

---

### Task 9: Verification — full test run and build check

- [ ] **Step 26: Run native tests**

```bash
pio test -e native
```
Expected: All tests pass.

- [ ] **Step 27: Build production target**

```bash
pio run -e production
```
Expected: Build succeeds with no warnings.

- [ ] **Step 28: No doc updates needed — `docs/supervisor-api.md` already references `setObservedStateImmediate` and `setup()`**

---

## Summary of Changes

### `supervisor.h` (+3 lines)
- Rename `transitionTo` → `setObservedStateImmediate`
- Add `void setup()` public method
- Add `void stepTowardTarget(SystemEvent, SystemReason)` private helper

### `supervisor.cpp` (net ≈-40 lines)
- **Remove:** ~45 lines of per-target `switch` in `handleEvent` STATE_REQUESTED block
- **Add:** `stepTowardTarget()` (~35 lines), `setup()` (~10 lines), `postEvent` guards (~4 lines)
- **Replace:** 2 hardcoded continuation blocks → generalized `observedState_ != targetMode_` checks
- Fix `targetMode_ = READY` bug (was incorrectly `SLEEP`)

### `main.cpp` (-7 lines)
- Replace explicit component-setup loop + postEvent with `s_system.setup()` call

### Test file (+~120 lines)
- Update BOOTING rejection test (returns false now)
- Add 6 new boundary tests
- Target: 28 tests passing
