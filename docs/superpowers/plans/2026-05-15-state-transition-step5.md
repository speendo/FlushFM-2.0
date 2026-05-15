# Step 5: Orchestration Method Implementations (startOrchestration, checkOrchestrationResponse, orchestrationWorker)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the orchestrator sub-file `supervisor_v2_orch.cpp` with `startOrchestration`, `checkOrchestrationResponse`, and `orchestrationWorker`. Wire the worker task in `setup()`. Add a minimal `setObservedState()` in `supervisor_v2.cpp` so the completion path compiles. Update `completeTransition` optional failure to set event group bit.

**Architecture:** All orchestrator methods live in `supervisor_v2_orch.cpp` (sub-file of SupervisorV2 class). The state machine `setup()` in `supervisor_v2.cpp` creates the worker task. Tests include both `.cpp` files.

**Tech Stack:** C++17, PlatformIO native, Unity test framework, `#define private public`. Worker hardware-only.

**Prerequisite:** Step 4.1 (structs, members, method declarations, friend, native stubs already in place).

---

### File Structure

- **Modify:** `src/state_machine/supervisor_v2.cpp` — add minimal `setObservedState()`, update `completeTransition` optional failure, update `setup()` to create worker task
- **Create:** `src/state_machine/supervisor_v2_orch.cpp` — `startOrchestration()`, `checkOrchestrationResponse()`, `orchestrationWorker()`
- **Create:** `test/test_supervisor_v2_orchestration/test_main.cpp` — 11 tests
- **Modify:** `platformio.ini` — test_ignore during development, remove at end

---

### Task 5a: Minimal setObservedState + completeTransition optional failure fix

- [ ] **Step 5a.1: Add minimal `setObservedState()` to `supervisor_v2.cpp`**

Add after `resetRecoveryIfOutOfError()` (after line 236):

```cpp
void SupervisorV2::setObservedState(SystemState state) {
    observedState_ = state;
    hasActiveOrchestration_ = false;
}
```

(Full version with logging and `resetRecoveryIfOutOfError` comes in step 6.)

- [ ] **Step 5a.2: Update `completeTransition()` optional failure path**

In `supervisor_v2.cpp`, change:
```cpp
    } else {
        componentStatuses_[static_cast<int>(id)] = ComponentStatus::DEGRADED;
    }
```

To:
```cpp
    } else {
        componentStatuses_[static_cast<int>(id)] = ComponentStatus::DEGRADED;
        xEventGroupSetBits(eventGroup_, 1 << static_cast<int>(id));
    }
```

- [ ] **Step 5a.3: Run full suite**

```bash
pio test -e native
```

Expected: 95 succeeded. 4 pre-existing errors unchanged.

- [ ] **Step 5a.4: Commit**

```bash
git add src/state_machine/supervisor_v2.cpp
git commit -m "step 5a: add minimal setObservedState, update completeTransition optional failure to set event bit"
```

---

### Task 5b: Implement startOrchestration + 7 tests

- [ ] **Step 5b.1: Add test_ignore to platformio.ini**

```ini
test_framework = unity
test_ignore = test_supervisor_v2_orchestration
```

- [ ] **Step 5b.2: Create test file with startOrchestration + completeTransition tests**

**File: `test/test_supervisor_v2_orchestration/test_main.cpp`** — includes both .cpp files:

```cpp
#include <unity.h>

#define private public
#include "../../src/state_machine/supervisor_v2.cpp"
#include "../../src/state_machine/supervisor_v2_orch.cpp"
#undef private

namespace {

struct TestComponent {
    ComponentMailbox mailbox;
};

void test_start_orchestration_sets_active_flag() {
    SupervisorV2 supervisor;
    TestComponent board, wifi, audio, cli;
    supervisor.registerComponent(ComponentID::BoardInfo, &board.mailbox, true);
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);

    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.startOrchestration(SystemState::CONNECTING);

    TEST_ASSERT_TRUE(supervisor.hasActiveOrchestration_);
    TEST_ASSERT_EQUAL(static_cast<int>(SubState::PENDING),
                      static_cast<int>(supervisor.nextState_.subState));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(supervisor.nextState_.transitionTarget));
}

void test_start_orchestration_writes_all_component_mailboxes() {
    SupervisorV2 supervisor;
    TestComponent board, wifi, audio, cli;
    supervisor.registerComponent(ComponentID::BoardInfo, &board.mailbox, true);
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);

    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.startOrchestration(SystemState::CONNECTING);

    TEST_ASSERT_TRUE(board.mailbox.pending);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(board.mailbox.targetState));
    TEST_ASSERT_TRUE(wifi.mailbox.pending);
    TEST_ASSERT_TRUE(audio.mailbox.pending);
    TEST_ASSERT_TRUE(cli.mailbox.pending);
}

void test_start_orchestration_posts_order_with_correct_bits() {
    SupervisorV2 supervisor;
    TestComponent board, wifi, audio, cli;
    supervisor.registerComponent(ComponentID::BoardInfo, &board.mailbox, true);
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);

    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.startOrchestration(SystemState::CONNECTING);

    TEST_ASSERT_TRUE(supervisor.orderMailbox_.pending);
    int boardBit = 1 << static_cast<int>(ComponentID::BoardInfo);
    int wifiBit  = 1 << static_cast<int>(ComponentID::WiFi);
    int audioBit = 1 << static_cast<int>(ComponentID::AudioRuntime);
    TEST_ASSERT_EQUAL(boardBit | wifiBit | audioBit, supervisor.orderMailbox_.expectedBits);
}

void test_start_orchestration_excludes_degraded_from_order() {
    SupervisorV2 supervisor;
    TestComponent board, wifi;
    supervisor.registerComponent(ComponentID::BoardInfo, &board.mailbox, true);
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.componentStatuses_[static_cast<int>(ComponentID::WiFi)] = ComponentStatus::DEGRADED;

    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.startOrchestration(SystemState::CONNECTING);

    EventBits_t expected = 1 << static_cast<int>(ComponentID::BoardInfo);
    TEST_ASSERT_EQUAL(expected, supervisor.orderMailbox_.expectedBits);
}

void test_start_orchestration_clears_event_group_bits() {
    SupervisorV2 supervisor;
    TestComponent wifi;
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.setup();

    xEventGroupSetBits(supervisor.eventGroup_, 1 << static_cast<int>(ComponentID::WiFi));
    TEST_ASSERT_NOT_EQUAL(0, xEventGroupGetBits(supervisor.eventGroup_));

    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.startOrchestration(SystemState::CONNECTING);

    TEST_ASSERT_EQUAL(0, xEventGroupGetBits(supervisor.eventGroup_));
}

void test_start_orchestration_sets_deadline_in_order() {
    SupervisorV2 supervisor;
    TestComponent wifi;
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);

    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.startOrchestration(SystemState::CONNECTING);

    TEST_ASSERT_NOT_EQUAL(0, supervisor.orderMailbox_.deadlineMs);
}

void test_complete_transition_optional_failed_sets_event_bit() {
    SupervisorV2 supervisor;
    TestComponent cli;
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);
    supervisor.setup();

    supervisor.completeTransition(ComponentID::CLI, TransitionStatus::Failed);

    TEST_ASSERT_TRUE(xEventGroupGetBits(supervisor.eventGroup_) & (1 << static_cast<int>(ComponentID::CLI)));
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::DEGRADED),
                      static_cast<int>(supervisor.componentStatuses_[static_cast<int>(ComponentID::CLI)]));
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_start_orchestration_sets_active_flag);
    RUN_TEST(test_start_orchestration_writes_all_component_mailboxes);
    RUN_TEST(test_start_orchestration_posts_order_with_correct_bits);
    RUN_TEST(test_start_orchestration_excludes_degraded_from_order);
    RUN_TEST(test_start_orchestration_clears_event_group_bits);
    RUN_TEST(test_start_orchestration_sets_deadline_in_order);
    RUN_TEST(test_complete_transition_optional_failed_sets_event_bit);
    return UNITY_END();
}
```

- [ ] **Step 5b.3: Run tests — expect compile failure**

