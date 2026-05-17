# Audit Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the 14 open issues from the Supervisor V2 code audit (2026-05-16): C3 FATAL dwell bug, D3–D5/D7/D8 design cleanups, T1–T5 test infrastructure improvements, and file a user story for D1/D2.

**Architecture:** A new `fatal_task.cpp` gets a dedicated FreeRTOS task that owns all FATAL behavior (logging, LED, cleanup, dwell timer, deep sleep). The supervisor spawns it once on FATAL entry and goes idle — no cancellation needed (FATAL is one-way). Test access to private members moves to a `S2V2Access` friend struct, enabling removal of `#define private public`. Small type-safety fixes (DebugReason, bounds check) are applied inline.

**Tech Stack:** C++20, Arduino-ESP32 (FreeRTOS), PlatformIO native tests (Unity), pioarduino platform

---

## File Structure Map

```
Create:
  src/supervisor/fatal_task.cpp          -- FATAL task function (logging, dwell, deep sleep)
  test/support/s2v2_access.h             -- Test-accessor friend struct for SupervisorV2
  test/test_fatal_task/test_main.cpp     -- Tests for fatalTask()

Modify:
  src/supervisor/supervisor_v2.h         -- Friend decl, FATAL members, remove D3/D4/D5 dead code
  src/supervisor/supervisor_v2.cpp       -- D8 bounds check in registerComponent()
  src/supervisor/state_machine.cpp       -- Remove handleFatal(), update run() + FATAL spawn
  src/component_types.h                  -- D7: DebugReason -> char[48] buffer
  src/supervisor/native_stubs.h          -- T3: settable tick counter, xTaskCreatePinnedToCore
  platformio.ini                         -- T1: -DUNIT_TEST, compile .cpp separately
  test/test_supervisor_v2_run/test_main.cpp           -- Migrate to S2V2Access
  test/test_supervisor_v2_step_6/test_main.cpp         -- Replace handleFatal tests with fatalTask tests
  test/test_supervisor_v2_orchestration/test_main.cpp  -- Migrate to S2V2Access
  test/test_supervisor_v2_remaining_paths/test_main.cpp-- Migrate to S2V2Access
  test/test_supervisor_v2_registration/test_main.cpp   -- T4 real assertions + migrate
  test/test_supervisor_v2_mailbox_spinlock/test_main.cpp -- Migrate to S2V2Access

User story only (no code):
  requirements/user-stories/open/             -- File US-00XX for D1/D2 refactor
```

---

### Task 1: Create FATAL task function

**Context:** The FATAL task is a standalone FreeRTOS task spawned once by `run()` when `observedState_` enters FATAL. It owns: logging the FATAL entry, placeholder hooks for LED/cleanup, a 60-second `vTaskDelay`, and `esp_deep_sleep_start()`. On native builds, the task is testable as a plain function call — tests call `fatalTask(supervisor)` directly and verify `fatalDeadlineElapsed_` is set. The old `handleFatal()` method and its member variables are removed in Task 2.

**Files:**
- Create: `src/supervisor/fatal_task.cpp`
- Create: `test/test_fatal_task/test_main.cpp`

- [x] **Step 1: Write the failing test**

Create `test/test_fatal_task/test_main.cpp`:

```cpp
#include <unity.h>

#define private public
#include "../../src/supervisor/supervisor_v2.cpp"
#include "../../src/supervisor/orchestrator.cpp"
#include "../../src/supervisor/state_machine.cpp"
#include "../../src/supervisor/fatal_task.cpp"
#undef private

// Forward-declare the free function under test (defined in fatal_task.cpp)
void fatalTask(SupervisorV2* supervisor);

namespace {

void test_fatal_task_sets_elapsed_flag() {
    SupervisorV2 supervisor;
    supervisor.fatalEnteredTicks_ = 1;  // xTaskGetTickCount() returns 0, delta wraps to UINT32_MAX >= 60s

    fatalTask(&supervisor);

    TEST_ASSERT_TRUE(supervisor.fatalDeadlineElapsed_);
}

void test_fatal_task_does_not_set_elapsed_before_dwell() {
    SupervisorV2 supervisor;
    // fatalEnteredTicks_ defaults to 0, xTaskGetTickCount() returns 0 => delta 0 < 60s
    // BUT native stubs make vTaskDelay a no-op, so time doesn't pass.
    // This test verifies: when deadline has NOT elapsed, flag stays false.
    // We simulate no-time-passed by leaving fatalEnteredTicks_ at 0.
    supervisor.fatalEnteredTicks_ = 0;

    fatalTask(&supervisor);

    TEST_ASSERT_FALSE(supervisor.fatalDeadlineElapsed_);
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_fatal_task_sets_elapsed_flag);
    RUN_TEST(test_fatal_task_does_not_set_elapsed_before_dwell);
    return UNITY_END();
}
```

- [x] **Step 2: Run test to verify it fails**

```bash
pio test -e native -f test_fatal_task
```
Expected: FAIL — `fatalTask` is not defined (linker error).

- [x] **Step 3: Write minimal implementation**

Create `src/supervisor/fatal_task.cpp`:

