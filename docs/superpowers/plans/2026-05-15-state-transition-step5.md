# Step 5: Orchestration Method Implementations (startOrchestration, checkOrchestrationResponse, orchestrationWorker)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `OrchestrationOrder`/`OrchestrationResponse` types, replace polling members/methods with split-task equivalents, implement the orchestrator sub-file `orchestrator.cpp` with `startOrchestration`, `checkOrchestrationResponse`, and `orchestrationWorker`. Wire the worker task in `setup()`. Add a minimal `setObservedState()` so the completion path compiles. Update `completeTransition` optional failure to set event group bit.

**Architecture:** All orchestrator methods live in `orchestrator.cpp` (sub-file of SupervisorV2 class). The state machine `setup()` in `supervisor_v2.cpp` creates the worker task. Tests include three `.cpp` files.

**Tech Stack:** C++17, PlatformIO native, Unity test framework, `#define private public`. Worker hardware-only.

**Prerequisite:** Step 4.1 (file split — three .cpp files in place).

---

### File Structure

- **Modify:** `src/supervisor/supervisor_v2.h` — add `OrchestrationResult`, `OrchestrationOrder`, `OrchestrationResponse`; remove `expectedBits_`/`orchestrationDeadlineMs_`; add `orderMailbox_`/`responseMailbox_`/`workerTaskHandle_`; replace method declarations; add friend; slim down `#else` block to include `native_stubs.h`
- **Create:** `src/supervisor/native_stubs.h` — bitmap-backed event group stubs + task stubs for native
- **Modify:** `src/supervisor/supervisor_v2.cpp` — add minimal `setObservedState()`, update `completeTransition` optional failure, update `setup()` to create worker task
- **Modify:** `src/supervisor/orchestrator.cpp` — add `startOrchestration()`, `checkOrchestrationResponse()`, `orchestrationWorker()`
- **Create:** `test/test_supervisor_v2_orchestration/test_main.cpp` — 11 tests
- **Modify:** `platformio.ini` — test_ignore during development, remove at end

---

### Task 5a: Add orchestrator types, replace polling members, add friend and native stubs

- [x] **Step 5a.1: Add `OrchestrationResult`, `OrchestrationOrder`, `OrchestrationResponse` to `supervisor_v2.h`**

Add after the `ErrorEvent` struct definition (after line 136), with doxygen comments following the existing codebase pattern:

```cpp
/** @brief Result of an orchestration attempt.
 *  COMPLETED: all required bits were set before the deadline.
 *  TIMED_OUT: the deadline elapsed with bits still missing.
 */
enum class OrchestrationResult : uint8_t {
    COMPLETED,
    TIMED_OUT
};

/** @brief Order posted by the state machine to the orchestration worker.
 *  Single-slot, spinlock-guarded. Last-write-wins.
 *  The worker reads this via consume() and begins waiting on the event group.
 */
struct OrchestrationOrder {
    bool pending = false;
    EventBits_t expectedBits = 0;        // Bits to wait for
    TickType_t deadlineMs = 0;            // Absolute tick deadline
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

    /** @brief Clear the pending flag under spinlock. Caller reads members directly after.
     *  @return true if an order was pending and was consumed.
     */
    bool consume() {
        portENTER_CRITICAL(&spinlock);
        if (!pending) { portEXIT_CRITICAL(&spinlock); return false; }
        pending = false;
        portEXIT_CRITICAL(&spinlock);
        return true;
    }
};

/** @brief Response posted by the orchestration worker to the state machine.
 *  Single-slot, spinlock-guarded. The state machine reads this on each run() tick.
 */
struct OrchestrationResponse {
    bool pending = false;
    OrchestrationResult result = OrchestrationResult::COMPLETED;
    EventBits_t timedOutComponents = 0;  // Bitmask of components that missed the deadline
    portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;

    void post(OrchestrationResult r, EventBits_t timedOut) {
        portENTER_CRITICAL(&spinlock);
        result = r;
        timedOutComponents = timedOut;
        pending = true;
        portEXIT_CRITICAL(&spinlock);
    }

    /** @brief Clear the pending flag under spinlock. Caller reads members directly after.
     *  @return true if a response was pending and was consumed.
     */
    bool consume() {
        portENTER_CRITICAL(&spinlock);
        if (!pending) { portEXIT_CRITICAL(&spinlock); return false; }
        pending = false;
        portEXIT_CRITICAL(&spinlock);
        return true;
    }
};
```

