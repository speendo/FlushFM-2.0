# Step 7: run() — Full Tick Sequence

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the `run()` main tick function — the event-driven state machine loop that blocks on `ulTaskNotifyTake`, drains mailboxes, steps toward the target state, checks orchestration responses, and handles FATAL housekeeping. Add a `stepTowardTarget()` helper.

**Architecture:** Both `run()` and `stepTowardTarget()` go into `state_machine.cpp` (state logic sub-file). `run()` is the top-level entry point called by the FreeRTOS state machine task loop. On native, `ulTaskNotifyTake` is a no-op (returns 0 immediately), so tests call `run()` synchronously and verify side effects. Tests follow the established `#define private public` + 3-`.cpp`-include pattern.

**Tech Stack:** C++17, PlatformIO native, Unity test framework, `#define private public`. A `portMAX_DELAY` stub must be added to `native_stubs.h`.

**Prerequisite:** Step 6 complete (all methods compiled, 121 passed, 4 pre-existing errors).

---

## File Structure

- **Modify:** `src/supervisor/native_stubs.h` — add `constexpr TickType_t portMAX_DELAY`
- **Modify:** `src/supervisor/supervisor_v2.h` — add `stepTowardTarget()` declaration
- **Modify:** `src/supervisor/state_machine.cpp` — add `stepTowardTarget()`, add `run()`
- **Create:** `test/test_supervisor_v2_run/test_main.cpp` — 14 tests
- **Modify:** `platformio.ini` — add/remove `test_ignore` during development

---

### Task 7: Create test file with all 14 tests

**Files:**
- Create: `test/test_supervisor_v2_run/test_main.cpp`
- Modify: `platformio.ini`

- [ ] **Step 7.1: Add test_ignore**

In `platformio.ini`, in the `[env:native]` section, change:

```
test_framework = unity
```

To:

```
test_framework = unity
test_ignore = test_supervisor_v2_run
```

- [ ] **Step 7.2: Create the test file**