```cpp
#include "supervisor/supervisor_v2.h"
#include "core/debug.h"

namespace {

constexpr const char* kLogSource = "Fatal";
constexpr TickType_t kFatalDwellMs = 60000;

}  // namespace

void fatalTask(SupervisorV2* supervisor) {
    PROD_LOG(kLogSource, "FATAL — system entering fatal state");

    // TODO: LED signalling placeholder
    // e.g. set LED to solid red pattern

    // TODO: component cleanup placeholder
    // e.g. signal Wi-Fi disconnect, stop audio pipeline

    vTaskDelay(pdMS_TO_TICKS(kFatalDwellMs));

    // After dwell, check if deadline elapsed (simulated time may run faster)
    TickType_t elapsed = xTaskGetTickCount() - supervisor->fatalEnteredTicks_;
    if (elapsed >= pdMS_TO_TICKS(kFatalDwellMs)) {
        supervisor->fatalDeadlineElapsed_ = true;
    }
#if defined(ARDUINO)
    esp_deep_sleep_start();
#endif
}
```

- [x] **Step 4: Run test to verify it passes**

```bash
pio test -e native -f test_fatal_task
```
Expected: PASS (2 tests).

- [x] **Step 5: Commit**

```bash
git add src/supervisor/fatal_task.cpp test/test_fatal_task/test_main.cpp
git commit -m "audit-fixes: add FATAL task with dwell timer and deep sleep"
```

---

### Task 2: Integrate FATAL task into run(), remove handleFatal()

**Context:** `run()` now spawns the FATAL task on first entry into FATAL and then goes idle. The old `handleFatal()` method and its associated members (`fatalEntered_`, `fatalEnteredTicks_`, `fatalDeadlineElapsed_`, `kFatalDwellMs`) are removed from `state_machine.cpp`. However `fatalEnteredTicks_` and `fatalDeadlineElapsed_` stay as members on the class — the FATAL task needs them for the dwell check and tests observe them.

**Files:**
- Modify: `src/supervisor/supervisor_v2.h` — add `fatalTaskSpawned_`, `fatalTaskHandle_`, keep `fatalEnteredTicks_`/`fatalDeadlineElapsed_`
- Modify: `src/supervisor/state_machine.cpp` — update `run()`, remove `handleFatal()`, remove `kFatalDwellMs`

- [x] **Step 1: Add FATAL task members to header**

In `src/supervisor/supervisor_v2.h`, after the `firstOrchestration_` member (around line 372), add:

```cpp
    TaskHandle_t fatalTaskHandle_{};
    bool fatalTaskSpawned_{};

    TickType_t fatalEnteredTicks_{};
    bool fatalDeadlineElapsed_{};
```

And in the private method declarations section, replace the `handleFatal()` declaration with:

```cpp
    void spawnFatalTask();
```

Remove the `bool fatalEntered_{};` member (line 371).

- [x] **Step 2: Update run() and add spawnFatalTask()**

In `src/supervisor/state_machine.cpp`, remove the entire `handleFatal()` method (lines 230-243) and the `kFatalDwellMs` constant (line 11).

Replace the FATAL section in `run()` (lines 130-132) with:

```cpp
    if (observedState_ == SystemState::FATAL) {
        if (!fatalTaskSpawned_) {
            fatalTaskSpawned_ = true;
            spawnFatalTask();
        }
    }
```

Add `spawnFatalTask()` before `run()`:

```cpp
void SupervisorV2::spawnFatalTask() {
    fatalEnteredTicks_ = xTaskGetTickCount();
    PROD_LOG(kLogSource, "Spawning FATAL task, dwell=%ums", static_cast<unsigned int>(pdTICKS_TO_MS(pdMS_TO_TICKS(60000))));
#if defined(ARDUINO)
    xTaskCreatePinnedToCore(
        reinterpret_cast<void(*)(void*)>(fatalTask),
        "FatalTask",
        2048,
        this,
        1,
        &fatalTaskHandle_,
        1  // Core 1 — same core as state machine
    );
#endif
}
```

- [x] **Step 3: Forward-declare fatalTask in header**

At the bottom of the public method declarations in `supervisor_v2.h`, add:

```cpp
    friend void fatalTask(SupervisorV2* supervisor);
```

- [x] **Step 4: Run all tests to verify nothing broke**

```bash
pio test -e native
```
Expected: The 3 old `handleFatal()` tests in `test_supervisor_v2_step_6` will fail (method removed). The new `test_fatal_task` tests should pass. All other tests should pass.

- [x] **Step 5: Update the old handleFatal() tests in test_supervisor_v2_step_6**

In `test/test_supervisor_v2_step_6/test_main.cpp`, replace the three `handleFatal` tests (lines 117-144) with tests that call fatalTask directly:

```cpp
void fatalTask(SupervisorV2* supervisor);

void test_fatal_task_sets_elapsed_flag() {
    SupervisorV2 supervisor;
    supervisor.fatalEnteredTicks_ = 1;

    fatalTask(&supervisor);

    TEST_ASSERT_TRUE(supervisor.fatalDeadlineElapsed_);
}

void test_fatal_task_no_elapsed_before_deadline() {
    SupervisorV2 supervisor;
    supervisor.fatalEnteredTicks_ = 0;

    fatalTask(&supervisor);

    TEST_ASSERT_FALSE(supervisor.fatalDeadlineElapsed_);
}

void test_run_wakes_then_spawns_fatal_task() {
    SupervisorV2 supervisor;
    supervisor.observedState_ = SystemState::FATAL;

    supervisor.run();

    TEST_ASSERT_TRUE(supervisor.fatalTaskSpawned_);
}
```

Update the `RUN_TEST` calls at the bottom to match the new test names:

```cpp
    RUN_TEST(test_fatal_task_sets_elapsed_flag);
    RUN_TEST(test_fatal_task_no_elapsed_before_deadline);
    RUN_TEST(test_run_wakes_then_spawns_fatal_task);
```

- [x] **Step 6: Run tests to verify**

```bash
pio test -e native
```
Expected: All tests pass.

- [x] **Step 7: Commit**

```bash
git add src/supervisor/state_machine.cpp src/supervisor/supervisor_v2.h test/test_supervisor_v2_step_6/test_main.cpp
git commit -m "audit-fixes: integrate FATAL task, remove handleFatal"
```

---

### Task 3: Add test accessor infrastructure (T1)

**Context:** Replace `#define private public` with a `S2V2Access` friend struct that provides controlled read/write access to private members. The struct is defined in `test/support/s2v2_access.h`, guarded by `#ifdef UNIT_TEST`. The `SupervisorV2` class declares it as a friend. Tests include `s2v2_access.h` and use `S2V2Access::setObservedState(s, state)` etc. instead of `s.observedState_ = state`.

**Files:**
- Create: `test/support/s2v2_access.h`
- Modify: `src/supervisor/supervisor_v2.h` — friend declaration
- Modify: `platformio.ini` — add `-DUNIT_TEST`

- [x] **Step 1: Add friend declaration to header**

In `src/supervisor/supervisor_v2.h`, after the existing `friend void fatalTask(SupervisorV2* supervisor);` line, add:

```cpp
#ifdef UNIT_TEST
    friend struct S2V2Access;
#endif
```

- [x] **Step 2: Create the test accessor header**

Create `test/support/s2v2_access.h`:

```cpp
#pragma once

#ifndef UNIT_TEST
#error "s2v2_access.h requires -DUNIT_TEST build flag"
#endif

#include "src/supervisor/supervisor_v2.h"

struct S2V2Access {
    static void setObservedState(SupervisorV2& s, SystemState state) { s.observedState_ = state; }
    static SystemState getObservedState(const SupervisorV2& s) { return s.observedState_; }

    static void setTargetState(SupervisorV2& s, SystemState state) { s.targetState_ = state; }
    static SystemState getTargetState(const SupervisorV2& s) { return s.targetState_; }

    static void setHasActiveOrchestration(SupervisorV2& s, bool v) { s.hasActiveOrchestration_ = v; }
    static bool getHasActiveOrchestration(const SupervisorV2& s) { return s.hasActiveOrchestration_; }

    static ActiveTransition& nextState(SupervisorV2& s) { return s.nextState_; }

    static RetryPolicy& retryPolicy(SupervisorV2& s) { return s.retryPolicy_; }
    static ErrorEvent& errorEvent(SupervisorV2& s) { return s.errorEvent_; }
    static Mailbox& stateRequestMailbox(SupervisorV2& s) { return s.stateRequestMailbox_; }

    static void setFatalEnteredTicks(SupervisorV2& s, TickType_t v) { s.fatalEnteredTicks_ = v; }
    static bool getFatalDeadlineElapsed(const SupervisorV2& s) { return s.fatalDeadlineElapsed_; }
    static void setFatalDeadlineElapsed(SupervisorV2& s, bool v) { s.fatalDeadlineElapsed_ = v; }

    static void setFatalTaskSpawned(SupervisorV2& s, bool v) { s.fatalTaskSpawned_ = v; }
    static bool getFatalTaskSpawned(const SupervisorV2& s) { return s.fatalTaskSpawned_; }

    static SystemState getLastTargetBeforeError(const SupervisorV2& s) { return s.lastTargetBeforeError_; }
    static void setLastTargetBeforeError(SupervisorV2& s, SystemState state) { s.lastTargetBeforeError_ = state; }

    static ComponentStatus getComponentStatus(const SupervisorV2& s, ComponentID id) {
        return s.componentStatuses_[static_cast<int>(id)];
    }
    static void setComponentStatus(SupervisorV2& s, ComponentID id, ComponentStatus status) {
        s.componentStatuses_[static_cast<int>(id)] = status;
    }

    static ComponentMailbox* getComponentMailbox(SupervisorV2& s, ComponentID id) {
        return s.componentMailboxes_[static_cast<int>(id)];
    }

    static bool getOrderPending(const SupervisorV2& s) { return s.orderMailbox_.pending; }
};
```

- [x] **Step 3: Add -DUNIT_TEST to native build flags**

In `platformio.ini`, under `[env:native]`, add to `build_flags`:

```
    -DUNIT_TEST
    -Itest/support
```

- [x] **Step 4: Verify compilation with a quick test**

```bash
pio test -e native -f test_fatal_task
```
Expected: All tests pass (verify the test accessor compiles).

- [x] **Step 5: Commit**

```bash
git add test/support/s2v2_access.h src/supervisor/supervisor_v2.h platformio.ini
git commit -m "audit-fixes: add S2V2Access test accessor infrastructure"
```

---

### Task 4: Migrate tests away from #define private public (T1 continued)

**Context:** Update all 6 test files that use `#define private public` to use `S2V2Access` instead. This is the mechanical migration — find every `supervisor.privateMember_` access and replace with the equivalent `S2V2Access::method()`.

**Files:**
- Modify: `test/test_supervisor_v2_run/test_main.cpp`
- Modify: `test/test_supervisor_v2_orchestration/test_main.cpp`
- Modify: `test/test_supervisor_v2_remaining_paths/test_main.cpp`
- Modify: `test/test_supervisor_v2_mailbox_spinlock/test_main.cpp`

