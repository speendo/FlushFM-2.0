# Step 8: Remaining Path Tests

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add 10 unit tests covering untested code paths discovered during steps 1–7. No production code changes — test-only step.

**Architecture:** All tests go into a new file `test/test_supervisor_v2_remaining_paths/test_main.cpp` following the established `#define private public` + 3-`.cpp`-include pattern. Tests verify existing code works correctly for edge cases and boundary conditions that prior test suites missed.

**Tech Stack:** C++17, PlatformIO native, Unity test framework, `#define private public`.

**Prerequisite:** Step 7 complete (135 passed, 4 pre-existing errors).

---

## File Structure

- **Create:** `test/test_supervisor_v2_remaining_paths/test_main.cpp` — 10 tests
- **Modify:** `platformio.ini` — add/remove `test_ignore` during development

---

## Untested paths addressed

| # | Path | Why untested |
|---|------|-------------|
| 1 | `getNextState` downward stepping | Only FATAL absorbent + ERROR recovery tested; normal rank-down never exercised |
| 2 | `getNextState` upward stepping | Only tested implicitly through `stepTowardTarget()`; no direct unit test |
| 3 | `getNextState` invalid state fallback | `getIndex` tested in isolation, but `getNextState` fallback to FATAL never hit |
| 4 | `completeTransition` required failure detail | Existing test is a no-crash smoke check; doesn't verify errorEvent_ contents |
| 5 | `checkOrchestrationResponse` mixed timeout | Only single-component timeouts tested; loop over multiple timedOutComponents never exercised |
| 6 | `startOrchestration` empty bits | All existing tests register >= 1 component; zero-registered edge case untested |
| 7 | `setMaxRecoveries` rejection | `recoveries >= 1` guard silently drops invalid values; never verified |
| 8 | `getTransitionTimeout` forward/backward | Per-state timeout matrix lookup never directly tested |
| 9 | `getTransitionTimeout` invalid state | Returns 0 for out-of-range states; untested |

---

### Task 8: Create test file with all 10 tests

**Files:**
- Create: `test/test_supervisor_v2_remaining_paths/test_main.cpp`
- Modify: `platformio.ini`

- [ ] **Step 8.1: Add test_ignore**

In `platformio.ini`, in the `[env:native]` section, change:

```
test_framework = unity
```

To:

```
test_framework = unity
test_ignore = test_supervisor_v2_remaining_paths
```

- [ ] **Step 8.2: Create the test file**