- [x] **Step 5a.2: Replace polling member variables with split-task equivalents**

In the private section, remove:
```cpp
    TickType_t orchestrationDeadlineMs_{};
    EventBits_t expectedBits_{};
```

Add after `hasActiveOrchestration_`:
```cpp
    OrchestrationOrder orderMailbox_{};
    OrchestrationResponse responseMailbox_{};
    TaskHandle_t workerTaskHandle_{};
    TaskHandle_t supervisorTaskHandle_{};
```

- [x] **Step 5a.3: Replace method declarations**

Remove `checkOrchestrationCompletion()` and `checkStateTimeout()`. Add:
```cpp
    void checkOrchestrationResponse();
```

(`startOrchestration()` declaration stays — signature unchanged.)

- [x] **Step 5a.4: Add friend declaration**

Before closing `};` of class `SupervisorV2`:
```cpp
    friend void orchestrationWorker(void* param);
```

- [x] **Step 5a.5: Create `src/supervisor/native_stubs.h` with bitmap-backed event group stubs**

The current stubs in supervisor_v2.h's `#if !defined(ARDUINO)` block are no-op inline functions
that return 0/nullptr. Extract them into a dedicated file with functional bitmap-backed
implementations so event-group tests work on native. supervisor_v2.h keeps only type aliases
and includes this file.

Create `src/supervisor/native_stubs.h`:

```cpp
#pragma once

#include <cstring>

using EventGroupHandle_t = void*;
struct StaticEventGroup_t { uint8_t data[32]; };
using TickType_t = uint32_t;
using EventBits_t = uint32_t;
using TaskHandle_t = void*;

inline constexpr TickType_t pdMS_TO_TICKS(TickType_t ms) { return ms; }
inline void vTaskDelay(TickType_t) {}

inline EventGroupHandle_t xEventGroupCreateStatic(StaticEventGroup_t* buffer) {
    std::memset(buffer, 0, sizeof(StaticEventGroup_t));
    return buffer;
}
inline EventBits_t xEventGroupClearBits(EventGroupHandle_t handle, EventBits_t bitsToClear) {
    if (handle == nullptr) return 0;
    auto* bits = reinterpret_cast<uint32_t*>(handle);
    *bits &= ~bitsToClear;
    return *bits;
}
inline EventBits_t xEventGroupSetBits(EventGroupHandle_t handle, EventBits_t bitsToSet) {
    if (handle == nullptr) return 0;
    auto* bits = reinterpret_cast<uint32_t*>(handle);
    *bits |= bitsToSet;
    return *bits;
}
inline EventBits_t xEventGroupGetBits(EventGroupHandle_t handle) {
    if (handle == nullptr) return 0;
    return *reinterpret_cast<uint32_t*>(handle);
}
inline void xTaskCreatePinnedToCore(void (*task)(void*), const char*, uint32_t,
                                     void* param, uint32_t, TaskHandle_t*, int) {}
inline TickType_t xTaskGetTickCount() { return 0; }
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return nullptr; }
inline void xTaskNotifyGive(TaskHandle_t) {}
inline uint32_t ulTaskNotifyTake(bool, TickType_t) { return 0; }
```

Replace the `#if !defined(ARDUINO)` block in `supervisor_v2.h` with:

```cpp
#if defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#else
#include "native_stubs.h"
#endif
```

- [x] **Step 5a.6: Run full suite**

```bash
pio test -e native
```

Expected: 97 succeeded. 4 pre-existing errors unchanged. No regressions from structural changes.

