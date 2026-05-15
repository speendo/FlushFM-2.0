# Step 6: setObservedState Enhancement, determineRecoveryTarget, handleFatal

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enhance `setObservedState()` with transition logging and recovery reset; snapshot `lastTargetBeforeError_` in `setTargetState()`; implement `determineRecoveryTarget()` placeholder and `handleFatal()` with deadline tracking.

**Architecture:** All new implementations go into `state_machine.cpp` (state logic sub-file). A new member `fatalDeadlineElapsed_` makes `handleFatal()` testable on native without hardware calls. Tests live in a dedicated test suite following the established `#define private public` + 3-`.cpp`-include pattern.

**Tech Stack:** C++17, PlatformIO native, Unity test framework, `#define private public`. No new libraries.

**Prerequisite:** Step 5 complete (all methods compiled, 108 passed, 4 pre-existing errors).

---

## File Structure

- **Modify:** `src/supervisor/supervisor_v2.h` — add `bool fatalDeadlineElapsed_` member
- **Modify:** `src/supervisor/state_machine.cpp` — enhance `setObservedState()`, snapshot `lastTargetBeforeError_` in `setTargetState()`, add `determineRecoveryTarget()`, add `handleFatal()`
- **Create:** `test/test_supervisor_v2_step_6/test_main.cpp` — 12 tests
- **Modify:** `platformio.ini` — add/remove `test_ignore` during development

---

### Task 6: Create test file with all 12 tests

**Files:**
- Create: `test/test_supervisor_v2_step_6/test_main.cpp`

- [x] **Step 6.1: Add test_ignore for the new test suite**

In `platformio.ini`, in the `[env:native]` section, change:

```
test_framework = unity
```

To:

```
test_framework = unity
test_ignore = test_supervisor_v2_step_6
```

- [x] **Step 6.2: Create the test file**