```bash
pio test -e native --filter test_supervisor_v2_orchestration
```

Expected: FAIL — `supervisor_v2_orch.cpp` doesn't exist yet.

- [ ] **Step 5b.4: Create `src/state_machine/supervisor_v2_orch.cpp` with `startOrchestration()`**

```cpp
#include "state_machine/supervisor_v2.h"

#include "core/debug.h"

void SupervisorV2::startOrchestration(SystemState target) {
    EventBits_t bits = 0;
    for (size_t i = 0; i < componentCount; i++) {
        if (componentMailboxes_[i] != nullptr
            && isRequired_[i]
            && componentStatuses_[i] != ComponentStatus::DEGRADED) {
            bits |= (1 << i);
        }
    }

    xEventGroupClearBits(eventGroup_, 0xFFFF);

    for (size_t i = 0; i < componentCount; i++) {
        if (componentMailboxes_[i] != nullptr) {
            postNextComponentState(static_cast<ComponentID>(i));
        }
    }

    bool isForward = (getIndex(target) > getIndex(observedState_));
    uint32_t timeout = isForward
        ? timeoutConfig_.forwardTimeouts[getIndex(target)]
        : timeoutConfig_.backwardTimeouts[getIndex(target)];

    orderMailbox_.post(bits, xTaskGetTickCount() + timeout, target);

    nextState_.transitionTarget = target;
    nextState_.subState = SubState::PENDING;
    hasActiveOrchestration_ = true;
}
```

- [ ] **Step 5b.5: Run tests**

```bash
pio test -e native --filter test_supervisor_v2_orchestration
```

Expected: 7 tests PASS.

- [ ] **Step 5b.6: Run full suite**

```bash
pio test -e native
```

Expected: 97 succeeded (90 baseline + 7 new). 4 pre-existing errors unchanged.

- [ ] **Step 5b.7: Commit**

```bash
git add src/state_machine/supervisor_v2_orch.cpp test/test_supervisor_v2_orchestration/
git commit -m "step 5b: create supervisor_v2_orch.cpp with startOrchestration"
```

---

### Task 5c: Implement checkOrchestrationResponse + 4 tests

- [ ] **Step 5c.1: Add `checkOrchestrationResponse()` to `supervisor_v2_orch.cpp`**

Append after `startOrchestration()`:

```cpp
void SupervisorV2::checkOrchestrationResponse() {
    OrchestrationResult result;
    EventBits_t timedOut;
    if (!responseMailbox_.consume(result, timedOut)) return;

    hasActiveOrchestration_ = false;

    if (result == OrchestrationResult::COMPLETED) {
        nextState_.subState = SubState::COMMITTED;
        setObservedState(nextState_.transitionTarget);
    } else {
        for (size_t i = 0; i < componentCount; i++) {
            if (!(timedOut & (1 << i))) continue;
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

- [ ] **Step 5c.2: Add checkOrchestrationResponse tests to test file**

Add these before closing `}  // namespace`:

```cpp
void test_check_response_completed_advances_observed_state() {
    SupervisorV2 supervisor;
    TestComponent wifi;
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);

    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.startOrchestration(SystemState::CONNECTING);
    supervisor.responseMailbox_.pending = false;

    supervisor.responseMailbox_.post(OrchestrationResult::COMPLETED);

    supervisor.checkOrchestrationResponse();

    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(supervisor.observedState_));
    TEST_ASSERT_FALSE(supervisor.hasActiveOrchestration_);
    TEST_ASSERT_EQUAL(static_cast<int>(SubState::COMMITTED),
                      static_cast<int>(supervisor.nextState_.subState));
}

void test_check_response_timed_out_required_posts_error() {
    SupervisorV2 supervisor;
    TestComponent wifi;
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.setMaxRecoveries(3);

    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.responseMailbox_.post(OrchestrationResult::TIMED_OUT,
        1 << static_cast<int>(ComponentID::WiFi));

    supervisor.checkOrchestrationResponse();

    TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::FAILED),
                      static_cast<int>(supervisor.componentStatuses_[static_cast<int>(ComponentID::WiFi)]));
    TEST_ASSERT_FALSE(supervisor.hasActiveOrchestration_);
}

void test_check_response_timed_out_optional_is_degraded() {
    SupervisorV2 supervisor;
    TestComponent cli;
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);

    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.responseMailbox_.post(OrchestrationResult::TIMED_OUT,
        1 << static_cast<int>(ComponentID::CLI));

    supervisor.checkOrchestrationResponse();

    TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::DEGRADED),
                      static_cast<int>(supervisor.componentStatuses_[static_cast<int>(ComponentID::CLI)]));
    TEST_ASSERT_FALSE(supervisor.hasActiveOrchestration_);
}

void test_check_response_returns_when_no_pending() {
    SupervisorV2 supervisor;

    supervisor.hasActiveOrchestration_ = true;
    supervisor.responseMailbox_.pending = false;

    supervisor.checkOrchestrationResponse();

    TEST_ASSERT_TRUE(supervisor.hasActiveOrchestration_);
}
```