```cpp
#include <unity.h>

#define private public
#include "../../src/supervisor/supervisor_v2.cpp"
#include "../../src/supervisor/orchestrator.cpp"
#include "../../src/supervisor/state_machine.cpp"
#undef private

namespace {

/**
 * @brief Helper struct providing a ComponentMailbox for test fixtures.
 * Following the established pattern in other test suites (registration,
 * orchestration). The supervisor stores a pointer to this mailbox during
 * registerComponent(). In production, each component owns its own mailbox;
 * in tests, the stack-allocated struct suffices.
 */
struct TestComponent {
    ComponentMailbox mailbox;
};

// ============================================================================
// Test group: idle state (no transition needed)
// ============================================================================
// Covers Phase 3 branch (none): if observedState_ already equals targetState_,
// the stepping condition fails and run() falls through without calling
// startOrchestration or any other stepping logic. hasActiveOrchestration_
// must remain false and the observed state unchanged.

void test_run_already_at_target_does_nothing() {
    SupervisorV2 supervisor;

    // Both observed and target are BOOTING — no transition possible.
    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.targetState_ = SystemState::BOOTING;
    supervisor.hasActiveOrchestration_ = false;

    supervisor.run();

    TEST_ASSERT_FALSE(supervisor.hasActiveOrchestration_);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::BOOTING),
                      static_cast<int>(supervisor.observedState_));
}

// ============================================================================
// Test group: stepping toward target
// ============================================================================
// Covers Phase 3 branch (A): targetState_ differs from observedState_ and
// hasActiveOrchestration_ is false. stepTowardTarget() is called, which
// internally calls getNextState() then startOrchestration(). Verifies that
// the orchestration was started: the active flag is set, nextState_ records
// the intermediate stepping state, and orderMailbox_ is written.

void test_run_steps_toward_target() {
    // Register at least one component so startOrchestration has mailboxes
    // to write and can compute the expected-bits mask.
    SupervisorV2 supervisor;
    TestComponent wifiComponent;
    supervisor.registerComponent(ComponentID::WiFi, &wifiComponent.mailbox, true);

    // Observed is BOOTING, target is CONNECTING (one rank up).
    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.targetState_ = SystemState::CONNECTING;
    supervisor.hasActiveOrchestration_ = false;

    supervisor.run();

    // The orchestration must have been started: the active flag indicates
    // the worker is waiting on the event group, nextState_ holds the
    // intermediate stepping state (CONNECTING), and orderMailbox_ contains
    // the expected-bits mask and deadline for the worker to read.
    TEST_ASSERT_TRUE(supervisor.hasActiveOrchestration_);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(supervisor.nextState_.transitionTarget));
    TEST_ASSERT_TRUE(supervisor.orderMailbox_.pending);
}

void test_run_step_noop_when_already_at_target() {
    // Verify that when targetState_ == observedState_ (both LIVE),
    // no orchestration is started even after event processing.
    // Event processing may change targetState_, so this test isolates
    // the step-toward-path by confirming the condition check.
    SupervisorV2 supervisor;
    // No components registered — would not matter here since the
    // stepping path is never entered.

    supervisor.observedState_ = SystemState::LIVE;
    supervisor.targetState_ = SystemState::LIVE;
    supervisor.hasActiveOrchestration_ = false;

    supervisor.run();

    TEST_ASSERT_FALSE(supervisor.hasActiveOrchestration_);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(supervisor.observedState_));
}

// ============================================================================
// Test group: active orchestration response checking
// ============================================================================
// Covers Phase 3 branch (B): hasActiveOrchestration_ is true, so run()
// calls checkOrchestrationResponse() instead of stepTowardTarget().
// The response mailbox is pre-loaded with a result before calling run().

void test_run_checks_orchestration_response_completed() {
    // Simulate a COMPLETED orchestration: the worker posted a response
    // indicating all expected bits were set before the deadline.
    // Expected: observedState_ advances to the orchestration target,
    // active orchestration flag is cleared.
    SupervisorV2 supervisor;
    TestComponent wifiComponent;
    supervisor.registerComponent(ComponentID::WiFi, &wifiComponent.mailbox, true);

    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.hasActiveOrchestration_ = true;
    supervisor.nextState_.transitionTarget = SystemState::CONNECTING;
    supervisor.responseMailbox_.post(OrchestrationResult::COMPLETED, 0);

    supervisor.run();

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(supervisor.observedState_));
    TEST_ASSERT_FALSE(supervisor.hasActiveOrchestration_);
}

void test_run_checks_orchestration_response_timed_out() {
    // Simulate a TIMED_OUT orchestration: the worker detected that some
    // expected bits were never set before the deadline. For required
    // components, this should mark the component as FAILED and post an
    // error event so the next run() tick initiates ERROR recovery.
    SupervisorV2 supervisor;
    TestComponent wifiComponent;
    supervisor.registerComponent(ComponentID::WiFi, &wifiComponent.mailbox, true);
    supervisor.setMaxRecoveries(3);

    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.hasActiveOrchestration_ = true;
    supervisor.responseMailbox_.post(OrchestrationResult::TIMED_OUT,
        1 << static_cast<int>(ComponentID::WiFi));

    supervisor.run();

    TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::FAILED),
                      static_cast<int>(supervisor.componentStatuses_[static_cast<int>(ComponentID::WiFi)]));
    TEST_ASSERT_FALSE(supervisor.hasActiveOrchestration_);
}

void test_run_active_orchestration_blocks_stepping() {
    // An in-flight orchestration prevents new stepping even when
    // targetState_ differs from observedState_. Verify that the step
    // is not taken and the orchestration remains in progress.
    SupervisorV2 supervisor;
    TestComponent wifiComponent;
    supervisor.registerComponent(ComponentID::WiFi, &wifiComponent.mailbox, true);

    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.targetState_ = SystemState::LIVE;  // would step if idle
    supervisor.hasActiveOrchestration_ = true;

    supervisor.run();

    // No new orchestration was started (stepTowardTarget was skipped),
    // and the existing orchestration is still active because no response
    // was posted to the mailbox.
    TEST_ASSERT_TRUE(supervisor.hasActiveOrchestration_);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::BOOTING),
                      static_cast<int>(supervisor.observedState_));
}

// ============================================================================
// Test group: event (mailbox) processing
// ============================================================================
// Covers Phase 2: error event and state request mailboxes are drained
// before state stepping. Order: errors first, then state requests.

void test_run_consumes_state_request() {
    // A state request should update targetState_ via setTargetState().
    // No components are registered — state stepping is not tested here.
    SupervisorV2 supervisor;

    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.targetState_ = SystemState::BOOTING;
    supervisor.stateRequestMailbox_.pending = true;
    supervisor.stateRequestMailbox_.requestedTarget = SystemState::LIVE;

    supervisor.run();

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(supervisor.targetState_));
    TEST_ASSERT_FALSE(supervisor.stateRequestMailbox_.pending);
}

void test_run_consumes_error_event() {
    // An error event should increment the recovery counter and set the
    // target to ERROR (recoverable). The recovery budget (maxRecoveries=3)
    // is not exhausted after a single error, so FATAL is not reached.
    SupervisorV2 supervisor;

    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.targetState_ = SystemState::LIVE;
    supervisor.retryPolicy_.recoveryCounter = 0;
    supervisor.setMaxRecoveries(3);
    supervisor.errorEvent_.pending = true;
    supervisor.errorEvent_.reason = "test error";
    supervisor.errorEvent_.source = ComponentID::WiFi;

    supervisor.run();

    TEST_ASSERT_EQUAL(1, supervisor.retryPolicy_.recoveryCounter);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR),
                      static_cast<int>(supervisor.targetState_));
}

void test_run_consumes_both_events_and_steps() {
    // Full integration: a state request arrives while idle. run() consumes
    // the mailbox (updates targetState_), then sees target != observed and
    // steps toward the new target. Verifies the complete pipeline: event
    // drain → stepping → orchestration start.
    SupervisorV2 supervisor;
    TestComponent wifiComponent;
    supervisor.registerComponent(ComponentID::WiFi, &wifiComponent.mailbox, true);

    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.targetState_ = SystemState::BOOTING;
    supervisor.stateRequestMailbox_.pending = true;
    supervisor.stateRequestMailbox_.requestedTarget = SystemState::CONNECTING;

    supervisor.run();

    // The state request was consumed, target updated, and then stepping
    // detected the delta and started an orchestration toward CONNECTING.
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(supervisor.targetState_));
    TEST_ASSERT_FALSE(supervisor.stateRequestMailbox_.pending);
    TEST_ASSERT_TRUE(supervisor.hasActiveOrchestration_);
}

// ============================================================================
// Test group: FATAL behavior
// ============================================================================
// Covers Phases 2-4: in FATAL, event processing and state stepping are
// skipped entirely. Only handleFatal() runs (arms the dwell timer).

void test_run_skips_event_processing_in_fatal() {
    // In FATAL, pending events must remain untouched. The supervisor
    // does not accept new state requests or error events once FATAL.
    SupervisorV2 supervisor;

    supervisor.observedState_ = SystemState::FATAL;
    supervisor.targetState_ = SystemState::BOOTING;
    supervisor.stateRequestMailbox_.pending = true;
    supervisor.stateRequestMailbox_.requestedTarget = SystemState::LIVE;
    supervisor.errorEvent_.pending = true;

    supervisor.run();

    // Both mailboxes must still be pending — they were never consumed.
    TEST_ASSERT_TRUE(supervisor.stateRequestMailbox_.pending);
    TEST_ASSERT_TRUE(supervisor.errorEvent_.pending);
}

void test_run_skips_state_stepping_in_fatal() {
    // In FATAL, stepping toward any target is prohibited. Even if
    // targetState_ differs from observedState_, stepTowardTarget()
    // must NOT be called. The system stays in FATAL permanently.
    SupervisorV2 supervisor;
    TestComponent wifiComponent;
    supervisor.registerComponent(ComponentID::WiFi, &wifiComponent.mailbox, true);

    supervisor.observedState_ = SystemState::FATAL;
    supervisor.targetState_ = SystemState::LIVE;
    supervisor.hasActiveOrchestration_ = false;

    supervisor.run();

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL),
                      static_cast<int>(supervisor.observedState_));
    TEST_ASSERT_FALSE(supervisor.hasActiveOrchestration_);
}

void test_run_calls_handle_fatal() {
    // When observedState_ == FATAL, handleFatal() must be called.
    // Verify the side effect: fatalEntered_ is set to true and the
    // dwell timer deadline is recorded.
    SupervisorV2 supervisor;

    supervisor.observedState_ = SystemState::FATAL;
    supervisor.fatalEntered_ = false;

    supervisor.run();

    TEST_ASSERT_NOT_EQUAL(0, supervisor.fatalDeadlineMs_);
    TEST_ASSERT_TRUE(supervisor.fatalEntered_);
}

// ============================================================================
// Test group: error recovery
// ============================================================================
// Covers Phase 3 branch (C): observedState_ == ERROR with no active
// orchestration. determineRecoveryTarget() is called and if the result
// differs from ERROR, a state request is posted.

void test_run_error_recovery_posts_state_request() {
    // From ERROR with lastTargetBeforeError_ = LIVE, recovery should post
    // a state request for LIVE. This goes through the mailbox so it will
    // be consumed on the next run() tick.
    SupervisorV2 supervisor;

    supervisor.observedState_ = SystemState::ERROR;
    supervisor.hasActiveOrchestration_ = false;
    supervisor.lastTargetBeforeError_ = SystemState::LIVE;

    supervisor.run();

    TEST_ASSERT_TRUE(supervisor.stateRequestMailbox_.pending);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(supervisor.stateRequestMailbox_.requestedTarget));
}

void test_run_error_recovery_noop_when_target_matches() {
    // If the saved recovery target equals the current error state (should
    // not happen in practice due to the restamp guard, but defensive),
    // no state request should be posted.
    SupervisorV2 supervisor;

    supervisor.observedState_ = SystemState::ERROR;
    supervisor.hasActiveOrchestration_ = false;
    supervisor.lastTargetBeforeError_ = SystemState::ERROR;

    supervisor.run();

    TEST_ASSERT_FALSE(supervisor.stateRequestMailbox_.pending);
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_run_already_at_target_does_nothing);
    RUN_TEST(test_run_steps_toward_target);
    RUN_TEST(test_run_step_noop_when_already_at_target);
    RUN_TEST(test_run_checks_orchestration_response_completed);
    RUN_TEST(test_run_checks_orchestration_response_timed_out);
    RUN_TEST(test_run_active_orchestration_blocks_stepping);
    RUN_TEST(test_run_consumes_state_request);
    RUN_TEST(test_run_consumes_error_event);
    RUN_TEST(test_run_consumes_both_events_and_steps);
    RUN_TEST(test_run_skips_event_processing_in_fatal);
    RUN_TEST(test_run_skips_state_stepping_in_fatal);
    RUN_TEST(test_run_calls_handle_fatal);
    RUN_TEST(test_run_error_recovery_posts_state_request);
    RUN_TEST(test_run_error_recovery_noop_when_target_matches);
    return UNITY_END();
}
```