```cpp
#include <unity.h>

#define private public
#include "../../src/supervisor/supervisor_v2.cpp"
#include "../../src/supervisor/orchestrator.cpp"
#include "../../src/supervisor/state_machine.cpp"
#undef private

namespace {

// --- setTargetState snapshot tests ---

void test_set_target_to_error_saves_last_target() {
    SupervisorV2 supervisor;
    supervisor.targetState_ = SystemState::LIVE;
    supervisor.lastTargetBeforeError_ = SystemState::BOOTING;

    supervisor.setTargetState(SystemState::ERROR);

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(supervisor.lastTargetBeforeError_));
}

void test_set_target_to_fatal_saves_last_target() {
    SupervisorV2 supervisor;
    supervisor.targetState_ = SystemState::CONNECTING;
    supervisor.lastTargetBeforeError_ = SystemState::BOOTING;

    supervisor.setTargetState(SystemState::FATAL);

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(supervisor.lastTargetBeforeError_));
}

void test_set_target_error_to_error_does_not_restamp() {
    SupervisorV2 supervisor;
    supervisor.targetState_ = SystemState::ERROR;
    supervisor.lastTargetBeforeError_ = SystemState::LIVE;

    supervisor.setTargetState(SystemState::ERROR);

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(supervisor.lastTargetBeforeError_));
}

void test_set_target_non_error_does_not_snapshot() {
    SupervisorV2 supervisor;
    supervisor.lastTargetBeforeError_ = SystemState::READY;

    supervisor.setTargetState(SystemState::CONNECTING);

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY),
                      static_cast<int>(supervisor.lastTargetBeforeError_));
}

// --- setObservedState enhancement tests ---

void test_set_observed_state_logs_and_resets_recovery() {
    SupervisorV2 supervisor;
    supervisor.retryPolicy_.recoveryCounter = 2;
    supervisor.hasActiveOrchestration_ = true;

    supervisor.setObservedState(SystemState::READY);

    TEST_ASSERT_EQUAL(0, supervisor.retryPolicy_.recoveryCounter);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY),
                      static_cast<int>(supervisor.observedState_));
    TEST_ASSERT_FALSE(supervisor.hasActiveOrchestration_);
}

void test_set_observed_state_during_error_does_not_reset_recovery() {
    SupervisorV2 supervisor;
    supervisor.retryPolicy_.recoveryCounter = 2;

    supervisor.setObservedState(SystemState::ERROR);

    TEST_ASSERT_EQUAL(2, supervisor.retryPolicy_.recoveryCounter);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR),
                      static_cast<int>(supervisor.observedState_));
}

void test_set_observed_state_during_fatal_does_not_reset_recovery() {
    SupervisorV2 supervisor;
    supervisor.retryPolicy_.recoveryCounter = 3;

    supervisor.setObservedState(SystemState::FATAL);

    TEST_ASSERT_EQUAL(3, supervisor.retryPolicy_.recoveryCounter);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL),
                      static_cast<int>(supervisor.observedState_));
}

void test_set_observed_state_clears_active_orchestration() {
    SupervisorV2 supervisor;
    supervisor.hasActiveOrchestration_ = true;

    supervisor.setObservedState(SystemState::CONNECTING);

    TEST_ASSERT_FALSE(supervisor.hasActiveOrchestration_);
}

// --- determineRecoveryTarget tests ---

void test_determine_recovery_target_returns_saved_target() {
    SupervisorV2 supervisor;
    supervisor.lastTargetBeforeError_ = SystemState::LIVE;

    SystemState result = supervisor.determineRecoveryTarget();

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE),
                      static_cast<int>(result));
}

void test_determine_recovery_target_after_booting() {
    SupervisorV2 supervisor;
    supervisor.lastTargetBeforeError_ = SystemState::CONNECTING;

    SystemState result = supervisor.determineRecoveryTarget();

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(result));
}

// --- handleFatal tests ---

void test_handle_fatal_sets_deadline_on_first_call() {
    SupervisorV2 supervisor;

    supervisor.handleFatal();

    TEST_ASSERT_NOT_EQUAL(0, supervisor.fatalDeadlineMs_);
    TEST_ASSERT_FALSE(supervisor.fatalDeadlineElapsed_);
}

void test_handle_fatal_no_elapsed_before_deadline() {
    SupervisorV2 supervisor;
    supervisor.handleFatal();  // sets deadline to 60000
    // xTaskGetTickCount() returns 0 on native, so deadline is not reached

    supervisor.fatalDeadlineElapsed_ = false;
    supervisor.handleFatal();

    TEST_ASSERT_FALSE(supervisor.fatalDeadlineElapsed_);
}

void test_handle_fatal_detects_elapsed_deadline() {
    SupervisorV2 supervisor;
    supervisor.fatalEntered_ = true;
    supervisor.fatalDeadlineMs_ = 0;

    supervisor.handleFatal();

    TEST_ASSERT_TRUE(supervisor.fatalDeadlineElapsed_);
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_set_target_to_error_saves_last_target);
    RUN_TEST(test_set_target_to_fatal_saves_last_target);
    RUN_TEST(test_set_target_error_to_error_does_not_restamp);
    RUN_TEST(test_set_target_non_error_does_not_snapshot);
    RUN_TEST(test_set_observed_state_logs_and_resets_recovery);
    RUN_TEST(test_set_observed_state_during_error_does_not_reset_recovery);
    RUN_TEST(test_set_observed_state_during_fatal_does_not_reset_recovery);
    RUN_TEST(test_set_observed_state_clears_active_orchestration);
    RUN_TEST(test_determine_recovery_target_returns_saved_target);
    RUN_TEST(test_determine_recovery_target_after_booting);
    RUN_TEST(test_handle_fatal_sets_deadline_on_first_call);
    RUN_TEST(test_handle_fatal_no_elapsed_before_deadline);
    RUN_TEST(test_handle_fatal_detects_elapsed_deadline);
    return UNITY_END();
}
```

- [x] **Step 6.3: Run new tests to verify they fail (methods not implemented)**

```bash
pio test -e native --filter test_supervisor_v2_step_6
```

