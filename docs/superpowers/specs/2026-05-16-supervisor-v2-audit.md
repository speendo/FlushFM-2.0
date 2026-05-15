# Supervisor V2 Code Audit

**Date:** 2026-05-16
**Scope:** `SupervisorV2`, state machine (`state_machine.cpp`), orchestrator (`orchestrator.cpp`), supporting files (`supervisor_v2.h`, `supervisor_v2.cpp`, `native_stubs.h`, `component_types.h`, `main.cpp`), and all 7 test suites.
**Type:** Architecture & correctness review.

---

## Critical Bugs

### C1. Event group cleared after mailbox writes (race condition)

**File:** `orchestrator.cpp:65-91` — `startOrchestration()`

```cpp
postNextComponentState(id);              // line 81 — writes component mailboxes
...
xEventGroupClearBits(..., kAllComponentBits);  // line 84 — THEN clears event group
```

The sequence writes component mailboxes **before** clearing the event group. A component on the other core can:
1. Read its mailbox (new target state)
2. Complete its transition
3. Call `completeTransition()` → `xEventGroupSetBits()`

...all before line 84 executes. Then `xEventGroupClearBits()` wipes that bit. The component's work is lost, the orchestration waits forever for a bit that was already set, and the timeout path eventually catches it — but only after the full timeout elapses.

**Fix:** Move `xEventGroupClearBits()` to **before** the mailbox-write loop (line 84 before line 74).

---

### C2. Unsigned wraparound in timeout calculation

**File:** `orchestrator.cpp:144-145`

```cpp
TickType_t now = xTaskGetTickCount();
TickType_t waitTicks = pdMS_TO_TICKS(supervisor->orderMailbox_.deadlineMs - now);
```

Both `deadlineMs` and `now` are `TickType_t` (unsigned 32-bit). If `now > deadlineMs` (tight scheduling, delayed wake, or a very short timeout), `deadlineMs - now` wraps to `UINT32_MAX - delta` ≈ 49.7 days. The worker blocks for 49.7 days instead of timing out immediately.

**Fix:** Clamp to zero: `(deadlineMs > now) ? deadlineMs - now : 0`.

---

### C3. FATAL dwell timer never fires on quiet system

**File:** `state_machine.cpp:110-131, 228-241`

`run()` blocks on `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)` — it only executes when notified. In FATAL state:
- `handleFatal()` arms a 60-second deadline on first call
- But subsequent calls only happen when the task is re-notified
- While in FATAL, no orchestration is active, so the worker never posts notifications
- External error/state events are the only wake sources — if none arrive, `handleFatal()` is never called again
- The 60-second dwell timer and `esp_deep_sleep_start()` never trigger on a quiet system

**Fix:** Use a periodic wake mechanism (timer, or `ulTaskNotifyTake()` with a finite timeout and periodic re-check).

---

## Architecture & Design Issues

### D1. God class: `SupervisorV2` owns everything

**File:** `supervisor_v2.h`

The class has ~25 member variables and ~22 methods (public + private), owning:
- State transition logic (`getNextState`, `stepTowardTarget`, `setTargetState`, `setObservedState`)
- Mailbox I/O (`consumeStateRequest`, `consumeErrorEvent`, `postStateRequest`, `postErrorEvent`)
- Orchestration lifecycle (`startOrchestration`, `checkOrchestrationResponse`, `completeTransition`)
- Component lifecycle management (`registerComponent`, `checkComponentPresence`)
- Retry policy (`RetryPolicy`, `getMaxRecoveries`, `setMaxRecoveries`)
- Recovery planning (`determineRecoveryTarget`, `lastTargetBeforeError_`)
- Deep sleep management (`handleFatal`, `fatalDeadlineMs_`, `fatalDeadlineElapsed_`)
- Event group management (`eventGroup_`, `kAllComponentBits`)
- Timeout config (`TransitionTimeoutConfig`, `getTransitionTimeout`, `loadTransitionTimeoutConfig`)

The files are split (`state_machine.cpp`, `orchestrator.cpp`) but all methods land on the same class. The split is cosmetic — every method is `SupervisorV2::method()`.

**Recommendation:** Extract into separate classes:
- `StateMachine` — pure transition logic (no FreeRTOS deps)
- `Orchestrator` — worker task, event group, component coordination
- `SupervisorV2` — composition of the above, public API surface

---

### D2. Empty `state_machine.h`

**File:** `src/supervisor/state_machine.h` (3 lines)

```cpp
#pragma once
#include "supervisor/supervisor_v2.h"
```

This is not a separation — it's a forwarding header that exists because the directory structure expected a file here. If `SupervisorV2` were refactored into separate classes, `StateMachine` would get its own header.

---

### D3. Duplicate X-macro `SYSTEM_STATE_X`