- [x] **Step 1: Migrate test_supervisor_v2_run**

In `test/test_supervisor_v2_run/test_main.cpp`, replace lines 1-7:

```cpp
#include <unity.h>

#include "support/s2v2_access.h"
#define private public
#include "../../src/supervisor/supervisor_v2.cpp"
#include "../../src/supervisor/orchestrator.cpp"
#include "../../src/supervisor/state_machine.cpp"
#undef private
```

becomes:

```cpp
#include <unity.h>

#include "support/s2v2_access.h"
#include "../../src/supervisor/supervisor_v2.cpp"
#include "../../src/supervisor/orchestrator.cpp"
#include "../../src/supervisor/state_machine.cpp"
```

Then replace all direct member accesses. In `test_run_already_at_target_does_nothing()`:

```cpp
    S2V2Access::setObservedState(supervisor, SystemState::BOOTING);
    S2V2Access::setTargetState(supervisor, SystemState::BOOTING);
    S2V2Access::setHasActiveOrchestration(supervisor, false);
    // ...
    TEST_ASSERT_FALSE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::BOOTING),
                      static_cast<int>(S2V2Access::getObservedState(supervisor)));
```

In `test_run_steps_toward_target()`:

```cpp
    S2V2Access::setObservedState(supervisor, SystemState::BOOTING);
    S2V2Access::setTargetState(supervisor, SystemState::CONNECTING);
    S2V2Access::setHasActiveOrchestration(supervisor, false);
    // ...
    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(S2V2Access::nextState(supervisor).transitionTarget));
    TEST_ASSERT_TRUE(S2V2Access::getOrderPending(supervisor));
```

Apply the same pattern to all remaining test functions in this file — replace every `supervisor.observedState_`, `supervisor.targetState_`, `supervisor.hasActiveOrchestration_`, `supervisor.nextState_`, `supervisor.retryPolicy_`, `supervisor.errorEvent_` with S2V2Access equivalents.

- [x] **Step 2: Migrate test_supervisor_v2_orchestration**

Same pattern — remove `#define private public` and replace line 3-7 preamble with:

```cpp
#include <unity.h>

#include "support/s2v2_access.h"
#include "../../src/supervisor/supervisor_v2.cpp"
#include "../../src/supervisor/orchestrator.cpp"
#include "../../src/supervisor/state_machine.cpp"
```

Replace all private member accesses with S2V2Access calls throughout the file.

- [x] **Step 3: Migrate test_supervisor_v2_remaining_paths**

Same mechanical change — remove `#define private public`, add `s2v2_access.h`, replace all private accesses.

- [x] **Step 4: Migrate test_supervisor_v2_mailbox_spinlock**

Same mechanical change.

- [x] **Step 5: Run all tests**

```bash
pio test -e native
```
Expected: All tests pass. No test uses `#define private public` anymore.

- [x] **Step 6: Commit**

```bash
git add test/test_supervisor_v2_run/test_main.cpp \
        test/test_supervisor_v2_orchestration/test_main.cpp \
        test/test_supervisor_v2_remaining_paths/test_main.cpp \
        test/test_supervisor_v2_mailbox_spinlock/test_main.cpp
git commit -m "audit-fixes: migrate tests from #define private public to S2V2Access"
```

---

### Task 5: Enable separate .cpp compilation for native tests (T2)

**Context:** Currently native tests include `.cpp` files directly because `build_src_filter = +<native_placeholder.cpp>` only compiles the placeholder. Change the filter to compile all supervisor `.cpp` files normally, then remove direct includes from test files. This requires a `#ifndef UNIT_TEST` guard in `main.cpp` since it defines `setup()`/`loop()` which conflict with tests.

**Files:**
- Modify: `platformio.ini` — `build_src_filter`
- Modify: `test/test_supervisor_v2_run/test_main.cpp` — remove .cpp includes, add header includes
- Modify: `test/test_supervisor_v2_orchestration/test_main.cpp` — same
- Modify: `test/test_supervisor_v2_remaining_paths/test_main.cpp` — same
- Modify: `test/test_supervisor_v2_registration/test_main.cpp` — same
- Modify: `test/test_supervisor_v2_step_6/test_main.cpp` — same
- Modify: `test/test_supervisor_v2_mailbox_spinlock/test_main.cpp` — same
- Modify: `test/test_supervisor_v2_get_next_state/test_main.cpp` — same
- Modify: `test/test_fatal_task/test_main.cpp` — same
- Modify: `src/main.cpp` — add `#ifndef UNIT_TEST` guard

- [x] **Step 1: Update platformio.ini native env**

In `platformio.ini`, change the `[env:native]` section:

```ini
[env:native]
platform = native
test_framework = unity
build_flags =
    -std=gnu++20
    -DUNIT_TEST
    -Ilib/cli_command_logic/include
    -Ilib/audio/include
    -Itest/support
```

Remove the `test_build_src = no` and `build_src_filter = +<native_placeholder.cpp>` lines. PlatformIO native tests will now build all source files normally.

- [x] **Step 2: Guard main.cpp content**

In `src/main.cpp`, wrap the entire file content (after includes) in:

```cpp
#ifndef UNIT_TEST

// ... existing setup(), loop(), component declarations ...

#endif  // UNIT_TEST
```

- [ ] **Step 3: Update one test file as template — test_supervisor_v2_run**

Replace the .cpp includes:

```cpp
#include <unity.h>

#include "support/s2v2_access.h"
#include "supervisor/supervisor_v2.h"
#include "supervisor/orchestrator.h"
```

Remove these three lines:
```cpp
#include "../../src/supervisor/supervisor_v2.cpp"
#include "../../src/supervisor/orchestrator.cpp"
#include "../../src/supervisor/state_machine.cpp"
```

- [x] **Step 4: Run only that test to verify linking**

```bash
pio test -e native -f test_supervisor_v2_run
```
Expected: Compile error about undefined references — the `state_machine.cpp` functions (`getNextState`, etc.) may not be linked if they live in a filtered-out source. If this fails, iterate: ensure all necessary .cpp files are compiled by the native env.

If compilation succeeds but tests fail: fix any linking issues before proceeding.

- [x] **Step 5: Update remaining 7 test files**

Apply the same pattern to all other test files that include `.cpp`:

Remove:
```cpp
#include "../../src/supervisor/supervisor_v2.cpp"
#include "../../src/supervisor/orchestrator.cpp"
#include "../../src/supervisor/state_machine.cpp"
```

Or for `supervisor.cpp` includes:
```cpp
#include "../../src/supervisor/supervisor.cpp"
```

Add:
```cpp
#include "supervisor/supervisor_v2.h"
```

And for tests that need `fatalTask`:
```cpp
void fatalTask(SupervisorV2* supervisor);  // forward declare
```

- [x] **Step 6: Run full test suite**

```bash
pio test -e native
```
Expected: All 67+ tests pass. No test includes `.cpp` files directly.

- [x] **Step 7: Delete native_placeholder.cpp if unused**

Check if `native_placeholder.cpp` is still referenced:

```bash
grep -r "native_placeholder" platformio.ini
```

If no matches, delete it:

```bash
rm src/native_placeholder.cpp
```

- [x] **Step 8: Commit**

```bash
git add platformio.ini src/main.cpp \
        test/test_supervisor_v2_run/test_main.cpp \
        test/test_supervisor_v2_orchestration/test_main.cpp \
        test/test_supervisor_v2_remaining_paths/test_main.cpp \
        test/test_supervisor_v2_registration/test_main.cpp \
        test/test_supervisor_v2_step_6/test_main.cpp \
        test/test_supervisor_v2_mailbox_spinlock/test_main.cpp \
        test/test_supervisor_v2_get_next_state/test_main.cpp \
        test/test_fatal_task/test_main.cpp
git commit -m "audit-fixes: compile .cpp separately in native tests"
```

---

### Task 6: D3 — Remove duplicate X-macro from supervisor_v2.h

**Context:** `SYSTEM_STATE_X` is defined in `component_types.h` (line 20) and duplicated in `supervisor_v2.h` (lines 17-26) inside a dead `#ifndef` guard. The `#undef` at line 66 is also dead. Remove both. The `detail` namespace at lines 28-62 uses `SYSTEM_STATE_X` as a macro — it still works because `component_types.h` defines it (included at line 14).

**Files:**
- Modify: `src/supervisor/supervisor_v2.h` — remove lines 17-26 and line 66

- [x] **Step 1: Remove dead X-macro block and #undef**

In `src/supervisor/supervisor_v2.h`, delete lines 17-26:

```cpp
#ifndef SYSTEM_STATE_X       // ← DELETE (line 17)
#define SYSTEM_STATE_X(V) \  // ← DELETE
    V(FATAL, 0) \            // ← DELETE
    V(ERROR, 10) \           // ← DELETE
    V(SLEEP, 20) \           // ← DELETE
    V(BOOTING, 30) \         // ← DELETE
    V(CONNECTING, 40) \      // ← DELETE
    V(READY, 50) \           // ← DELETE
    V(LIVE, 60)              // ← DELETE
#endif                       // ← DELETE (line 26)
```

Delete line 66:

```cpp
#undef SYSTEM_STATE_X        // ← DELETE
```

- [x] **Step 2: Run tests to verify no regression**

```bash
pio test -e native
```
Expected: All tests pass. The `detail::kValues` and `detail::kRoute` arrays still compile from `component_types.h`'s definition.

- [x] **Step 3: Commit**

```bash
git add src/supervisor/supervisor_v2.h
git commit -m "audit-fixes: remove dead duplicate SYSTEM_STATE_X macro"
```

---

### Task 7: D4 — Remove entire SubState enum (dead code)

**Context:** The `SubState` enum (`PENDING`, `COMMITTED`, `FAILED`) and the `subState` field on `ActiveTransition` are written but never read by any production code. Only tests assert on the values — those assertions can be removed without coverage loss since they duplicate checks on `hasActiveOrchestration_` and `observedState_`.

**Files:**
- Modify: `src/supervisor/supervisor_v2.h` — remove `SubState` enum and `subState` member
- Modify: `test/test_supervisor_v2_orchestration/test_main.cpp` — remove 2 subState assertions
- Modify: `test/test_supervisor_v2_remaining_paths/test_main.cpp` — remove 1 subState assertion

- [x] **Step 1: Remove SubState enum and subState field**

In `src/supervisor/supervisor_v2.h`:

Delete the entire `SubState` enum (lines 95-99):

```cpp
enum class SubState {
    PENDING,
    COMMITTED,
    FAILED
};
```

In the `ActiveTransition` struct (around line 101), remove the `subState` member:

```cpp
struct ActiveTransition {
    SubState subState = SubState::PENDING;  // ← DELETE this line
    SystemState transitionTarget;
};
```