- [x] **Step 5a.7: Commit**

```bash
git add src/supervisor/supervisor_v2.h src/supervisor/native_stubs.h
git commit -m "step 5a: add orchestration structs, replace polling members, add friend, extract stubs to native_stubs.h"
```

---

### Task 5b: Minimal setObservedState + completeTransition optional failure fix

  - [x] **Step 5b.1: Add minimal `setObservedState()` to `supervisor_v2.cpp`**

Add after `resetRecoveryIfOutOfError()` in `state_machine.cpp`:

```cpp
/** @brief Commit a new observed state. Minimal version — step 6 adds logging and resetRecoveryIfOutOfError.
 *  @param state The new observed state.
 */
void SupervisorV2::setObservedState(SystemState state) {
    observedState_ = state;
    hasActiveOrchestration_ = false;
}
```

(Full version with logging and `resetRecoveryIfOutOfError` comes in step 6.)

  - [x] **Step 5b.2: Update `completeTransition()` optional failure path**

In `orchestrator.cpp`, change:
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

  - [x] **Step 5b.3: Run full suite**

```bash
pio test -e native
```

Expected: 97 succeeded. 4 pre-existing errors unchanged. No regressions.

  - [x] **Step 5b.4: Commit**

```bash
git add src/supervisor/state_machine.cpp src/supervisor/orchestrator.cpp
git commit -m "step 5b: add minimal setObservedState, update completeTransition optional failure to set event bit"
```

---

### Task 5c: Implement startOrchestration + 7 tests

- [x] **Step 5c.1: Add test_ignore**

```ini
test_framework = unity
test_ignore = test_supervisor_v2_orchestration
```

- [x] **Step 5c.2: Create `test/test_supervisor_v2_orchestration/test_main.cpp`**

Includes all three `.cpp` files:

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
    int cliBit   = 1 << static_cast<int>(ComponentID::CLI);
    TEST_ASSERT_EQUAL(boardBit | wifiBit | audioBit | cliBit, supervisor.orderMailbox_.expectedBits);
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
    supervisor.setup();

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

- [x] **Step 5c.3: Run tests — expect compile failure**

```bash
pio test -e native --filter test_supervisor_v2_orchestration
```

Expected: FAIL — `startOrchestration` not defined yet.

- [x] **Step 5c.4: Add `kAllComponentBits` constant to `supervisor_v2.h` and `startOrchestration()` to `orchestrator.cpp`**

Add after `componentCount` definition in `supervisor_v2.h`:

```cpp
constexpr EventBits_t kAllComponentBits = (1U << componentCount) - 1;
```

Then add `startOrchestration()` to `orchestrator.cpp`:

```cpp
/** @brief Begin an orchestration toward the given target state.
 *  Computes the set of expected bits (required, non-degraded components),
 *  clears the event group, writes all component mailboxes, and posts an
 *  OrchestrationOrder so the worker task can begin the blocking wait.
 *  @param target The intermediate stepping state to orchestrate toward.
 */
void SupervisorV2::startOrchestration(SystemState target) {
    // Build the expected-bits mask: one bit per registered, non-degraded
    // component. Both required and optional components participate in the
    // quorum — optional components are only excluded after they time out or
    // explicitly fail (at which point they become DEGRADED).
    EventBits_t bits = 0;
    for (size_t i = 0; i < componentCount; i++) {
        if (componentMailboxes_[i] != nullptr
            && componentStatuses_[i] != ComponentStatus::DEGRADED) {
            bits |= (1 << i);
        }
    }

    xEventGroupClearBits(eventGroup_, kAllComponentBits);

    // Set the transition target before writing mailboxes — postNextComponentState
    // reads nextState_.transitionTarget to know what to write.
    nextState_.transitionTarget = target;

    // Write the stepping target to every registered component's mailbox.
    // Components read this on their own task loop and react accordingly.
    for (size_t i = 0; i < componentCount; i++) {
        if (componentMailboxes_[i] != nullptr) {
            postNextComponentState(static_cast<ComponentID>(i));
        }
    }

    // Look up the per-state timeout. Forward if the target has a higher rank
    // than the current observed state, backward otherwise.
    bool isForward = (getIndex(target) > getIndex(observedState_));
    uint32_t timeout = getTransitionTimeout(target, isForward);

    orderMailbox_.post(bits, xTaskGetTickCount() + timeout, target);

    nextState_.subState = SubState::PENDING;
    hasActiveOrchestration_ = true;
}
```