**Files:** `component_types.h:20-27`, `supervisor_v2.h:17-26`

The `SYSTEM_STATE_X` macro is defined in both files. `supervisor_v2.h`'s copy is guarded by `#ifndef SYSTEM_STATE_X`:
- Include order ensures `component_types.h` (included via `orchestrator.h`) defines it first
- `supervisor_v2.h:17` sees it already defined and skips the block
- `supervisor_v2.h:66` `#undef`s it

The copy in `supervisor_v2.h` is dead code — never reached. A developer editing one will likely forget the other. Having the same enum definition in two files is a maintenance trap.

**Fix:** Remove the duplicate from `supervisor_v2.h`. The `detail` namespace already references `component_types.h`'s types.

---

### D4. `SubState::FAILED` declared but never used

**File:** `supervisor_v2.h:95-99`

```cpp
enum class SubState {
    PENDING,
    COMMITTED,
    FAILED     // ← never set anywhere in the codebase
};
```

`ActiveTransition.subState` is set to `PENDING` in `startOrchestration()` and `COMMITTED` in `checkOrchestrationResponse()` — but `FAILED` is never assigned. If this value is meant for future use, it should have a reference. Otherwise it's dead code.

---

### D5. `lastTargetBeforeError_` with stale TODO

**File:** `supervisor_v2.h:355-358`

```cpp
/** @brief Saved target for ERROR recovery placeholder.
 *  TODO: remove once determineRecoveryTarget() is replaced with real logic.
 */
SystemState lastTargetBeforeError_;
```

The TODO links to no issue, no plan, and no timeline. The placeholder is now the shipped implementation. Either remove the TODO (if the snapshot approach is the final design) or create a tracking issue.

---

### D6. Default state values both equal FATAL (rank 0)

**File:** `supervisor_v2.h:329-330`

```cpp
SystemState observedState_;  // default-initialized to 0 = FATAL
SystemState targetState_;    // default-initialized to 0 = FATAL
```

`SystemState::FATAL = 0` per the enum. Both start as FATAL. The system is parked in FATAL until something calls `setObservedState()` to move out. The `run()` method's FATAL guard (`if (observedState_ != SystemState::FATAL)`) skips event processing, so state requests sent before any initialization are silently dropped.

While `main.cpp` calls `SupervisorV2::setup()` which doesn't change these states, the system relies on external components posting state requests to leave FATAL. A more natural default would be `BOOTING` for `observedState_` and `SLEEP` for `targetState_`.

---

### D7. `DebugReason` is raw `const char*`

**File:** `component_types.h:123`

```cpp
using DebugReason = const char*;
```

Works fine with string literals (the common case), but one dynamically-allocated string or `std::string::c_str()` with a short lifetime away from a use-after-free. The type provides no safety guarantees and no way to distinguish between null, empty, and valid strings.

---

### D8. `registerComponent()` lacks bounds assertion

**File:** `supervisor_v2.cpp:54-58`

```cpp
componentMailboxes_[static_cast<int>(id)] = mailbox;
isRequired_[static_cast<int>(id)] = isRequired;
```

Both arrays are sized by `componentCount`, which equals `ComponentID::Count`. The `static_assert` in the header (line 84) ensures bitmask safety but doesn't prevent a runtime out-of-bounds if `id` is somehow invalid (e.g., corrupted RAM, logic bug). A `size_t` check or debug assertion would catch this.

---

### D9. Worker task polls instead of blocking

**File:** `orchestrator.cpp:136-138`

```cpp
if (!supervisor->orderMailbox_.consume()) {
    vTaskDelay(pdMS_TO_TICKS(10));  // 100 Hz polling
    continue;
}
```

The orchestration worker polls its mailbox every 10ms even when idle. On the ESP32-S3 (potentially battery-powered), this wastes CPU and energy. The worker should block on a task notification (like the state machine does) and be notified by `startOrchestration()`.

---

### D10. Worker reads order fields after releasing spinlock

**File:** `orchestrator.cpp:144-165`

After `orderMailbox_.consume()` returns (spinlock released), the worker reads `expectedBits`, `deadlineMs`, and `transitionTarget` without the lock. On same-core preemption (state machine has higher priority), the state machine could call `startOrchestration()` → `orderMailbox_.post()` and overwrite these fields while the worker is reading them.

This is mitigated in practice because `hasActiveOrchestration_` prevents the state machine from starting a new orchestration while one is in-flight. However, there's no formal guarantee — the fields should be copied out under spinlock.

---

### D11. Missing guard in `handleFatal()`

**File:** `state_machine.cpp:228-241`