- [ ] **Step 7.3: Run new tests — expect compilation failure**

```bash
pio test -e native --filter test_supervisor_v2_run
```

Expected: ERRORED — `stepTowardTarget` not defined, `run` not defined.

- [ ] **Step 7.4: Run full suite to verify no regressions**

```bash
pio test -e native
```

Expected: 121 succeeded (test_supervisor_v2_run is test_ignored). 4 pre-existing errors unchanged.

- [ ] **Step 7.5: Commit**

```bash
git add test/test_supervisor_v2_run/test_main.cpp platformio.ini
git commit -m "step 7: add test file for run() tick sequence"
```

---

### Task 7a: Add portMAX_DELAY stub + stepTowardTarget declaration

**Files:**
- Modify: `src/supervisor/native_stubs.h` — add `portMAX_DELAY`
- Modify: `src/supervisor/supervisor_v2.h` — add `stepTowardTarget()` declaration

- [ ] **Step 7a.1: Add `portMAX_DELAY` to `native_stubs.h`**

Add after line 11 (`inline constexpr int pdTRUE = 1;`):

```cpp
inline constexpr int pdTRUE = 1;
// portMAX_DELAY is a FreeRTOS constant (0xFFFFFFFF) that tells blocking APIs
// to wait indefinitely. ulTaskNotifyTake(pdTRUE, portMAX_DELAY) means "block
// forever until notified." The stub value must be non-zero for any arithmetic
// involving the timeout to work correctly (e.g., pdMS_TO_TICKS addition).
inline constexpr TickType_t portMAX_DELAY = 0xffffffffUL;
```