- [x] **Step 2: Remove subState assertions from test_supervisor_v2_orchestration**

In `test/test_supervisor_v2_orchestration/test_main.cpp`, find and remove these assertion blocks:

In `test_start_orchestration_sets_active_flag`:
```cpp
    TEST_ASSERT_EQUAL(static_cast<int>(SubState::PENDING),
                      static_cast<int>(S2V2Access::nextState(supervisor).subState));
```

In `test_check_response_completed_advances_observed_state`:
```cpp
    TEST_ASSERT_EQUAL(static_cast<int>(SubState::COMMITTED),
                      static_cast<int>(S2V2Access::nextState(supervisor).subState));
```

- [x] **Step 3: Remove subState assertion from test_supervisor_v2_remaining_paths**

In `test/test_supervisor_v2_remaining_paths/test_main.cpp`, find and remove:

```cpp
    TEST_ASSERT_EQUAL(static_cast<int>(SubState::PENDING),
                      static_cast<int>(S2V2Access::nextState(supervisor).subState));
```

- [x] **Step 4: Run tests**

```bash
pio test -e native
```
Expected: All tests pass. No references to `SubState` or `subState` remain in production or test code.

- [x] **Step 5: Commit**

```bash
git add src/supervisor/supervisor_v2.h \
        test/test_supervisor_v2_orchestration/test_main.cpp \
        test/test_supervisor_v2_remaining_paths/test_main.cpp
git commit -m "audit-fixes: remove unused SubState enum and subState field"
```

---

### Task 8: D8 — Add bounds check to registerComponent()

**Files:**
- Modify: `src/supervisor/supervisor_v2.cpp` — lines 57-62

- [x] **Step 1: Add configASSERT before array access**

In `src/supervisor/supervisor_v2.cpp`, change:

```cpp
void SupervisorV2::registerComponent(ComponentID id, ComponentMailbox* mailbox, bool isRequired) {
    // Store the mailbox pointer for cross-core writes. Null means absent.
    componentMailboxes_[static_cast<int>(id)] = mailbox;
    // Track required/optional for boot presence checks and failure handling.
    isRequired_[static_cast<int>(id)] = isRequired;
}
```

to:

```cpp
void SupervisorV2::registerComponent(ComponentID id, ComponentMailbox* mailbox, bool isRequired) {
    configASSERT(static_cast<size_t>(id) < componentCount);
    // Store the mailbox pointer for cross-core writes. Null means absent.
    componentMailboxes_[static_cast<size_t>(id)] = mailbox;
    // Track required/optional for boot presence checks and failure handling.
    isRequired_[static_cast<size_t>(id)] = isRequired;
}
```

`configASSERT` is a FreeRTOS macro that triggers on invalid input in debug builds and is compiled out in production. On native builds, the stub must exist.

- [x] **Step 2: Add configASSERT stub to native_stubs.h if missing**

In `src/supervisor/native_stubs.h`, check if `configASSERT` is defined:

```bash
grep "configASSERT" src/supervisor/native_stubs.h
```

If not present, add after the `#include <cstring>`:

```cpp
#ifndef configASSERT
#define configASSERT(x) ((void)0)
#endif
```

- [x] **Step 3: Run tests**

```bash
pio test -e native
```
Expected: All tests pass. `registerComponent` with valid IDs works. An invalid ID would trip the assertion in debug builds.

- [x] **Step 4: Commit**

```bash
git add src/supervisor/supervisor_v2.cpp src/supervisor/native_stubs.h
git commit -m "audit-fixes: add bounds assertion to registerComponent"
```

---

### Task 9: T3 — Settable tick counter in native stubs

**Context:** `xTaskGetTickCount()` always returns 0 in native stubs, making timeout-dependent tests impossible. Add a `nativeTickCount` global variable and expose it through `xTaskGetTickCount()`. Tests can set it to simulate time passing.

**Files:**
- Modify: `src/supervisor/native_stubs.h` — make tick count settable
- Modify: `test/test_fatal_task/test_main.cpp` — use settable tick count

- [x] **Step 1: Make tick count settable**

In `src/supervisor/native_stubs.h`, change:

```cpp
inline TickType_t xTaskGetTickCount() { return 0; }
```

to:

```cpp
inline TickType_t nativeTickCount = 0;
inline TickType_t xTaskGetTickCount() { return nativeTickCount; }
```

Also, `vTaskDelay` should simulate time passing. Update:

```cpp
inline void vTaskDelay(TickType_t ticks) {
    nativeTickCount += ticks;
}
```

- [x] **Step 2: Update fatal_task tests to use nativeTickCount**

In `test/test_fatal_task/test_main.cpp`, update tests:

```cpp
void test_fatal_task_sets_elapsed_flag() {
    SupervisorV2 supervisor;
    nativeTickCount = 0;
    // Simulate that we entered FATAL 60001 ticks ago (>= 60s)
    supervisor.fatalEnteredTicks_ = 0;
    nativeTickCount = 60001;

    fatalTask(&supervisor);

    TEST_ASSERT_TRUE(supervisor.fatalDeadlineElapsed_);
}

void test_fatal_task_no_elapsed_before_deadline() {
    SupervisorV2 supervisor;
    nativeTickCount = 0;
    supervisor.fatalEnteredTicks_ = 0;
    nativeTickCount = 100;  // Only 100ms passed, nowhere near 60s

    fatalTask(&supervisor);

    TEST_ASSERT_FALSE(supervisor.fatalDeadlineElapsed_);
}
```