- [x] **Step 5c.5: Run tests**

```bash
pio test -e native --filter test_supervisor_v2_orchestration
```

Expected: 7 tests PASS.

- [x] **Step 5c.6: Run full suite**

```bash
pio test -e native
```

Expected: 104 succeeded (97 baseline + 7 new). 4 pre-existing errors.

- [x] **Step 5c.7: Commit**

```bash
git add src/supervisor/supervisor_v2.h src/supervisor/orchestrator.cpp test/test_supervisor_v2_orchestration/
git commit -m "step 5c: add kAllComponentBits constant, add startOrchestration to orchestrator.cpp"
```

---

### Task 5d: Implement checkOrchestrationResponse + 4 tests

- [ ] **Step 5d.1: Add `checkOrchestrationResponse()` to `orchestrator.cpp`**

```cpp
/** @brief Check for a pending orchestration response from the worker task.
 *  Reads responseMailbox_ (non-blocking, spinlock inside).
 *  On COMPLETED: advances observedState_ to the orchestration target.
 *  On TIMED_OUT: clears the active flag and handles overdue components.
 */
void SupervisorV2::checkOrchestrationResponse() {
    if (!responseMailbox_.consume()) return;

    // Clear the active flag regardless of outcome — the orchestration cycle
    // is done (either all bits arrived or the deadline elapsed).
    hasActiveOrchestration_ = false;

    if (responseMailbox_.result == OrchestrationResult::COMPLETED) {
        nextState_.subState = SubState::COMMITTED;
        setObservedState(nextState_.transitionTarget);
    } else {
        // TIMED_OUT — some components did not set their event group bits
        EventBits_t timedOut = responseMailbox_.timedOutComponents;
        for (size_t i = 0; i < componentCount; i++) {
            if (!(timedOut & (1 << i))) continue;
            if (isRequired_[i]) {
                componentStatuses_[i] = ComponentStatus::FAILED;
                postErrorEvent("transition timeout", static_cast<ComponentID>(i));
            } else {
                componentStatuses_[i] = ComponentStatus::DEGRADED;
            }
        }
        // The posted error event will be consumed on the next run() tick,
        // setting targetState_ to ERROR or FATAL as appropriate.
    }
}
```

- [ ] **Step 5d.2: Add tests to test file**

Add before `}  // namespace`:

```cpp
void test_check_response_completed_advances_observed_state() {
    SupervisorV2 supervisor;
    TestComponent wifi;
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);

    supervisor.observedState_ = SystemState::BOOTING;
    supervisor.startOrchestration(SystemState::CONNECTING);
    supervisor.responseMailbox_.pending = false;

    supervisor.responseMailbox_.post(OrchestrationResult::COMPLETED, 0);

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

- [ ] **Step 5d.3: Run tests**

```bash
pio test -e native --filter test_supervisor_v2_orchestration
```

Expected: 11 tests PASS.

- [ ] **Step 5d.4: Run full suite**

```bash
pio test -e native
```

Expected: 108 succeeded (97 baseline + 11 new). 4 pre-existing errors.

- [ ] **Step 5d.5: Commit**

```bash
git add src/supervisor/orchestrator.cpp test/test_supervisor_v2_orchestration/test_main.cpp
git commit -m "step 5d: add checkOrchestrationResponse to orchestrator.cpp"
```

---

### Task 5e: Implement orchestrationWorker + wire in setup()

- [ ] **Step 5e.1: Add `orchestrationWorker()` to `orchestrator.cpp`**

```cpp
/** @brief Orchestration worker task. Reads orders from orderMailbox_ and blocks
 *  on xEventGroupWaitBits until all expected bits are set or the deadline expires.
 *  Posts a result back to responseMailbox_ for the state machine to consume.
 *  @param param Pointer to the SupervisorV2 instance (cast from void*).
 */
