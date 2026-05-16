# Code Review Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix 13 code review findings in the supervisor V2 subsystem — race conditions, uninitialized members, null dereferences, time unit bugs, polling waste, and design gaps.

**Architecture:** All fixes are within `src/supervisor/` (orchestrator, state machine, supervisor). Each task is self-contained, ordered by dependency. Tasks 2-3 have a sequential dependency (Task 3 assumes Task 2's consume() signature change). All other tasks are independent. Tests use Unity framework and `#define private public` to access internals.

**Tech Stack:** C++20, FreeRTOS (ESP32-S3), Unity test framework, PlatformIO native environment.

---

## File Map

| File | Responsibility | Tasks |
|------|---------------|-------|
| `src/supervisor/orchestrator.h` | OrchestrationOrder, OrchestrationResponse structs | 2, 3 |
| `src/supervisor/orchestrator.cpp` | startOrchestration, orchestrationWorker, completeTransition, postStateRequest, postErrorEvent, checkOrchestrationResponse | 1, 2, 3, 4, 6 |
| `src/supervisor/supervisor_v2.h` | SupervisorV2 class, member declarations | 1, 5 |
| `src/supervisor/supervisor_v2.cpp` | setup(), constructor | 1 |
| `src/supervisor/state_machine.cpp` | getNextState, stepTowardTarget, handleFatal | 1, 5 |
| `test/test_supervisor_v2_orchestration/test_main.cpp` | Start orchestration, complete transition, check response tests | 3 |
| `test/test_supervisor_v2_step_6/test_main.cpp` | setObservedState, setTargetState, handleFatal tests | 5 |
| `test/test_supervisor_v2_run/test_main.cpp` | run() loop tests | 5 |

---

### Task 1: Trivial batch (#5, #6, #10, #13, #15, #16)

**Files:**
- Modify: `src/supervisor/supervisor_v2.h` — add member initializers, add `firstOrchestration_` flag
- Modify: `src/supervisor/supervisor_v2.cpp` — add EventGroup NULL check
- Modify: `src/supervisor/orchestrator.cpp` — guard xTaskNotifyGive, fix comment in completeTransition, add checkComponentPresence call in startOrchestration
- Modify: `src/supervisor/state_machine.cpp` — fix comment in getNextState

- [ ] **Step 1: Add member initializers (#5)**

Edit `src/supervisor/supervisor_v2.h`. Add initializers to the four uninitialized members and the `ActiveTransition` struct:

In the `ActiveTransition` struct (line 101-104), add an initializer for `transitionTarget`:
```cpp
struct ActiveTransition {
    SystemState transitionTarget{SystemState::BOOTING};
    SubState subState = SubState::PENDING;
};
```

In the `SupervisorV2` class (lines 329-371), change:
```cpp
SystemState observedState_;
SystemState targetState_;
```
to:
```cpp
SystemState observedState_{SystemState::BOOTING};
SystemState targetState_{SystemState::BOOTING};
```

And change (line 371):
```cpp
SystemState lastTargetBeforeError_;
```
to:
```cpp
SystemState lastTargetBeforeError_{SystemState::BOOTING};
```

- [ ] **Step 2: Add null handle guard (#6)**

Edit `src/supervisor/orchestrator.cpp`. In `postStateRequest()` (line 45), wrap `xTaskNotifyGive`:
```cpp
void SupervisorV2::postStateRequest(SystemState target) {
    portENTER_CRITICAL(&stateRequestMailbox_.spinlock);
    stateRequestMailbox_.pending = true;
    stateRequestMailbox_.requestedTarget = target;
    portEXIT_CRITICAL(&stateRequestMailbox_.spinlock);

    if (supervisorTaskHandle_ != nullptr) {
        xTaskNotifyGive(supervisorTaskHandle_);
    }
}
```

In `postErrorEvent()` (line 57), wrap the same way:
```cpp
    if (supervisorTaskHandle_ != nullptr) {
        xTaskNotifyGive(supervisorTaskHandle_);
    }
```

- [ ] **Step 3: Add EventGroup NULL check (#10)**

Edit `src/supervisor/supervisor_v2.cpp`. In `setup()`, after `xEventGroupCreateStatic` (line 5-8), add a null check:
```cpp
void SupervisorV2::setup() {
    eventGroup_ = xEventGroupCreateStatic(&eventGroupBuffer_);
    if (eventGroup_ == nullptr) {
        // Allocation failure — cannot operate without event group.
        // On native builds this never happens; on hardware it means OOM.
        return;
    }
    loadTransitionTimeoutConfig();

    supervisorTaskHandle_ = xTaskGetCurrentTaskHandle();

    xTaskCreatePinnedToCore(
        orchestrationWorker,
        "OrchWorker",
        4096,
        this,
        1,
        &workerTaskHandle_,
        0
    );
}
```

- [ ] **Step 4: Fix completeTransition comment (#13)**

Edit `src/supervisor/orchestrator.cpp`. In `completeTransition()`, change the comment block (lines 24-30) from:
```cpp
    // Component reported Failed. How we handle it depends on whether this
    // component is required or optional:
    //   - Required: post an error event which the supervisor consumes on the
    //     next run() tick. This sets targetState_ to ERROR and aborts the
    //     current orchestration. The recovery logic then decides what to do.
    //   - Optional: mark as DEGRADED and exclude from the orchestration
    //     quorum. The remaining components are expected to finish normally.
```
to:
```cpp
    // Component reported Failed. How we handle it depends on whether this
    // component is required or optional:
    //   - Required: post an error event; the state machine processes it after
    //     the current orchestration cycle ends (via timeout or completion),
    //     then decides the next target.
    //   - Optional: mark as DEGRADED and exclude from the orchestration
    //     quorum. The remaining components are expected to finish normally.
```

- [ ] **Step 5: Fix getNextState comment (#15)**

Edit `src/supervisor/state_machine.cpp`. In `getNextState()`, change line 22 from:
```cpp
    // ERROR and FATAL as target are immediate — no stepping needed.
```
to:
```cpp
    // ERROR and FATAL as target are immediate target selection (no stepping).
    // Components still go through orchestration to acknowledge the error state.
```

- [ ] **Step 6: Add firstOrchestration_ flag and call checkComponentPresence (#16)**

Edit `src/supervisor/supervisor_v2.h`. In the private member section, add after `fatalEntered_` (around line 365):
```cpp
    bool fatalDeadlineElapsed_{};
    bool fatalEntered_{};
    bool firstOrchestration_{true};
```

Edit `src/supervisor/orchestrator.cpp`. In `startOrchestration()`, add at the top (after setting nextState_.transitionTarget):
```cpp
void SupervisorV2::startOrchestration(SystemState target) {
    nextState_.transitionTarget = target;

    if (firstOrchestration_) {
        firstOrchestration_ = false;
        checkComponentPresence();
    }

    // ... rest of existing code
```

- [ ] **Step 7: Run all existing tests to verify no regressions**

Run: `pio test -e native`
Expected: All tests pass. If any test referencing `orderMailbox_.deadlineMs` fails, that's expected — it will be fixed in Task 3.

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "fix: uninitialized members, null guards, comment fixes, checkComponentPresence call (#5, #6, #10, #13, #15, #16)"
```

---

### Task 2: consume() race fix for OrchestrationOrder and OrchestrationResponse (#1, #2)

**Files:**
- Modify: `src/supervisor/orchestrator.h` — change consume() signatures to copy fields under lock
- Modify: `src/supervisor/orchestrator.cpp` — update call sites in worker and checkOrchestrationResponse

- [ ] **Step 1: Change OrchestrationOrder::consume() signature**

Edit `src/supervisor/orchestrator.h`. Replace the `consume()` method in `OrchestrationOrder` (lines 47-53):
```cpp
    /** @brief Copy all fields under spinlock and clear the pending flag.
     *  @param outBits Receives the expected bits mask.
     *  @param outDeadline Receives the deadline tick.
     *  @param outTarget Receives the transition target state.
     *  @return true if an order was pending and was consumed.
     */
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
```

- [ ] **Step 2: Change OrchestrationResponse::consume() signature**

Edit `src/supervisor/orchestrator.h`. Replace the `consume()` method in `OrchestrationResponse` (lines 76-82):
```cpp
    /** @brief Copy all fields under spinlock and clear the pending flag.
     *  @param outResult Receives the orchestration result.
     *  @param outTimedOut Receives the timed-out component bits.
     *  @return true if a response was pending and was consumed.
     */
    bool consume(OrchestrationResult& outResult, EventBits_t& outTimedOut) {
        portENTER_CRITICAL(&spinlock);
        if (!pending) { portEXIT_CRITICAL(&spinlock); return false; }
        outResult = result;
        outTimedOut = timedOutComponents;
        pending = false;
        portEXIT_CRITICAL(&spinlock);
        return true;
    }
```

- [ ] **Step 3: Update orchestrationWorker call site**

Edit `src/supervisor/orchestrator.cpp`. Replace the worker loop body (lines 132-173) with:
```cpp
void orchestrationWorker(void* param) {
    auto* supervisor = static_cast<SupervisorV2*>(param);
    for (;;) {
        EventBits_t expectedBits;
        TickType_t deadlineTicks;
        SystemState transitionTarget;
        if (!supervisor->orderMailbox_.consume(expectedBits, deadlineTicks, transitionTarget)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        TickType_t now = xTaskGetTickCount();
        TickType_t waitTicks = pdMS_TO_TICKS(deadlineTicks - now);

        EventBits_t bits = xEventGroupWaitBits(supervisor->eventGroup_,
                                                 expectedBits,
                                                 pdTRUE,
                                                 pdTRUE,
                                                 waitTicks);

        if ((bits & expectedBits) == expectedBits) {
            supervisor->responseMailbox_.post(OrchestrationResult::COMPLETED, 0);
        } else {
            EventBits_t missing = expectedBits & ~bits;
            supervisor->responseMailbox_.post(OrchestrationResult::TIMED_OUT, missing);
        }

        xTaskNotifyGive(supervisor->supervisorTaskHandle_);
    }
}
```

- [ ] **Step 4: Update checkOrchestrationResponse call site**

Edit `src/supervisor/orchestrator.cpp`. Replace `checkOrchestrationResponse()` (lines 98-123):
```cpp
void SupervisorV2::checkOrchestrationResponse() {
    OrchestrationResult result;
    EventBits_t timedOutComponents;
    if (!responseMailbox_.consume(result, timedOutComponents)) return;

    hasActiveOrchestration_ = false;

    if (result == OrchestrationResult::COMPLETED) {
        nextState_.subState = SubState::COMMITTED;
        setObservedState(nextState_.transitionTarget);
    } else {
        for (size_t i = 0; i < componentCount; i++) {
            if (!(timedOutComponents & (1 << i))) continue;
            if (isRequired_[i]) {
                componentStatuses_[i] = ComponentStatus::FAILED;
                postErrorEvent("transition timeout", static_cast<ComponentID>(i));
            } else {
                componentStatuses_[i] = ComponentStatus::DEGRADED;
            }
        }
    }
}
```

- [ ] **Step 5: Run tests**

Run: `pio test -e native`
Expected: All tests pass.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "fix: consume() race — copy fields under spinlock (#1, #2)"
```

---

### Task 3: Mixed time units and underflow in deadline calculation (#3)

**Files:**
- Modify: `src/supervisor/orchestrator.h` — rename `deadlineMs` to `deadlineTicks`, update `post()` and `consume()` references
- Modify: `src/supervisor/orchestrator.cpp` — fix startOrchestration to convert ms to ticks, update worker to use capped subtraction
- Modify: `test/test_supervisor_v2_orchestration/test_main.cpp` — update field name in test

- [ ] **Step 1: Rename deadlineMs to deadlineTicks**

Edit `src/supervisor/orchestrator.h`. In `OrchestrationOrder`, line 31:
```cpp
    TickType_t deadlineMs = 0;
```
change to:
```cpp
    TickType_t deadlineTicks = 0;
```

- [ ] **Step 1.5: Update post() and consume() references**

Edit `src/supervisor/orchestrator.h`. In `OrchestrationOrder::post()` (line 38), the field write references the old name:
```cpp
        deadlineMs = deadline;
```
change to:
```cpp
        deadlineTicks = deadline;
```

In `OrchestrationOrder::consume()` (line 209, added by Task 2), the field read references the old name:
```cpp
        outDeadline = deadlineMs;
```
change to:
```cpp
        outDeadline = deadlineTicks;
```

- [ ] **Step 2: Fix startOrchestration deadline computation**

Edit `src/supervisor/orchestrator.cpp`. In `startOrchestration()`, replace the `orderMailbox_.post()` call (lines 89-90):
```cpp
    orderMailbox_.post(bits, xTaskGetTickCount() + getTransitionTimeout(target,
        getIndex(target) > getIndex(observedState_)), target);
```
with:
```cpp
    TickType_t timeoutTicks = pdMS_TO_TICKS(getTransitionTimeout(target,
        getIndex(target) > getIndex(observedState_)));
    TickType_t deadline = xTaskGetTickCount() + timeoutTicks;
    orderMailbox_.post(bits, deadline, target);
```

- [ ] **Step 3: Fix worker deadline computation**

Edit `src/supervisor/orchestrator.cpp`. In `orchestrationWorker()`, replace the existing time computation (the lines that compute `waitTicks` before `xEventGroupWaitBits`):
```cpp
        TickType_t now = xTaskGetTickCount();
        TickType_t waitTicks = pdMS_TO_TICKS(deadlineTicks - now);
```
with:
```cpp
        TickType_t rawWait = deadlineTicks - xTaskGetTickCount();
        // If rawWait exceeds the maximum possible timeout, the deadline
        // has already passed — use zero wait. The 60s cap is well above
        // any configured transition timeout.
        TickType_t waitTicks = (rawWait < pdMS_TO_TICKS(60000)) ? rawWait : 0;
```

- [ ] **Step 4: Update test field name**

Edit `test/test_supervisor_v2_orchestration/test_main.cpp`. In `test_start_orchestration_sets_deadline_in_order` (line 109):
```cpp
    TEST_ASSERT_NOT_EQUAL(0, supervisor.orderMailbox_.deadlineMs);
```
change to:
```cpp
    TEST_ASSERT_NOT_EQUAL(0, supervisor.orderMailbox_.deadlineTicks);
```

- [ ] **Step 5: Run tests**

Run: `pio test -e native`
Expected: All tests pass.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "fix: mixed time units and underflow in deadline calculation (#3)"
```

---

### Task 4: EventGroup bits cleared after mailbox writes (#4)

**Files:**
- Modify: `src/supervisor/orchestrator.cpp` — reorder clear-and-write in startOrchestration

- [ ] **Step 1: Move xEventGroupClearBits before the mailbox write loop**

Edit `src/supervisor/orchestrator.cpp`. In `startOrchestration()`, move the clear bits call. The current order (roughly):
```
1. Set nextState_.transitionTarget
2. Loop: build bits + postNextComponentState
3. xEventGroupClearBits
4. Set nextState_.subState / hasActiveOrchestration_
5. orderMailbox_.post
```

Change to:
```
1. Set nextState_.transitionTarget
2. xEventGroupClearBits(eventGroup_, kAllComponentBits);   // ← moved up
3. Loop: build bits + postNextComponentState                // ← components see cleared bits
4. Set nextState_.subState / hasActiveOrchestration_
5. orderMailbox_.post
```

The exact change in the file: move line 84 (`xEventGroupClearBits(eventGroup_, kAllComponentBits);`) to just after line 68 (`nextState_.transitionTarget = target;`) and before the loop at line 74.

The resulting `startOrchestration()` should look like:
```cpp
void SupervisorV2::startOrchestration(SystemState target) {
    nextState_.transitionTarget = target;

    if (firstOrchestration_) {
        firstOrchestration_ = false;
        checkComponentPresence();
    }

    xEventGroupClearBits(eventGroup_, kAllComponentBits);

    EventBits_t bits = 0;
    for (size_t i = 0; i < componentCount; i++) {
        if (componentMailboxes_[i] == nullptr) continue;
        if (componentStatuses_[i] != ComponentStatus::DEGRADED) {
            bits |= (1 << i);
        }
        postNextComponentState(static_cast<ComponentID>(i));
    }

    nextState_.subState = SubState::PENDING;
    hasActiveOrchestration_ = true;

    TickType_t timeoutTicks = pdMS_TO_TICKS(getTransitionTimeout(target,
        getIndex(target) > getIndex(observedState_)));
    TickType_t deadline = xTaskGetTickCount() + timeoutTicks;
    orderMailbox_.post(bits, deadline, target);
}
```

- [ ] **Step 2: Run tests**

Run: `pio test -e native`
Expected: All tests pass.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "fix: clear EventGroup bits before writing component mailboxes (#4)"
```

---

### Task 5: TickType_t wrap-around in handleFatal (#12)

**Files:**
- Modify: `src/supervisor/supervisor_v2.h` — rename `fatalDeadlineMs_` to `fatalEnteredTicks_`
- Modify: `src/supervisor/state_machine.cpp` — change handleFatal to delta comparison
- Modify: `src/supervisor/supervisor_v2.cpp` — update any reference to the renamed field (there should be none since it's just a member declaration)
- Modify: `test/test_supervisor_v2_step_6/test_main.cpp` — update tests for renamed field
- Modify: `test/test_supervisor_v2_run/test_main.cpp` — update test for renamed field

- [ ] **Step 1: Rename fatalDeadlineMs_ to fatalEnteredTicks_ in header**

Edit `src/supervisor/supervisor_v2.h`. Change line 363:
```cpp
    TickType_t fatalDeadlineMs_{};
```
to:
```cpp
    TickType_t fatalEnteredTicks_{};
```

- [ ] **Step 2: Rewrite handleFatal to use delta comparison**

Edit `src/supervisor/state_machine.cpp`. Replace `handleFatal()` (lines 228-241):
```cpp
void SupervisorV2::handleFatal() {
    if (!fatalEntered_) {
        fatalEntered_ = true;
        fatalEnteredTicks_ = xTaskGetTickCount();
        return;
    }

    if ((xTaskGetTickCount() - fatalEnteredTicks_) >= pdMS_TO_TICKS(kFatalDwellMs)) {
        fatalDeadlineElapsed_ = true;
#if defined(ARDUINO)
        esp_deep_sleep_start();
#endif
    }
}
```

- [ ] **Step 3: Update step_6 test file**

Edit `test/test_supervisor_v2_step_6/test_main.cpp`. Three tests reference the old field:

In `test_handle_fatal_sets_deadline_on_first_call` (line 122):
```cpp
    TEST_ASSERT_NOT_EQUAL(0, supervisor.fatalDeadlineMs_);
```
change to:
```cpp
    TEST_ASSERT_NOT_EQUAL(0, supervisor.fatalEnteredTicks_);
```

In `test_handle_fatal_detects_elapsed_deadline` (lines 136-144), the old test set `fatalDeadlineMs_ = 0` which was a "past deadline" value (xTaskGetTickCount() would always be >= 0). With the delta approach, `(0 - 0) >= 60000` is false. Change to set `fatalEnteredTicks_` to a value that makes the delta large:
```cpp
void test_handle_fatal_detects_elapsed_deadline() {
    SupervisorV2 supervisor;
    supervisor.fatalEntered_ = true;
    supervisor.fatalEnteredTicks_ = 1;  // xTaskGetTickCount() returns 0, so 0 - 1 = UINT32_MAX >= 60000 ✓
    supervisor.fatalDeadlineElapsed_ = false;

    supervisor.handleFatal();

    TEST_ASSERT_TRUE(supervisor.fatalDeadlineElapsed_);
}
```

- [ ] **Step 4: Update run test file**

Edit `test/test_supervisor_v2_run/test_main.cpp`. In `test_run_calls_handle_fatal` (line 211):
```cpp
    TEST_ASSERT_NOT_EQUAL(0, supervisor.fatalDeadlineMs_);
```
change to:
```cpp
    TEST_ASSERT_NOT_EQUAL(0, supervisor.fatalEnteredTicks_);
```

- [ ] **Step 5: Run tests**

Run: `pio test -e native`
Expected: All tests pass.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "fix: TickType wrap in handleFatal — delta comparison instead of absolute (#12)"
```

---

### Task 6: Polling busy-loop in orchestrationWorker (#9)

**Files:**
- Modify: `src/supervisor/orchestrator.cpp` — replace vTaskDelay with ulTaskNotifyTake, add signal in startOrchestration

- [ ] **Step 1: Replace vTaskDelay with ulTaskNotifyTake in worker**

Edit `src/supervisor/orchestrator.cpp`. In `orchestrationWorker()`, replace lines 135-139:
```cpp
        if (!supervisor->orderMailbox_.consume(expectedBits, deadlineTicks, transitionTarget)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
```
with:
```cpp
        if (!supervisor->orderMailbox_.consume(expectedBits, deadlineTicks, transitionTarget)) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
```

- [ ] **Step 2: Add signal in startOrchestration**

Edit `src/supervisor/orchestrator.cpp`. In `startOrchestration()`, after the `orderMailbox_.post()` call (and before the closing brace), add:
```cpp
    xTaskNotifyGive(workerTaskHandle_);
```

The signal is harmless on native builds (xTaskNotifyGive is a no-op stub, workerTaskHandle_ is nullptr). On hardware, it wakes the worker to process the new order.

- [ ] **Step 3: Run tests**

Run: `pio test -e native`
Expected: All tests pass.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "fix: replace polling with task notification in orchestrationWorker (#9)"
```

---

## Self-Review

**Spec coverage:**
- #1, #2 → Task 2
- #3 → Task 3
- #4 → Task 4
- #5 → Task 1, Step 1
- #6 → Task 1, Step 2
- #9 → Task 6
- #10 → Task 1, Step 3
- #11 → Already documented in code comment (done in earlier conversation)
- #12 → Task 5
- #13 → Task 1, Step 4
- #15 → Task 1, Step 5
- #16 → Task 1, Step 6
- #7, #8, #14 → Deferred per spec

**Placeholder scan:** No TBD, TODO, or incomplete sections in the plan itself. All code is complete and shown inline. Pre-existing TODO comments in the source files (e.g., `supervisor_v2.h:369`) are intentionally preserved — the plan does not remove or modify them.

**Type consistency:** `deadlineMs` → `deadlineTicks` consistently renamed in Task 3. `fatalDeadlineMs_` → `fatalEnteredTicks_` consistently renamed in Task 5. `consume()` signature change applied to both call sites in Task 2.

---

### Additional Finding: Timeout clamp causes immediate false timeouts

**Problem:** In `orchestrationWorker()` the wait time for `xEventGroupWaitBits` is clamped by forcing `waitTicks = 0` whenever `rawWait` exceeds 60 seconds. That means any transition timeout configured above 60 seconds will *always* be treated as already expired, causing an immediate `TIMED_OUT` response even though the deadline is still in the future. Refs: [src/supervisor/orchestrator.cpp](src/supervisor/orchestrator.cpp#L151-L165).

**Repro (logic):** Configure a transition timeout of 120s in `getTransitionTimeout()`. `deadlineTicks - now` evaluates to ~120s. The clamp assigns `waitTicks = 0`, `xEventGroupWaitBits` returns immediately, and the worker posts `TIMED_OUT`. Refs: [src/supervisor/orchestrator.cpp](src/supervisor/orchestrator.cpp#L151-L165).

**Fix sketch:** Remove the 60s clamp and instead guard only true underflow. The minimal safe version is:

```cpp
TickType_t now = xTaskGetTickCount();
TickType_t rawWait = deadlineTicks - now; // unsigned wrap handles deadline in past
TickType_t waitTicks = rawWait; // no upper clamp
```

If you want a bounded wait to periodically re-check, loop in chunks but carry the remaining time:

```cpp
TickType_t remaining = deadlineTicks - xTaskGetTickCount();
while (remaining > 0) {
    TickType_t chunk = (remaining > pdMS_TO_TICKS(60000))
        ? pdMS_TO_TICKS(60000)
        : remaining;
    EventBits_t bits = xEventGroupWaitBits(..., chunk);
    if ((bits & expectedBits) == expectedBits) { /* completed */ break; }
    remaining = deadlineTicks - xTaskGetTickCount();
}
```

Either approach avoids the false immediate timeout and preserves correctness for long deadlines. Refs: [src/supervisor/orchestrator.cpp](src/supervisor/orchestrator.cpp#L151-L165).