```cpp
#include <unity.h>

#define private public
#include "../../src/supervisor/supervisor_v2.cpp"
#include "../../src/supervisor/orchestrator.cpp"
#include "../../src/supervisor/state_machine.cpp"
#undef private

namespace {

struct TestComponent {
    ComponentMailbox mailbox;
};

// ============================================================================
// Test group: getNextState — rank-based transitions
// ============================================================================
// The state rank table is: FATAL=0, ERROR=10, SLEEP=20, BOOTING=30,
// CONNECTING=40, READY=50, LIVE=60.
// Prior tests cover FATAL absorbent (always stays FATAL) and ERROR
// recovery jump (ERROR -> BOOTING). These tests cover the general
// downward and upward stepping paths and the invalid-state fallback.

void test_get_next_state_downward_rank_stepping() {
    // LIVE (rank 60, route index 6) -> SLEEP (rank 20, route index 2).
    // currentIndex=6 > targetIndex=2, so step down: stateRoute[5-1]=READY.
    // Wait — stateRoute[6-1]=stateRoute[5]=SystemState::READY.
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY),
                      static_cast<int>(getNextState(SystemState::LIVE, SystemState::SLEEP)));

    // READY (index 5) -> CONNECTING (index 4). Single step down.
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(getNextState(SystemState::READY, SystemState::CONNECTING)));
}

void test_get_next_state_upward_rank_stepping() {
    // SLEEP (rank 20, route index 2) -> LIVE (rank 60, route index 6).
    // currentIndex=2 < targetIndex=6, so step up: stateRoute[2+1]=BOOTING.
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::BOOTING),
                      static_cast<int>(getNextState(SystemState::SLEEP, SystemState::LIVE)));

    // BOOTING (index 3) -> LIVE (index 6). One step: CONNECTING.
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(getNextState(SystemState::BOOTING, SystemState::LIVE)));
}

void test_get_next_state_invalid_falls_back_to_fatal() {
    // getIndex returns -1 for out-of-range uint8_t values, causing
    // getNextState to fall through to the ERROR_LOG + return FATAL.
    SystemState badState = static_cast<SystemState>(99);

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL),
                      static_cast<int>(getNextState(SystemState::BOOTING, badState)));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL),
                      static_cast<int>(getNextState(badState, SystemState::LIVE)));
}

// ============================================================================
// Test group: completeTransition — required component failure detail
// ============================================================================
// The existing test_complete_transition_failed_required_posts_error is a
// no-crash smoke test. This test verifies the error event payload.

void test_complete_transition_required_failed_writes_error_event() {
    SupervisorV2 supervisor;
    TestComponent wifi;
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);

    supervisor.completeTransition(ComponentID::WiFi, TransitionStatus::Failed);

    TEST_ASSERT_TRUE(supervisor.errorEvent_.pending);
    TEST_ASSERT_EQUAL_STRING("component failed", supervisor.errorEvent_.reason);
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentID::WiFi),
                      static_cast<int>(supervisor.errorEvent_.source));
}

// ============================================================================
// Test group: checkOrchestrationResponse — mixed timeout
// ============================================================================
// Prior tests only check single-component timeouts. This test verifies the
// loop over timedOutComponents correctly distinguishes required from optional.

void test_check_response_mixed_timeout() {
    SupervisorV2 supervisor;
    TestComponent wifi, audio, cli;
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);
    supervisor.setMaxRecoveries(3);

    supervisor.observedState_ = SystemState::BOOTING;
    EventBits_t timedOut = (1 << static_cast<int>(ComponentID::WiFi))
                         | (1 << static_cast<int>(ComponentID::AudioRuntime))
                         | (1 << static_cast<int>(ComponentID::CLI));
    supervisor.responseMailbox_.post(OrchestrationResult::TIMED_OUT, timedOut);

    supervisor.checkOrchestrationResponse();

    TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::FAILED),
                      static_cast<int>(supervisor.componentStatuses_[static_cast<int>(ComponentID::WiFi)]));
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::FAILED),
                      static_cast<int>(supervisor.componentStatuses_[static_cast<int>(ComponentID::AudioRuntime)]));
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::DEGRADED),
                      static_cast<int>(supervisor.componentStatuses_[static_cast<int>(ComponentID::CLI)]));
    TEST_ASSERT_FALSE(supervisor.hasActiveOrchestration_);
}

// ============================================================================
// Test group: startOrchestration — zero registered components
// ============================================================================
// When no component mailboxes have been registered, expectedBits is 0.
// The orchestration should still start (no components to wait for).

void test_start_orchestration_empty_bits_mask() {
    SupervisorV2 supervisor;
    supervisor.setup();

    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.startOrchestration(SystemState::CONNECTING);

    TEST_ASSERT_TRUE(supervisor.hasActiveOrchestration_);
    TEST_ASSERT_EQUAL(0, supervisor.orderMailbox_.expectedBits);
    TEST_ASSERT_EQUAL(static_cast<int>(SubState::PENDING),
                      static_cast<int>(supervisor.nextState_.subState));
}

// ============================================================================
// Test group: setMaxRecoveries — rejection of invalid values
// ============================================================================

void test_set_max_recoveries_rejects_invalid_values() {
    SupervisorV2 supervisor;
    int original = supervisor.retryPolicy_.maxRecoveries;

    supervisor.setMaxRecoveries(0);
    TEST_ASSERT_EQUAL(original, supervisor.retryPolicy_.maxRecoveries);

    supervisor.setMaxRecoveries(-1);
    TEST_ASSERT_EQUAL(original, supervisor.retryPolicy_.maxRecoveries);
}

void test_set_max_recoveries_accepts_valid_value() {
    SupervisorV2 supervisor;

    supervisor.setMaxRecoveries(1);
    TEST_ASSERT_EQUAL(1, supervisor.retryPolicy_.maxRecoveries);

    supervisor.setMaxRecoveries(5);
    TEST_ASSERT_EQUAL(5, supervisor.retryPolicy_.maxRecoveries);
}

// ============================================================================
// Test group: getTransitionTimeout — per-state timeout lookup
// ============================================================================

void test_get_transition_timeout_forward_and_backward() {
    SupervisorV2 supervisor;

    uint32_t forward = supervisor.getTransitionTimeout(SystemState::BOOTING, true);
    uint32_t backward = supervisor.getTransitionTimeout(SystemState::BOOTING, false);

    TEST_ASSERT_EQUAL(5000, forward);
    TEST_ASSERT_EQUAL(5000, backward);
}

void test_get_transition_timeout_invalid_state_returns_zero() {
    SupervisorV2 supervisor;
    SystemState badState = static_cast<SystemState>(99);

    uint32_t result = supervisor.getTransitionTimeout(badState, true);

    TEST_ASSERT_EQUAL(0, result);
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_get_next_state_downward_rank_stepping);
    RUN_TEST(test_get_next_state_upward_rank_stepping);
    RUN_TEST(test_get_next_state_invalid_falls_back_to_fatal);
    RUN_TEST(test_complete_transition_required_failed_writes_error_event);
    RUN_TEST(test_check_response_mixed_timeout);
    RUN_TEST(test_start_orchestration_empty_bits_mask);
    RUN_TEST(test_set_max_recoveries_rejects_invalid_values);
    RUN_TEST(test_set_max_recoveries_accepts_valid_value);
    RUN_TEST(test_get_transition_timeout_forward_and_backward);
    RUN_TEST(test_get_transition_timeout_invalid_state_returns_zero);
    return UNITY_END();
}
```

- [ ] **Step 8.3: Run new tests to verify they pass (all should pass immediately since no new production code is needed)**

Remove test_ignore temporarily:

```
test_framework = unity
```

Run:

```bash
pio test -e native --filter test_supervisor_v2_remaining_paths
```

Expected: All 10 tests PASS. These tests exercise existing production code that was already implemented in steps 1–7 but never directly tested.

Restore test_ignore:

```
test_framework = unity
test_ignore = test_supervisor_v2_remaining_paths
```

- [ ] **Step 8.4: Commit**

```bash
git add test/test_supervisor_v2_remaining_paths/test_main.cpp platformio.ini
git commit -m "step 8: add tests covering untested state machine paths"
```

---

### Task 8e: Remove test_ignore and run full suite

**Files:**
- Modify: `platformio.ini`

- [ ] **Step 8e.1: Remove test_ignore**

Change:

```
test_framework = unity
test_ignore = test_supervisor_v2_remaining_paths
```

To:

```
test_framework = unity
```

- [ ] **Step 8e.2: Run full suite**

```bash
pio test -e native
```

Expected: 145 succeeded (135 baseline + 10 new). 4 pre-existing errors unchanged.

- [ ] **Step 8e.3: Commit**

```bash
git add platformio.ini
git commit -m "step 8e: enable remaining-paths tests in full suite"
```