- [x] **Step 3: Run tests**

```bash
pio test -e native -f test_fatal_task
```
Expected: PASS. The tick-based deadline checks work naturally without unsigned wraparound tricks.

- [ ] **Step 4: Run full suite — check for broken assumptions elsewhere**

```bash
pio test -e native
```
Expected: Some tests may fail if they relied on `xTaskGetTickCount()` always returning 0. If so, add `nativeTickCount = 0;` at the start of those tests or reset it in `setUp()`. The `vTaskDelay` change may also affect tests that call functions relying on delay to simulate time.

Fix any failures before committing.

- [x] **Step 5: Commit**

```bash
git add src/supervisor/native_stubs.h test/test_fatal_task/test_main.cpp
git commit -m "audit-fixes: add settable tick counter to native stubs"
```

---

### Task 10: T4 — Real assertions in registration tests

**Context:** 8 of 9 tests in `test_supervisor_v2_registration` are smoke tests (`TEST_ASSERT_TRUE_MESSAGE(true, "did not crash")`). Replace them with actual behavior assertions.

**Files:**
- Modify: `test/test_supervisor_v2_registration/test_main.cpp`

- [x] **Step 1: Rewrite smoke tests with real assertions**

Replace each smoke test:

`test_post_next_component_state_null_guard`:
```cpp
void test_post_next_component_state_null_guard() {
    SupervisorV2 supervisor;
    // postNextComponentState on unregistered component: should be a safe no-op
    supervisor.postNextComponentState(ComponentID::AudioRuntime);
    // No crash = pass. Additionally: verify no mailbox was touched.
    TEST_ASSERT_NULL(S2V2Access::getComponentMailbox(supervisor, ComponentID::AudioRuntime));
}
```

`test_register_component_null_mailbox_is_safe`:
```cpp
void test_register_component_null_mailbox_is_safe() {
    SupervisorV2 supervisor;
    supervisor.registerComponent(ComponentID::BoardInfo, nullptr, false);
    supervisor.postNextComponentState(ComponentID::BoardInfo);
    // Null mailbox means postNextComponentState is a no-op — no crash.
    TEST_ASSERT_NULL(S2V2Access::getComponentMailbox(supervisor, ComponentID::BoardInfo));
}
```

`test_complete_transition_completed_sets_event_bit`:
```cpp
void test_complete_transition_completed_sets_event_bit() {
    SupervisorV2 supervisor;
    TestComponent comp;
    supervisor.registerComponent(ComponentID::WiFi, &comp.mailbox, true);
    supervisor.setup();

    supervisor.completeTransition(ComponentID::WiFi, TransitionStatus::Completed);

    // Verify the event bit was set for the registered component
    EventBits_t bits = xEventGroupGetBits(supervisor.eventGroup_);
    TEST_ASSERT_TRUE(bits & (1 << static_cast<int>(ComponentID::WiFi)));
}
```

`test_complete_transition_failed_required_posts_error`:
```cpp
void test_complete_transition_failed_required_posts_error() {
    SupervisorV2 supervisor;
    TestComponent comp;
    supervisor.registerComponent(ComponentID::WiFi, &comp.mailbox, true);

    supervisor.completeTransition(ComponentID::WiFi, TransitionStatus::Failed);

    // Error event should be pending
    TEST_ASSERT_TRUE(S2V2Access::errorEvent(supervisor).pending);
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentID::WiFi),
                      static_cast<int>(S2V2Access::errorEvent(supervisor).source));
}
```

`test_complete_transition_failed_optional_is_degraded`:
```cpp
void test_complete_transition_failed_optional_is_degraded() {
    SupervisorV2 supervisor;
    TestComponent comp;
    supervisor.registerComponent(ComponentID::CLI, &comp.mailbox, false);

    supervisor.completeTransition(ComponentID::CLI, TransitionStatus::Failed);

    // Optional component should be marked DEGRADED (not FAILED)
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::DEGRADED),
                      static_cast<int>(S2V2Access::getComponentStatus(supervisor, ComponentID::CLI)));
    // No error event for optional failures
    TEST_ASSERT_FALSE(S2V2Access::errorEvent(supervisor).pending);
}
```

`test_boot_presence_passes_when_all_required_registered`:
```cpp
void test_boot_presence_passes_when_all_required_registered() {
    SupervisorV2 supervisor;
    TestComponent board, wifi, audio, cli;
    supervisor.registerComponent(ComponentID::BoardInfo, &board.mailbox, true);
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);

    supervisor.checkComponentPresence();

    // No error event should be pending (all required components present)
    TEST_ASSERT_FALSE(S2V2Access::errorEvent(supervisor).pending);
}
```

`test_boot_presence_detects_missing_required`:
```cpp
void test_boot_presence_detects_missing_required() {
    SupervisorV2 supervisor;
    TestComponent board, audio, cli;
    supervisor.registerComponent(ComponentID::BoardInfo, &board.mailbox, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);

    supervisor.checkComponentPresence();

    // Error event should be pending — WiFi is required but not registered
    TEST_ASSERT_TRUE(S2V2Access::errorEvent(supervisor).pending);
    TEST_ASSERT_EQUAL(static_cast<int>(ComponentID::WiFi),
                      static_cast<int>(S2V2Access::errorEvent(supervisor).source));
}
```