Add RUN_TEST calls before `return UNITY_END();`:
```cpp
    RUN_TEST(test_check_response_completed_advances_observed_state);
    RUN_TEST(test_check_response_timed_out_required_posts_error);
    RUN_TEST(test_check_response_timed_out_optional_is_degraded);
    RUN_TEST(test_check_response_returns_when_no_pending);
```

- [ ] **Step 5c.3: Run tests**

```bash
pio test -e native --filter test_supervisor_v2_orchestration
```

Expected: 11 tests PASS.

- [ ] **Step 5c.4: Run full suite**

```bash
pio test -e native
```

Expected: 101 succeeded (90 baseline + 11 new). 4 pre-existing errors unchanged.

- [ ] **Step 5c.5: Commit**

```bash
git add src/state_machine/supervisor_v2_orch.cpp test/test_supervisor_v2_orchestration/test_main.cpp
git commit -m "step 5c: add checkOrchestrationResponse to supervisor_v2_orch.cpp"
```

---

### Task 5d: Implement orchestrationWorker + wire in setup()

- [ ] **Step 5d.1: Add `orchestrationWorker()` to `supervisor_v2_orch.cpp`**

Append after `checkOrchestrationResponse()`:

```cpp
void orchestrationWorker(void* param) {
    auto* supervisor = static_cast<SupervisorV2*>(param);
    for (;;) {
        EventBits_t expectedBits;
        TickType_t deadlineMs;
        SystemState target;
        if (!supervisor->orderMailbox_.consume(expectedBits, deadlineMs, target)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        TickType_t waitTicks = pdMS_TO_TICKS(deadlineMs - xTaskGetTickCount());
        EventBits_t bits = xEventGroupWaitBits(supervisor->eventGroup_,
                                                expectedBits,
                                                pdTRUE,
                                                pdTRUE,
                                                waitTicks);

        if ((bits & expectedBits) == expectedBits) {
            supervisor->responseMailbox_.post(OrchestrationResult::COMPLETED);
        } else {
            EventBits_t missing = expectedBits & ~bits;
            supervisor->responseMailbox_.post(OrchestrationResult::TIMED_OUT, missing);
        }
    }
}
```

- [ ] **Step 5d.2: Update `setup()` in `supervisor_v2.cpp`**

Add task creation after `loadTransitionTimeoutConfig()`:

```cpp
void SupervisorV2::setup() {
    eventGroup_ = xEventGroupCreateStatic(&eventGroupBuffer_);
    loadTransitionTimeoutConfig();

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

- [ ] **Step 5d.3: Run full suite**

```bash
pio test -e native
```

Expected: 101 succeeded (no regression — worker setup is no-op on native).

- [ ] **Step 5d.4: Remove test_ignore from platformio.ini**

Delete the `test_ignore = test_supervisor_v2_orchestration` line.

- [ ] **Step 5d.5: Commit**

```bash
git add platformio.ini src/state_machine/supervisor_v2.cpp src/state_machine/supervisor_v2_orch.cpp
git commit -m "step 5d: add orchestrationWorker + wire worker task in setup()"
```