On every call after the first (when `fatalEntered_` is true but the deadline hasn't elapsed), `handleFatal()` does nothing — no logging, no indication that the deadline check is pending. A developer debugging a hang would see no output for 60 seconds while the dwell timer runs. A brief log or counter would help.

---

## Test Quality Issues

### T1. `#define private public` in 6 of 7 test files

**Files:** All test files except `test_supervisor_v2_get_next_state`

Tests reach directly into private members (`observedState_`, `targetState_`, `hasActiveOrchestration_`, `retryPolicy_`, `errorEvent_`, `stateRequestMailbox_`, etc.). This means:
- Any internal refactor breaks test compilation
- Tests bypass the public API, so the API surface is undertested
- The one test file that doesn't use this pattern (`test_get_next_state`) proves it's possible to test the state machine through public functions

The tests that set up internal state manually (lines like `supervisor.observedState_ = SystemState::BOOTING`) are particularly fragile — they rely on specific member layouts and names.

---

### T2. Tests include `.cpp` files directly

**Files:** All 7 test files

```cpp
#include "../../src/supervisor/supervisor_v2.cpp"
#include "../../src/supervisor/orchestrator.cpp"
#include "../../src/supervisor/state_machine.cpp"
```

Each test `#include`s the `.cpp` source files directly, meaning:
- No separate compilation of source files
- No library linkage — tests can't compile against a static or shared lib
- The `#define private public` trick only works because of the include order
- Makes integration with a test runner or build system fragile

The `#include` approach combined with `#define private public` means tests are **compiled into the same translation unit** as the source, giving them access to everything. This is the root cause of the coupling in T1.

---

### T3. `xTaskGetTickCount()` always returns 0 in stubs

**File:** `native_stubs.h:47`

```cpp
inline TickType_t xTaskGetTickCount() { return 0; }
```

Time is frozen at zero, making timeout-dependent behavior untestable:
- `handleFatal()` timeout can never trigger naturally — the test works around it by manually setting `fatalDeadlineMs_ = 0`
- No test verifies that `handleFatal()` actually waits ~60s before deep sleep
- No test verifies timeout behavior of `xEventGroupWaitBits`
- The `checkOrchestrationResponse()` TIMED_OUT path is only exercised by manually posting a response, not by letting the deadline expire

A better approach would be a settable tick counter (`xTaskGetTickCount() = fakeNow`) or a mock that tests can advance.

---

### T4. Registration tests are "didn't crash" smoke tests

**File:** `test/test_supervisor_v2_registration/test_main.cpp`

6 of 9 tests use:
```cpp
TEST_ASSERT_TRUE_MESSAGE(true, "... did not crash");
```

These verify the absence of a segfault but don't verify:
- That the component mailbox was actually written
- That the event bit was set
- That the error event was populated correctly
- That the component status changed

Example: `test_complete_transition_completed_sets_event_bit` calls `completeTransition()` but never checks `xEventGroupGetBits()`. The test name says "sets event bit" but the assertion is "didn't crash."

---

### T5. No multi-core / concurrency tests

All 7 test suites run single-threaded via native stubs. None test:
- Concurrent mailbox writes from two cores
- Race between component completion and event group clear
- Notification delivery between state machine and worker tasks
- Spinlock contention under load

This means the cross-core synchronization code is entirely untested in realistic conditions.

---

## Summary

| Severity | Count | Key items |
|----------|-------|-----------|
| **Critical** | 3 | C1 (race), C2 (wraparound), C3 (dwell never fires) |
| **Design** | 11 | D1–D11 |
| **Test** | 5 | T1–T5 |
| **Total** | 19 | |

The architecture (split-task, mailbox protocol, event-group bits) and test coverage (67 tests) show care. But the 3 critical bugs are the kind that cause unreproducible field failures — random hangs on timeout (C1), deadlock-length waits from a simple timeout (C2), and radios that never shut down after fatal errors (C3). The `#define private public` pattern in 86% of tests means any significant refactor will require rewriting most of the test suite.

---

## Appendix: File Reference

| File | Lines | Role |
|------|-------|------|
| `src/supervisor/supervisor_v2.h` | 364 | Class definition, rank table, types |
| `src/supervisor/supervisor_v2.cpp` | 59 | Setup, config, registration |
| `src/supervisor/state_machine.cpp` | 241 | Transition logic, `run()` loop |
| `src/supervisor/state_machine.h` | 3 | Empty forwarding header |
| `src/supervisor/orchestrator.h` | 83 | Order/response mailbox types |
| `src/supervisor/orchestrator.cpp` | 175 | Worker task, orchestration lifecycle |
| `src/supervisor/native_stubs.h` | 50 | FreeRTOS stubs for native testing |
| `src/component_types.h` | 125 | Enum, mailbox, DebugReason |
| `src/main.cpp` | 87 | Setup, component wiring |
| `test/test_supervisor_v2_*` | 7 dirs | 67 test cases total |