- [ ] **Step 7a.2: Add `stepTowardTarget()` declaration to `supervisor_v2.h`**

Add after the `checkOrchestrationResponse()` declaration (after line 364):

```cpp
	/** @brief Compute the next stepping state and begin orchestration.
	 *
	 *  Called by run() when targetState_ differs from observedState_ and
	 *  no orchestration is in flight. Delegates to getNextState() which
	 *  uses the state rank table (multiples of 10: FATAL=0, ERROR=10,
	 *  SLEEP=20, ... LIVE=60) to determine the single-step intermediate.
	 *
	 *  If the intermediate equals the current observed state (meaning we
	 *  are already at the target), this is a no-op.
	 *
	 *  Otherwise, calls startOrchestration() which clears the event group,
	 *  writes all component mailboxes, computes the timeout deadline, and
	 *  posts an OrchestrationOrder to the worker task on Core 0.
	 */
	void stepTowardTarget();
```

- [ ] **Step 7a.3: Commit**

```bash
git add src/supervisor/native_stubs.h src/supervisor/supervisor_v2.h
git commit -m "step 7a: add portMAX_DELAY stub and stepTowardTarget declaration"
```

---

### Task 7b: Implement stepTowardTarget()

**Files:**
- Modify: `src/supervisor/state_machine.cpp` — add method before `run()`