`test_boot_presence_ignores_missing_optional`:
```cpp
void test_boot_presence_ignores_missing_optional() {
    SupervisorV2 supervisor;
    TestComponent board, wifi, audio;
    supervisor.registerComponent(ComponentID::BoardInfo, &board.mailbox, true);
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);

    supervisor.checkComponentPresence();

    // No error event — CLI is optional, its absence is not an error
    TEST_ASSERT_FALSE(S2V2Access::errorEvent(supervisor).pending);
}
```

Also update the top of the file — add `S2V2Access` include and remove `#define private public`:

```cpp
#include <unity.h>

#include "support/s2v2_access.h"
#include "../../src/supervisor/supervisor_v2.cpp"
#include "../../src/supervisor/orchestrator.cpp"
#include "../../src/supervisor/state_machine.cpp"
```

- [x] **Step 2: Run registration tests**

```bash
pio test -e native -f test_supervisor_v2_registration
```
Expected: All 9 tests pass with real assertions.

- [x] **Step 3: Run full suite**

```bash
pio test -e native
```
Expected: All tests pass.

- [x] **Step 4: Commit**

```bash
git add test/test_supervisor_v2_registration/test_main.cpp
git commit -m "audit-fixes: replace registration smoke tests with real assertions"
```

---

### Task 11: File user story for D1/D2 architecture refactor

**Files:**
- Create: `requirements/user-stories/open/US-XXXX-supervisor-refactor.md`

- [x] **Step 1: Read the user story template**

```bash
cat requirements/user-stories/_template.md
```

- [x] **Step 2: Find next available US number**

```bash
ls requirements/user-stories/open/ | sort | tail -5
```

Use the next number in sequence.

- [x] **Step 3: Write the user story**

Create `requirements/user-stories/open/US-00XX-supervisor-refactor.md` with this content:

```markdown
# US-00XX: Extract StateMachine and Orchestrator from SupervisorV2

**Status:** To Do
**Priority:** Low
**Story Points:** 5

## Description

`SupervisorV2` is a god class with 22 members and 25 methods, owning state
machine logic, orchestration coordination, mailbox I/O, and FreeRTOS management.
The files are split (`state_machine.cpp`, `orchestrator.cpp`) but all methods
belong to the same class — the split is cosmetic.

## Acceptance Criteria

- [ ] `StateMachine` class exists with pure transition logic (getNextState,
      setObservedState, setTargetState, stepTowardTarget, determineRecoveryTarget,
      consumeErrorEvent, consumeStateRequest) — no FreeRTOS dependencies
- [ ] `Orchestrator` class exists owning the worker task, event group, order/response
      mailboxes, component mailboxes, and orchestration lifecycle
- [ ] `SupervisorV2` composes `StateMachine` and `Orchestrator`, exposing the public
      API surface (postStateRequest, postErrorEvent, completeTransition, run, etc.)
- [ ] `state_machine.h` is a real header (not a 3-line forward)
- [ ] All 67+ tests pass
- [ ] `StateMachine` is fully unit-testable without FreeRTOS

## References

- Audit: `docs/superpowers/specs/2026-05-16-supervisor-v2-audit.md` D1, D2
```

- [x] **Step 4: Commit**

```bash
git add requirements/user-stories/open/US-00XX-supervisor-refactor.md
git commit -m "audit-fixes: file user story for SupervisorV2 refactor (D1/D2)"
```

---

### Task 12: Full test suite verification

- [x] **Step 1: Run all native tests**

```bash
pio test -e native
```
Expected: All tests pass. No regressions. Count should be ≥67 tests.

- [ ] **Step 2: Verify no `#define private public` remains**

```bash
grep -r "define private public" test/
```
Expected: No matches.

- [ ] **Step 3: Verify no `.cpp` includes in test files**

```bash
grep -r '#include.*\.\./.*\.cpp' test/
```
Expected: No matches (Task 5 should have removed them). If any remain from the `.cpp` → `.h` migration, remove them.

- [x] **Step 4: Build production to verify no compile errors on target**

```bash
pio run -e production
```
Expected: Build succeeds (warnings are OK, errors are not).

- [x] **Step 5: Final commit**

```bash
git add -A
git status
git commit -m "audit-fixes: final verification — all tests pass, production builds"
```
Only commit if there are untracked/stale files to clean up.

---

## Summary

| Task | Issue | Description |
|------|-------|-------------|
| 1 | C3 | Create FATAL task function (TDD) |
| 2 | C3 | Integrate FATAL task into run(), remove handleFatal() |
| 3 | T1 | Add S2V2Access test accessor infrastructure |
| 4 | T1 | Migrate all tests from `#define private public` |
| 5 | T2 | Compile .cpp separately, remove direct includes |
| 6 | D3 | Remove duplicate SYSTEM_STATE_X macro |
| 7 | D4 | Remove unused SubState enum and subState field |
| 8 | D8 | Bounds assertion in registerComponent() |
| 9 | T3 | Settable tick counter in native stubs |
| 10 | T4 | Real assertions in registration tests |
| 11 | D1/D2 | File user story for architecture refactor |
| 12 | — | Full test suite + production build verification |

### T5 — Concurrency testing (not addressed in this plan)

The audit's T5 finding (no multi-core / concurrency tests) is a known limitation of the native test environment. Testing cross-core scenarios (concurrent mailbox writes, spinlock contention, race conditions) requires either real FreeRTOS dual-core scheduling or a thread-based test framework — neither is available in the current PlatformIO native Unity setup. This is tracked as a future infrastructure improvement when a suitable test harness for concurrent FreeRTOS tasks is introduced.