Expected: FAIL — `setObservedState` doesn't log/reset, `determineRecoveryTarget` not defined, `handleFatal` not defined.

- [x] **Step 6.4: Run full suite to verify no regressions**

```bash
pio test -e native
```

Expected: 108 succeeded (test_supervisor_v2_step_6 is test_ignored). 4 pre-existing errors unchanged.

- [x] **Step 6.5: Commit**

```bash
git add test/test_supervisor_v2_step_6/test_main.cpp platformio.ini
git commit -m "step 6: add test file for setObservedState, determineRecoveryTarget, handleFatal"
```

---

### Task 6a: Snapshot lastTargetBeforeError_ in setTargetState()

**Files:**
- Modify: `src/supervisor/state_machine.cpp:128-131`
- Modify: `src/supervisor/supervisor_v2.h` — add `fatalDeadlineElapsed_` member (needed by later tasks, added now to avoid churn)

- [x] **Step 6a.1: Add `fatalDeadlineElapsed_` member to `supervisor_v2.h`**

Find the existing `fatalDeadlineMs_` member (line 420) and add the new member after it:

```cpp
	TickType_t fatalDeadlineMs_{};
	bool fatalDeadlineElapsed_{};
	bool fatalEntered_{};
```

So the block becomes:

```cpp
	TickType_t fatalDeadlineMs_{};
	bool fatalDeadlineElapsed_{};

	/** @brief Saved target for ERROR recovery placeholder.
	 *  Auto-snapshotted by setTargetState() when transitioning to ERROR.
	 *  TODO: remove once determineRecoveryTarget() is replaced with real logic.
	 */
	SystemState lastTargetBeforeError_;
```

- [x] **Step 6a.2: Update `setTargetState()` to snapshot `lastTargetBeforeError_`**

In `src/supervisor/state_machine.cpp`, replace the existing `setTargetState()`:

```cpp
void SupervisorV2::setTargetState(SystemState target) {
    PROD_LOG(kLogSource, "Setting target state to %s", stateToString(target));
    targetState_ = target;
}
```

With:

```cpp
void SupervisorV2::setTargetState(SystemState target) {
    // When transitioning TO an error state, save the current pre-error target
    // for recovery. The determineRecoveryTarget() placeholder reads this value.
    if (isErrorState(target) && !isErrorState(targetState_)) {
        lastTargetBeforeError_ = targetState_;
    }

    PROD_LOG(kLogSource, "Setting target state to %s", stateToString(target));
    targetState_ = target;
}
```

- [x] **Step 6a.3: Run snapshot tests**

```bash
pio test -e native --filter test_supervisor_v2_step_6
```

Expected: 4 snapshot tests PASS. 9 other tests FAIL (setObservedState not enhanced, determineRecoveryTarget/handleFatal not implemented).

- [x] **Step 6a.4: Commit**

```bash
git add src/supervisor/supervisor_v2.h src/supervisor/state_machine.cpp
git commit -m "step 6a: snapshot lastTargetBeforeError_ in setTargetState when entering error state"
```

---

### Task 6b: Enhance setObservedState() with logging and recovery reset

**Files:**
- Modify: `src/supervisor/state_machine.cpp:142-145`

- [x] **Step 6b.1: Enhance `setObservedState()`**

In `src/supervisor/state_machine.cpp`, replace the existing `setObservedState()`:

```cpp
/** @brief Commit a new observed state. Minimal version — step 6 adds logging and resetRecoveryIfOutOfError.
 *  @param state The new observed state.
 */
void SupervisorV2::setObservedState(SystemState state) {
    observedState_ = state;
    hasActiveOrchestration_ = false;
}
```

With:

```cpp
/** @brief Commit a new observed state.
 *  Logs the state transition, resets the recovery counter if exiting an error
 *  state, and clears the active orchestration flag.
 *  @param state The new observed state.
 */
void SupervisorV2::setObservedState(SystemState state) {
    PROD_LOG(kLogSource, "%s -> %s",
             stateToString(observedState_), stateToString(state));

    observedState_ = state;
    hasActiveOrchestration_ = false;

    resetRecoveryIfOutOfError();
}
```