void orchestrationWorker(void* param) {
    auto* supervisor = static_cast<SupervisorV2*>(param);
    for (;;) {
        // Wait for an order from the state machine
        if (!supervisor->orderMailbox_.consume()) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // Block until all bits set, OR the deadline passes. FreeRTOS handles
        // the timeout internally — the pdTRUE flags mean clear-on-exit and
        // wait-for-all-bits respectively.
        TickType_t waitTicks = pdMS_TO_TICKS(supervisor->orderMailbox_.deadlineMs - xTaskGetTickCount());
        EventBits_t bits = xEventGroupWaitBits(supervisor->eventGroup_,
                                                supervisor->orderMailbox_.expectedBits,
                                                pdTRUE,
                                                pdTRUE,
                                                waitTicks);

        if ((bits & supervisor->orderMailbox_.expectedBits) == supervisor->orderMailbox_.expectedBits) {
            supervisor->responseMailbox_.post(OrchestrationResult::COMPLETED, 0);
        } else {
            // Timeout — find which bits are still missing
            EventBits_t missing = supervisor->orderMailbox_.expectedBits & ~bits;
            supervisor->responseMailbox_.post(OrchestrationResult::TIMED_OUT, missing);
        }
        xTaskNotifyGive(supervisor->supervisorTaskHandle_);
    }
}
```

- [ ] **Step 5e.2: Update `setup()` in `supervisor_v2.cpp`**

```cpp
void SupervisorV2::setup() {
    eventGroup_ = xEventGroupCreateStatic(&eventGroupBuffer_);
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

- [ ] **Step 5e.3: Run full suite**

```bash
pio test -e native
```

Expected: 108 succeeded (no regression — worker setup is no-op on native).

- [ ] **Step 5e.4: Remove test_ignore**

- [ ] **Step 5e.5: Commit**

```bash
git add platformio.ini src/supervisor/supervisor_v2.cpp src/supervisor/orchestrator.cpp
git commit -m "step 5e: add orchestrationWorker + wire worker task in setup()"
```

---

### Task 5f: Wire task notifications into postStateRequest and postErrorEvent

- [ ] **Step 5f.1: Add `xTaskNotifyGive` to `postStateRequest()` in `orchestrator.cpp`**

```cpp
void SupervisorV2::postStateRequest(SystemState target) {
    portENTER_CRITICAL(&stateRequestMailbox_.spinlock);
    stateRequestMailbox_.pending = true;
    stateRequestMailbox_.requestedTarget = target;
    portEXIT_CRITICAL(&stateRequestMailbox_.spinlock);

    xTaskNotifyGive(supervisorTaskHandle_);
}
```

- [ ] **Step 5f.2: Add `xTaskNotifyGive` to `postErrorEvent()` in `orchestrator.cpp`**

```cpp
void SupervisorV2::postErrorEvent(DebugReason reason, ComponentID source) {
    portENTER_CRITICAL(&errorEvent_.spinlock);
    if (!errorEvent_.pending) {
        errorEvent_.pending = true;
        errorEvent_.reason = reason;
        errorEvent_.source = source;
    }
    portEXIT_CRITICAL(&errorEvent_.spinlock);

    xTaskNotifyGive(supervisorTaskHandle_);
}
```

- [ ] **Step 5f.3: Run full suite**

```bash
pio test -e native
```

Expected: 108 succeeded (no regression — notification stubs are no-ops on native).

- [ ] **Step 5f.4: Commit**

```bash
git add src/supervisor/orchestrator.cpp
git commit -m "step 5f: wire xTaskNotifyGive into postStateRequest and postErrorEvent"
```