- [ ] **Step 7b.1: Add `stepTowardTarget()` to `state_machine.cpp`**

Add after the closing brace of `handleFatal()`:

```cpp
/** @brief Compute the next stepping state and begin an orchestration.
 *  Delegates to getNextState() which uses the state rank table to determine
 *  the next intermediate state along the path from observedState_ toward
 *  targetState_. If already at the target (getNextState returns the same
 *  value as observedState_), this is a no-op — no orchestration is started,
 *  no component mailboxes are written, and the event group is left alone.
 *
 *  Called by run() when targetState_ != observedState_ and no orchestration
 *  is currently in flight (hasActiveOrchestration_ == false).
 */
void SupervisorV2::stepTowardTarget() {
    SystemState nextSteppingState = getNextState(observedState_, targetState_);
    if (nextSteppingState == observedState_) return;
    startOrchestration(nextSteppingState);
}
```

- [ ] **Step 7b.2: Run step-predicate tests (tests 1, 2, 3)**

Remove test_ignore temporarily by editing `platformio.ini`:

```
test_framework = unity
```

Then run:

```bash
pio test -e native --filter test_supervisor_v2_run
```

Expected: ERRORED — `run()` not defined. Compilation fails because test_supervisor_v2_run calls `run()` which doesn't exist yet. The stepTowardTarget code itself compiles without errors.

Restore test_ignore:

```
test_framework = unity
test_ignore = test_supervisor_v2_run
```

- [ ] **Step 7b.3: Commit**

```bash
git add src/supervisor/state_machine.cpp
git commit -m "step 7b: implement stepTowardTarget helper"
```

---

### Task 7c: Implement run()

**Files:**
- Modify: `src/supervisor/state_machine.cpp` — add `run()` after `stepTowardTarget()`

- [ ] **Step 7c.1: Add `run()` to `state_machine.cpp`**

Add after the closing brace of `stepTowardTarget()`:

```cpp
/** @brief Execute one tick of the supervisor state machine loop.
 *
 *  This is the top-level entry point called by the FreeRTOS state machine
 *  task. It implements a three-phase processing pipeline:
 *
 *  Phase 1 — Wait for wake signal:
 *    Calls ulTaskNotifyTake(pdTRUE, portMAX_DELAY) which blocks the task
 *    until another task or ISR calls xTaskNotifyGive() on its handle.
 *    Three wake sources exist:
 *      - postStateRequest()  — external component requests a new target state
 *      - postErrorEvent()    — component reports a failure
 *      - orchestrationWorker — reports COMPLETED or TIMED_OUT via responseMailbox_
 *    On native builds ulTaskNotifyTake is a no-op stub that returns 0,
 *    so run() executes synchronously in tests.
 *
 *  Phase 2 — Drain pending events (skipped in FATAL):
 *    1. consumeErrorEvent()    — reads the single-slot error event mailbox.
 *       If an error is pending, increments recoveryCounter and sets target
 *       to ERROR or FATAL depending on exhaustion.
 *    2. consumeStateRequest()  — reads the single-slot state request mailbox.
 *       If a request is pending, calls setTargetState() which updates the
 *       target and, if entering an error state, snapshots lastTargetBeforeError_.
 *    Both use embedded spinlocks so they are safe to call across cores.
 *    Order matters: errors are drained FIRST so they can override any stale
 *    state request that arrived before the error.
 *
 *  Phase 3 — State stepping (skipped in FATAL):
 *    Three mutually exclusive branches based on current conditions:
 *    A. Need to move: targetState_ differs from observedState_ and no
 *       orchestration is in flight → stepTowardTarget() computes the
 *       intermediate stepping state and starts a new orchestration.
 *    B. Waiting for completion an in-flight orchestration is active →
 *       checkOrchestrationResponse() reads the response mailbox. If the
 *       worker has posted a result (COMPLETED or TIMED_OUT), it is handled.
 *       If no response is ready (mailbox empty), run() simply returns.
 *    C. Error recovery: the system is in ERROR with no active orchestration →
 *       determineRecoveryTarget() returns the pre-error target (or a
 *       placeholder), and postStateRequest() initiates recovery.
 *
 *  Phase 4 — FATAL housekeeping:
 *    If observedState_ == FATAL, calls handleFatal() which arms a 60-second
 *    dwell timer on first entry and triggers deep sleep once the timer
 *    expires. FATAL is absorbent: no state transitions can exit it, so
 *    event processing and state stepping are unconditionally skipped.
 */
void SupervisorV2::run() {
    // --- Phase 1: Block until woken by a notification ---
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // --- Phase 2: Drain pending events (mailbox consumption) ---
    // In FATAL, both external state requests and error events are ignored.
    if (observedState_ != SystemState::FATAL) {
        consumeErrorEvent();
        consumeStateRequest();
    }

    // --- Phase 3: State stepping toward the target ---
    if (observedState_ != SystemState::FATAL) {
        if (targetState_ != observedState_ && !hasActiveOrchestration_) {
            // (A) Not at target yet and no orchestration running — start one.
            stepTowardTarget();
        } else if (hasActiveOrchestration_) {
            // (B) An orchestration is in flight — check if the worker completed.
            checkOrchestrationResponse();
        } else if (observedState_ == SystemState::ERROR) {
            // (C) Idle in ERROR — attempt recovery toward the pre-error target.
            SystemState recoveryTarget = determineRecoveryTarget();
            if (recoveryTarget != observedState_) {
                postStateRequest(recoveryTarget);
            }
        }
    }

    // --- Phase 4: FATAL housekeeping ---
    if (observedState_ == SystemState::FATAL) {
        handleFatal();
    }
}
```

- [ ] **Step 7c.2: Run the full step 7 suite**

Remove test_ignore temporarily:

```
test_framework = unity
```

Run:

```bash
pio test -e native --filter test_supervisor_v2_run
```

Expected: 14 tests PASS (all 14 tests listed in Task 7.2).

Restore test_ignore:

```
test_framework = unity
test_ignore = test_supervisor_v2_run
```

- [ ] **Step 7c.3: Commit**

```bash
git add src/supervisor/state_machine.cpp
git commit -m "step 7c: implement run() with full tick sequence"
```

---

### Task 7d: Remove test_ignore and run full suite

**Files:**
- Modify: `platformio.ini`

- [ ] **Step 7d.1: Remove test_ignore**

Change:

```
test_framework = unity
test_ignore = test_supervisor_v2_run
```

To:

```
test_framework = unity
```

- [ ] **Step 7d.2: Run full suite**

```bash
pio test -e native
```

Expected: 135 succeeded (121 baseline + 14 new). 4 pre-existing errors unchanged.

- [ ] **Step 7d.3: Commit**

```bash
git add platformio.ini
git commit -m "step 7d: enable run() tests in full suite"
```