- [x] **Step 6b.2: Run setObservedState tests**

```bash
pio test -e native --filter test_supervisor_v2_step_6
```

Expected: 8 tests PASS (4 snapshot + 4 setObservedState). 5 tests FAIL (determineRecoveryTarget, handleFatal not implemented).

- [x] **Step 6b.3: Commit**

```bash
git add src/supervisor/state_machine.cpp
git commit -m "step 6b: enhance setObservedState with transition logging and recovery reset"
```

---

### Task 6c: Implement determineRecoveryTarget()

**Files:**
- Modify: `src/supervisor/state_machine.cpp` — add new method after `setObservedState()`

- [x] **Step 6c.1: Add `determineRecoveryTarget()` to `state_machine.cpp`**

Add after the closing brace of `setObservedState()`:

```cpp
/** @brief Determine the target state to aim for after ERROR recovery.
 *  Placeholder: returns the pre-error target snapshot captured by
 *  setTargetState() when the target entered ERROR or FATAL. This will
 *  be replaced with real logic once recovery policies are defined.
 *  @return The recovery target state.
 */
SystemState SupervisorV2::determineRecoveryTarget() {
    return lastTargetBeforeError_;
}
```

- [x] **Step 6c.2: Run determineRecoveryTarget tests**

```bash
pio test -e native --filter test_supervisor_v2_step_6
```

Expected: 10 tests PASS (4 snapshot + 4 setObservedState + 2 determineRecoveryTarget). 3 tests FAIL (handleFatal not implemented).

- [x] **Step 6c.3: Commit**

```bash
git add src/supervisor/state_machine.cpp
git commit -m "step 6c: implement determineRecoveryTarget placeholder"
```

---

### Task 6d: Implement handleFatal()

**Files:**
- Modify: `src/supervisor/state_machine.cpp` — add new method after `determineRecoveryTarget()`

- [x] **Step 6d.1: Add `handleFatal()` to `state_machine.cpp`**

Add after the closing brace of `determineRecoveryTarget()`:

```cpp
/** @brief Manage the deep sleep shutdown after FATAL.
 *  On first call, records the deadline 60 seconds from now. On subsequent
 *  calls, checks whether the deadline has elapsed. When it has, sets the
 *  fatalDeadlineElapsed_ flag so tests can observe the state. On actual
 *  hardware, this would also trigger esp_deep_sleep_start().
 */
void SupervisorV2::handleFatal() {
    if (!fatalEntered_) {
        fatalEntered_ = true;
        fatalDeadlineMs_ = xTaskGetTickCount() + pdMS_TO_TICKS(60000);
        return;
    }

    if (xTaskGetTickCount() >= fatalDeadlineMs_) {
        fatalDeadlineElapsed_ = true;
#if defined(ARDUINO)
        // esp_deep_sleep_start();
#endif
    }
}
```

- [x] **Step 6d.2: Run all step 6 tests**

```bash
pio test -e native --filter test_supervisor_v2_step_6
```

Expected: 12 tests PASS.

- [x] **Step 6d.3: Commit**

```bash
git add src/supervisor/state_machine.cpp
git commit -m "step 6d: implement handleFatal with deadline tracking and fatalDeadlineElapsed_ flag"
```

---

### Task 6e: Remove test_ignore and run full suite

**Files:**
- Modify: `platformio.ini`

- [x] **Step 6e.1: Remove test_ignore**

In `platformio.ini`, change:

```
test_framework = unity
test_ignore = test_supervisor_v2_step_6
```

To:

```
test_framework = unity
```

- [x] **Step 6e.2: Run full suite**

```bash
pio test -e native
```

Expected: 120 succeeded (108 baseline + 12 new). 4 pre-existing errors unchanged.

- [x] **Step 6e.3: Commit**

```bash
git add platformio.ini
git commit -m "step 6e: enable step 6 tests in full suite"
```
