# Step 5: Orchestration Order/Response Mailboxes + startOrchestration + checkOrchestrationResponse + Worker

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the split-task orchestration engine: `OrchestrationOrder`/`OrchestrationResponse` mailboxes for state-machine-to-worker communication, `startOrchestration` (writes order), `checkOrchestrationResponse` (reads response, handles COMPLETED/TIMED_OUT), and the `orchestrationWorker` free function (blocks on `xEventGroupWaitBits`).

**Architecture:** State machine posts orders via `orderMailbox_` (spinlock). Orchestration worker task blocks on `xEventGroupWaitBits(expectedBits, ALL, timeout)`, wakes on completion or timeout, posts result to `responseMailbox_` (spinlock). State machine reads response on next `run()` tick. No polling — the worker uses native FreeRTOS blocking.

**Tech Stack:** C++17, PlatformIO native (mailbox + response tests), Unity test framework, `#define private public` access pattern. Worker function hardware-only (not tested on native).

---

### File Structure

- **Modify:** `src/state_machine/supervisor_v2.h` — add `OrchestrationResult`, `OrchestrationOrder`, `OrchestrationResponse` structs; add `orderMailbox_`/`responseMailbox_`/`workerTaskHandle_` members; remove `expectedBits_`/`orchestrationDeadlineMs_`; add `friend void orchestrationWorker(void*)`; add `checkOrchestrationResponse()` declaration; remove old `checkOrchestrationCompletion()`/`checkStateTimeout()` declarations
- **Modify:** `src/state_machine/supervisor_v2.cpp` — add `startOrchestration()`, `checkOrchestrationResponse()`, `orchestrationWorker()`; update `completeTransition(Failed)` to set event group bit for optional; update `setup()` to create worker task
- **Create:** `test/test_supervisor_v2_orchestration/test_main.cpp` — 9 tests (startOrchestration + checkOrchestrationResponse paths)
- **Modify:** `platformio.ini` — test_ignore during development

---

### Task 5a: Add structs, members, friend declaration, remove old members

- [ ] **Step 5a.1: Add test_ignore to platformio.ini**

```ini
test_framework = unity
test_ignore = test_supervisor_v2_orchestration
```

- [ ] **Step 5a.2: Add `OrchestrationResult`, `OrchestrationOrder`, `OrchestrationResponse` to `supervisor_v2.h`**

Add after the `ErrorEvent` struct definition (after line 136):

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

- [ ] **Step 5a.3: Update SupervisorV2 class members**

Remove these two members from the private section:
```cpp
    TickType_t orchestrationDeadlineMs_{};
    EventBits_t expectedBits_{};
```

Replace the `hasActiveOrchestration_` line with:
```cpp
    bool hasActiveOrchestration_{};
```

Add these members after `hasActiveOrchestration_`:
```cpp
    OrchestrationOrder orderMailbox_{};
    OrchestrationResponse responseMailbox_{};
    TaskHandle_t workerTaskHandle_{};
```

- [ ] **Step 5a.4: Update method declarations in SupervisorV2**

Remove these two declarations:
```cpp
    void checkOrchestrationCompletion();
    void checkStateTimeout();
```

Add this declaration in their place:
```cpp
    void checkOrchestrationResponse();
```

- [ ] **Step 5a.5: Add friend declaration**

Add before the closing `};` of class `SupervisorV2`:
```cpp
    friend void orchestrationWorker(void* param);
```

- [ ] **Step 5a.6: Update `completeTransition()` optional failure path**

Replace the optional failure branch with one that sets the event group bit:

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

- [ ] **Step 5a.7: Run full suite to verify no breakage**

```bash
pio test -e native
```

Expected: 95 succeeded (same as before, existing test suites pass). New test suite ignored.

- [ ] **Step 5a.8: Commit**

```bash
git add src/state_machine/supervisor_v2.cpp src/state_machine/supervisor_v2.h
git commit -m "step 5a: add OrchestrationOrder/Response structs, remove polling members, update completeTransition"
```

---

### Task 5b: Implement startOrchestration + 6 tests

- [ ] **Step 5b.1: Create test file with startOrchestration tests**

**File: `test/test_supervisor_v2_orchestration/test_main.cpp`**

```cpp
#include <unity.h>

#define private public
#include "../../src/state_machine/supervisor_v2.cpp"
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

- [ ] **Step 5b.2: Run tests — expect compile failure** (no `startOrchestration` defined)

```bash
pio test -e native --filter test_supervisor_v2_orchestration
```

Expected: FAIL with `'startOrchestration' is not a member of 'SupervisorV2'`

- [ ] **Step 5b.3: Implement `startOrchestration()` in `supervisor_v2.cpp`**

Replace the old `startOrchestration` if it exists, or add new. The method computes expectedBits, clears the event group, writes component mailboxes, and posts the order:

```cpp
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

- [ ] **Step 5b.4: Run tests**

```bash
pio test -e native --filter test_supervisor_v2_orchestration
```

Expected: 7 tests PASS.

- [ ] **Step 5b.5: Run full suite**

```bash
pio test -e native
```

Expected: 97 succeeded (90 baseline + 7 new). 4 pre-existing errors unchanged.

- [ ] **Step 5b.6: Commit**

```bash
git add src/state_machine/supervisor_v2.cpp test/test_supervisor_v2_orchestration/test_main.cpp
git commit -m "step 5b: implement startOrchestration with order mailbox"
```

---

### Task 5c: Implement checkOrchestrationResponse + 3 tests

- [ ] **Step 5c.1: Implement `checkOrchestrationResponse()` in `supervisor_v2.cpp`**

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
    supervisor.responseMailbox_.pending = false;  // clear anything stale

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
    // Error event was posted — targetState_ will be set to ERROR when consumed
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
git add src/state_machine/supervisor_v2.cpp test/test_supervisor_v2_orchestration/test_main.cpp
git commit -m "step 5c: implement checkOrchestrationResponse"
```

---

### Task 5d: Implement orchestrationWorker + wire up in setup()

- [ ] **Step 5d.1: Implement `orchestrationWorker()` in `supervisor_v2.cpp`**

Add after `checkOrchestrationResponse()`:

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

- [ ] **Step 5d.2: Add `NativeTaskHandle_t` stub for native in `supervisor_v2.h`**

Under `#if !defined(ARDUINO)` block, add after the `using ...` lines:

```cpp
using TaskHandle_t = void*;
inline void xTaskCreatePinnedToCore(void (*)(void*), const char*, uint32_t, void*, uint32_t, TaskHandle_t*, int) {
    // No-op on native — worker is not tested on native
}
```

- [ ] **Step 5d.3: Update `setup()` in `supervisor_v2.cpp`**

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
        0  // Core 0
    );
}
```

- [ ] **Step 5d.4: Run tests**

```bash
pio test -e native
```

Expected: 101 succeeded (no regression — worker setup is no-op on native).

- [ ] **Step 5d.5: Remove test_ignore from platformio.ini**

- [ ] **Step 5d.6: Commit**

```bash
git add platformio.ini src/state_machine/supervisor_v2.cpp src/state_machine/supervisor_v2.h
git commit -m "step 5d: implement orchestrationWorker + wire in setup()"
```
