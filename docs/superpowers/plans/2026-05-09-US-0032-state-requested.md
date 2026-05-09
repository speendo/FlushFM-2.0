# US-0032: Add STATE_REQUESTED Event + Fix Build + Cleanup

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix production build, add generic `STATE_REQUESTED(targetState)` event type with Mailbox payload, then clean up dead code and logic bugs.

**Architecture:** Three sequential stages. Stage 1 fixes 4 compile-breaking bugs. Stage 2 adds `STATE_REQUESTED` to the X-macro `SYSTEM_EVENT_X` enum, adds a `SystemState targetState` field to the single-slot Mailbox, teaches `handleEvent()` to dispatch based on the payload, and adds tests. Stage 3 removes unused functions/includes, unifies duplicated code paths, and fixes the CLI play command double-posting bug.

**Tech Stack:** C++17, PlatformIO, Unity test framework, X-macro enums

---

## File Structure

| File | Stage 1 | Stage 2 | Stage 3 | Responsibility |
|---|---|---|---|---|
| `src/state_machine/supervisor.h` | - | modify | modify | Enum, Mailbox, handleEvent declaration |
| `src/state_machine/supervisor.cpp` | modify | modify | modify | setErrorEvent guard, handleEvent, processMailbox |
| `src/main.cpp` | modify | - | - | Remove `SystemEvent::BOOT` |
| `src/components/composition/system_components.cpp` | modify | - | - | Remove orphaned code |
| `src/components/cli/cli.cpp` | - | - | modify | Fix play double-post |
| `test/test_state_transition_flow/test_main.cpp` | - | modify | - | Add STATE_REQUESTED tests |
| `test/test_component_types/test_main.cpp` | - | modify | - | Add STATE_REQUESTED to enum test |
| `test/test_mailbox_contract/test_main.cpp` | - | - | - | No changes needed |
| `docs/event-contract-model.md` | - | modify | - | Update to reflect STATE_REQUESTED |

---

## Stage 1: Fix Production Build

### Task 1: Remove Orphaned Code in system_components.cpp

**Files:**
- Modify: `src/components/composition/system_components.cpp:239-245,381-388`

- [ ] **Step 1: Delete lines 239-245 (orphaned code after WiFiComponent::completePendingTransition)**

The file has two blocks of orphaned code that live outside any function body — leftover duplication from an earlier edit. Delete them.

Lines 238-245 currently read:
```
238: 
239:     const uint32_t transitionId = pendingTransitionId_;
240:     transitionPending_ = false;
241:     pendingTransitionId_ = 0;
242:     pendingStreamingTarget_ = false;
243: 
244:     (void)system_.reportCompletion(name(), transitionId, status, reason);
245: }
```

Replace lines 238-245 with just a blank line (the closing `}` of `WiFiComponent::completePendingTransition` is already at line 237):

```
}
```

The actual edit replaces the block from the blank line after `}` (line 238) through line 245 with a single blank line.

Similarly, lines 380-388:
```
380: 
381:     const uint32_t transitionId = pendingTransitionId_;
382:     transitionPending_ = false;
383:     pendingTransitionId_ = 0;
384:     pendingStreamingTarget_ = false;
385:     pendingErrorTarget_ = false;
386: 
387:     (void)system_.reportCompletion(name(), transitionId, status, reason);
388: }
```

Replace lines 380-388 with a single blank line.

- [ ] **Step 2: Verify the edit**

The file should flow: `WiFiComponent::completePendingTransition(...)` → blank line → `AudioRuntimeComponent::AudioRuntimeComponent(...)`. And `AudioRuntimeComponent::completePendingTransition(...)` → blank line → `CliComponent::CliComponent(...)`.

### Task 2: Remove SystemEvent::BOOT from main.cpp

**Files:**
- Modify: `src/main.cpp:51`

- [ ] **Step 1: Delete the BOOT postEvent line**

`BOOT` was removed from the `SystemEvent` enum in US-0035. Line 51 still references it:

```cpp
s_system.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE);
```

Delete this entire line. The boot sequence is initiated implicitly by `setup()` itself — no explicit BOOT event is needed.

- [ ] **Step 2: Delete the unused `SystemReason::BOOT_SEQUENCE`**

After removing the only caller, `SystemReason::BOOT_SEQUENCE` is unused. Remove from the X-macro in `src/state_machine/supervisor.h`:

Current (lines 77-86):
```cpp
#define SYSTEM_REASON_X(V) \
    V(NONE) \
    V(BOOT_SEQUENCE) \
    V(COMPONENT_SETUP) \
    V(WIFI_INITIALIZED) \
    V(AUDIO_TASK_STARTED) \
    V(AUDIO_TASK_INIT_FAILED) \
    V(USER_REQUEST) \
    V(RECOVERY) \
    V(POWER_POLICY)
```

New:
```cpp
#define SYSTEM_REASON_X(V) \
    V(NONE) \
    V(COMPONENT_SETUP) \
    V(WIFI_INITIALIZED) \
    V(AUDIO_TASK_STARTED) \
    V(AUDIO_TASK_INIT_FAILED) \
    V(USER_REQUEST) \
    V(RECOVERY) \
    V(POWER_POLICY)
```

### Task 3: Move setErrorEvent Out of Native-Only Guard

**Files:**
- Modify: `src/state_machine/supervisor.cpp:53-76`

- [ ] **Step 1: Restructure the native-only guard block**

Currently `setErrorEvent` (line 60-66) is inside `#if !defined(ARDUINO)` (lines 53-76), but it's called from production code in `system_components.cpp` (lines 221, 268, 356, 362). On ARDUINO this would cause a linker error.

The original block:
```cpp
#if !defined(ARDUINO)
void Supervisor::postEventBuffered(SystemEvent event, SystemReason reason) {
    mailbox_.reason = reason;
    mailbox_.event = event;
    mailbox_.pending = true;
}

void Supervisor::setErrorEvent(DebugReason reason, ComponentID source) {
    if (!errorEvent_.pending) {
        errorEvent_.pending = true;
        errorEvent_.reason = reason;
        errorEvent_.source = source;
    }
}

void Supervisor::triggerFatal() {
    transitionTo(SystemState::FATAL, static_cast<SystemEvent>(0), SystemReason::NONE);
}

uint32_t Supervisor::getPendingTimeout(ComponentID id) const {
    if (id == ComponentID::Count) return 0;
    return pendingTransitions_[static_cast<size_t>(id)].timeoutMs;
}
#endif
```

Restructure to:
```cpp
void Supervisor::setErrorEvent(DebugReason reason, ComponentID source) {
    if (!errorEvent_.pending) {
        errorEvent_.pending = true;
        errorEvent_.reason = reason;
        errorEvent_.source = source;
    }
}

#if !defined(ARDUINO)
void Supervisor::postEventBuffered(SystemEvent event, SystemReason reason) {
    mailbox_.reason = reason;
    mailbox_.event = event;
    mailbox_.pending = true;
}

void Supervisor::triggerFatal() {
    transitionTo(SystemState::FATAL, static_cast<SystemEvent>(0), SystemReason::NONE);
}

uint32_t Supervisor::getPendingTimeout(ComponentID id) const {
    if (id == ComponentID::Count) return 0;
    return pendingTransitions_[static_cast<size_t>(id)].timeoutMs;
}
#endif
```

`setErrorEvent` moves out. `postEventBuffered`, `triggerFatal`, `getPendingTimeout` stay inside (they are test-only).

### Task 4: Verify Production Build

**Files:**
- None (verification only)

- [ ] **Step 1: Build production**

Run: `~/.platformio/penv/bin/platformio run -e production`
Expected: SUCCESS with no errors.

- [ ] **Step 2: Run native tests**

Run: `~/.platformio/penv/bin/platformio test -e native`
Expected: All current tests pass. 4 legacy tests will still ERROR (this is expected, they use the old `SystemController` API and are out of scope for this plan).

---

## Stage 2: Implement US-0032 — STATE_REQUESTED Event

### Task 5: Add STATE_REQUESTED to SystemEvent Enum

**Files:**
- Modify: `src/state_machine/supervisor.h:53-58`

- [ ] **Step 1: Add STATE_REQUESTED to the X-macro list**

Current:
```cpp
#define SYSTEM_EVENT_X(V) \
    V(COMPONENT_SETUP_FAILED) \
    V(PLAY_REQUESTED) \
    V(STOP_REQUESTED) \
    V(RECOVER) \
    V(ENTER_SLEEP)
```

New:
```cpp
#define SYSTEM_EVENT_X(V) \
    V(COMPONENT_SETUP_FAILED) \
    V(PLAY_REQUESTED) \
    V(STOP_REQUESTED) \
    V(RECOVER) \
    V(ENTER_SLEEP) \
    V(STATE_REQUESTED)
```

`toString(SystemEvent)` automatically picks up the new value from the X-macro — no manual switch-case change needed.

### Task 6: Add targetState Payload to Mailbox

**Files:**
- Modify: `src/state_machine/supervisor.h:200-204`
- Modify: `src/state_machine/supervisor.cpp:41-51,54-58`

- [ ] **Step 1: Add targetState field to Mailbox struct**

Current:
```cpp
struct Mailbox {
    SystemEvent event = static_cast<SystemEvent>(0);
    SystemReason reason = SystemReason::NONE;
    bool pending = false;
};
```

New:
```cpp
struct Mailbox {
    SystemEvent event = static_cast<SystemEvent>(0);
    SystemReason reason = SystemReason::NONE;
    SystemState targetState = SystemState::BOOTING;
    bool pending = false;
};
```

- [ ] **Step 2: Update postEvent to accept optional target state**

Add an overloaded `postEvent` that carries a target state. The existing two-parameter `postEvent` remains unchanged for backward compatibility.

In `src/state_machine/supervisor.h`, after line 143 (`bool postEvent(SystemEvent event, SystemReason reason);`), add:

```cpp
bool postEvent(SystemEvent event, SystemReason reason, SystemState target);
```

In `src/state_machine/supervisor.cpp`, after the existing `postEvent` implementation (line 51), add:

```cpp
bool Supervisor::postEvent(SystemEvent event, SystemReason reason, SystemState target) {
#if !defined(ARDUINO)
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

Also update the test-only `postEventBuffered` in `supervisor.h` (line 157) and `supervisor.cpp` (lines 54-58) to support the overload:

In `supervisor.h`, after line 157:
```cpp
void postEventBuffered(SystemEvent event, SystemReason reason, SystemState target);
```

In `supervisor.cpp`:
```cpp
void Supervisor::postEventBuffered(SystemEvent event, SystemReason reason, SystemState target) {
    mailbox_.reason = reason;
    mailbox_.event = event;
    mailbox_.targetState = target;
    mailbox_.pending = true;
}
```

### Task 7: Update handleEvent to Process STATE_REQUESTED

**Files:**
- Modify: `src/state_machine/supervisor.cpp:452-536`

- [ ] **Step 1: Add STATE_REQUESTED handler before the state-independent user intents block**

The handler reads `mailbox_.targetState` and dispatches to the existing logic for each target. Insert after the `requestStateTransition` lambda definition (after line 469) and before the ENTER_SLEEP handler (before line 472):

```cpp
    if (event == SystemEvent::STATE_REQUESTED) {
        const SystemState target = mailbox_.targetState;
        switch (target) {
            case SystemState::SLEEP:
                targetMode_ = SystemState::SLEEP;
                requestStateTransition(SystemState::SLEEP);
                return;
            case SystemState::READY:
                targetMode_ = SystemState::SLEEP;
                if (observedState_ == SystemState::CONNECTING) {
                    return;
                }
                requestStateTransition(SystemState::READY);
                return;
            case SystemState::LIVE:
                if (observedState_ == SystemState::CONNECTING) {
                    targetMode_ = SystemState::LIVE;
                    return;
                }
                if (observedState_ == SystemState::SLEEP) {
                    targetMode_ = SystemState::LIVE;
                    transitionTo(SystemState::CONNECTING, event, reason);
                    requestStateTransition(SystemState::READY);
                    return;
                }
                if (observedState_ == SystemState::LIVE) {
                    targetMode_ = SystemState::LIVE;
                    requestStateTransition(SystemState::READY);
                    return;
                }
                requestStateTransition(SystemState::LIVE);
                return;
            case SystemState::ERROR:
                transitionTo(observedState_ == SystemState::ERROR ? SystemState::FATAL : SystemState::ERROR, event, reason);
                return;
            case SystemState::FATAL:
                targetMode_ = SystemState::FATAL;
                transitionTo(SystemState::FATAL, event, reason);
                return;
            case SystemState::BOOTING:
            case SystemState::CONNECTING:
                return;
        }
        return;
    }
```

This mirrors the existing handlers for `ENTER_SLEEP`, `STOP_REQUESTED`, `PLAY_REQUESTED`, and `COMPONENT_SETUP_FAILED`, but driven by the `target` payload. Transient states `BOOTING` and `CONNECTING` are not valid targets and are silently ignored.

### Task 8: Update processMailbox to Read targetState

**Files:**
- Modify: `src/state_machine/supervisor.cpp:78-107`

The Mailbox read in `processMailbox()` currently reads only `event` and `reason`. The `targetState` is already in the struct — `handleEvent` reads it via `mailbox_.targetState`. Since the Mailbox read happens before `handleEvent` is called, the state is already correct. No change needed.

- [ ] **Step 1: Verify no change needed**

The read path is:
```cpp
if (mailbox_.pending) {
    SystemEvent event = mailbox_.event;
    SystemReason reason = mailbox_.reason;
    mailbox_.pending = false;
    handleEvent(event, reason);
}
```

`handleEvent` accesses `mailbox_.targetState` directly via the member — this works because the read happens before `pending` is cleared. No code change required.

### Task 9: Update Enum Tests for STATE_REQUESTED

**Files:**
- Modify: `test/test_component_types/test_main.cpp:77-83`

- [ ] **Step 1: Add STATE_REQUESTED to the test cases**

Current (lines 77-83):
```cpp
const std::array<EnumLabelCase<SystemEvent>, 5> events = {{
    {SystemEvent::COMPONENT_SETUP_FAILED, "COMPONENT_SETUP_FAILED"},
    {SystemEvent::PLAY_REQUESTED, "PLAY_REQUESTED"},
    {SystemEvent::STOP_REQUESTED, "STOP_REQUESTED"},
    {SystemEvent::RECOVER, "RECOVER"},
    {SystemEvent::ENTER_SLEEP, "ENTER_SLEEP"},
}};
```

New:
```cpp
const std::array<EnumLabelCase<SystemEvent>, 6> events = {{
    {SystemEvent::COMPONENT_SETUP_FAILED, "COMPONENT_SETUP_FAILED"},
    {SystemEvent::PLAY_REQUESTED, "PLAY_REQUESTED"},
    {SystemEvent::STOP_REQUESTED, "STOP_REQUESTED"},
    {SystemEvent::RECOVER, "RECOVER"},
    {SystemEvent::ENTER_SLEEP, "ENTER_SLEEP"},
    {SystemEvent::STATE_REQUESTED, "STATE_REQUESTED"},
}};
```

- [ ] **Step 2: Run tests to verify**

Run: `~/.platformio/penv/bin/platformio test -e native -f test_component_types`
Expected: All 11 tests PASS.

### Task 10: Add STATE_REQUESTED Transition Tests

**Files:**
- Modify: `test/test_state_transition_flow/test_main.cpp`

- [ ] **Step 1: Add test_state_requested_sleep**

```cpp
void test_state_requested_sleep() {
    TransitionHooksFixture fixture;
    fixture.install();
    fixture.reachSleep();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    fixture.completeAllActive();
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::SLEEP));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));
}
```

- [ ] **Step 2: Add test_state_requested_live_from_sleep**

```cpp
void test_state_requested_live_from_sleep() {
    TransitionHooksFixture fixture;
    fixture.install();
    fixture.reachSleep();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING), static_cast<int>(fixture.controller.state()));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));
}
```

- [ ] **Step 3: Add test_state_requested_ready**

```cpp
void test_state_requested_ready() {
    TransitionHooksFixture fixture;
    fixture.install();
    fixture.reachSleep();
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    fixture.completeAllActive();
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::READY));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));
}
```

- [ ] **Step 4: Add test_state_requested_error**

```cpp
void test_state_requested_error() {
    TransitionHooksFixture fixture;
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::ERROR));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(fixture.controller.state()));
}
```

- [ ] **Step 5: Add test_state_requested_fatal**

```cpp
void test_state_requested_fatal() {
    TransitionHooksFixture fixture;
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::FATAL));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL), static_cast<int>(fixture.controller.state()));
}
```

- [ ] **Step 6: Add test_state_requested_booting_ignored**

```cpp
void test_state_requested_booting_ignored() {
    Supervisor controller;
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::BOOTING), static_cast<int>(controller.state()));
    controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::BOOTING);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::BOOTING), static_cast<int>(controller.state()));
}
```

- [ ] **Step 7: Add test_state_requested_deferred_in_connecting**

```cpp
void test_state_requested_deferred_in_connecting() {
    TransitionHooksFixture fixture;
    fixture.install();
    fixture.reachSleep();
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING), static_cast<int>(fixture.controller.state()));

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::STATE_REQUESTED, SystemReason::USER_REQUEST, SystemState::LIVE));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING), static_cast<int>(fixture.controller.state()));
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::LIVE), static_cast<int>(fixture.controller.state()));
}
```

- [ ] **Step 8: Register new tests in main()**

In `test_main.cpp`, after line 328 (`RUN_TEST(test_matrix_forward_timeout);`), add:

```cpp
RUN_TEST(test_state_requested_sleep);
RUN_TEST(test_state_requested_live_from_sleep);
RUN_TEST(test_state_requested_ready);
RUN_TEST(test_state_requested_error);
RUN_TEST(test_state_requested_fatal);
RUN_TEST(test_state_requested_booting_ignored);
RUN_TEST(test_state_requested_deferred_in_connecting);
```

- [ ] **Step 9: Run tests to verify**

Run: `~/.platformio/penv/bin/platformio test -e native -f test_state_transition_flow`
Expected: All 23 tests (16 existing + 7 new) PASS.

### Task 11: Run Full Test Suite

**Files:**
- None (verification only)

- [ ] **Step 1: Run all native tests**

Run: `~/.platformio/penv/bin/platformio test -e native`
Expected: ~72 test cases, all new and existing tests PASS. The 4 legacy tests (`test_transition_arbitration`, `test_orchestration_completion_policy`, `test_component_registry`, `test_transition_timeout_watchdog`) will still ERROR — these are out of scope.

- [ ] **Step 2: Verify production build**

Run: `~/.platformio/penv/bin/platformio run -e production`
Expected: SUCCESS.

### Task 12: Update Event Contract Documentation

**Files:**
- Modify: `docs/event-contract-model.md`

- [ ] **Step 1: Update the note about STATE_REQUESTED implementation status**

In `docs/event-contract-model.md`, line 57, replace:
```
> **Note:** `STATE_REQUESTED` / `STATE_ENTERED` / `STATE_FAILED` is the contract model. The current implementation uses specific event names (`PLAY_REQUESTED`, `STOP_REQUESTED`, `ENTER_SLEEP`) directly. A future story (US-0032) adds a generic `STATE_REQUESTED` event type.
```

With:
```
> **Note:** `STATE_REQUESTED` / `STATE_ENTERED` / `STATE_FAILED` is the contract model. `STATE_REQUESTED(targetState)` is implemented as a `SystemEvent` enum value with a `SystemState` payload in the Mailbox. Legacy specific event names (`PLAY_REQUESTED`, `STOP_REQUESTED`, `ENTER_SLEEP`) coexist as concrete aliases for backward compatibility.
```

- [ ] **Step 2: Add STATE_REQUESTED to the naming standards table**

In the table at line 119-127, add after the `STATE_REQUESTED` row:
```
| Three-param postEvent | `postEvent(event, reason, target)` | Payload overload for STATE_REQUESTED |
```

- [ ] **Step 3: Update the event types section (line 59-69)**

After the existing `STATE_REQUESTED` paragraph (line 68), add:

```
`STATE_REQUESTED` is now a first-class `SystemEvent` enum value. The Mailbox carries a `targetState` payload (`SystemState`) that `handleEvent()` reads to dispatch the appropriate transition. Callers use `postEvent(SystemEvent::STATE_REQUESTED, reason, targetState)`.
```

---

## Stage 3: Code Cleanup

### Task 13: Fix CLI Play Command Double-Posting Bug

**Files:**
- Modify: `src/components/cli/cli.cpp:247-251`

- [ ] **Step 1: Remove the erroneous STOP_REQUESTED post**

Current (lines 247-251):
```cpp
if (strcmp(cmd, "play") == 0 && result.key == cli_output::MessageKey::CONNECTING_STREAM) {
    (void)s_controller->postEvent(SystemEvent::PLAY_REQUESTED,
                                    SystemReason::USER_REQUEST);
    (void)s_controller->postEvent(SystemEvent::STOP_REQUESTED,
                                    SystemReason::USER_REQUEST);
```

The `play` command should only post `PLAY_REQUESTED`. The `STOP_REQUESTED` is a copy-paste error.

New:
```cpp
if (strcmp(cmd, "play") == 0 && result.key == cli_output::MessageKey::CONNECTING_STREAM) {
    (void)s_controller->postEvent(SystemEvent::PLAY_REQUESTED,
                                    SystemReason::USER_REQUEST);
```

### Task 14: Remove Unused isBelowState / isAtLeastState Functions

**Files:**
- Modify: `src/state_machine/supervisor.h:33-39`

- [ ] **Step 1: Delete dead functions**

US-0031a confirmed these have zero callers. They are dead code.

Delete lines 33-39:
```cpp
constexpr bool isBelowState(SystemState lhs, SystemState rhs) {
    return stateRank(lhs) < stateRank(rhs);
}

constexpr bool isAtLeastState(SystemState lhs, SystemState rhs) {
    return stateRank(lhs) >= stateRank(rhs);
}
```

### Task 15: Remove Unused #include <esp_system.h>

**Files:**
- Modify: `src/state_machine/supervisor.cpp:8`

- [ ] **Step 1: Delete the include**

`esp_system.h` provides functions like `esp_restart()`, `esp_get_free_heap_size()`, etc. The only platform function used in `supervisor.cpp` is `millis()`, which comes from `Arduino.h` (transitively via `core/debug.h`).

Delete line 8:
```cpp
#include <esp_system.h>
```

### Task 16: Unify processMailbox Duplicated Code

**Files:**
- Modify: `src/state_machine/supervisor.cpp:78-107`

- [ ] **Step 1: Replace the duplicated native/ARDUINO paths with a single path**

Current `processMailbox()` has identical code under `#if !defined(ARDUINO)` and `#else` (lines 78-107). The only difference is the native path has a spurious `return;` (line 92).

Replace the entire function body with the unified version:
```cpp
void Supervisor::processMailbox() {
    if (observedState_ == SystemState::FATAL) return;
    if (mailbox_.pending) {
        SystemEvent event = mailbox_.event;
        SystemReason reason = mailbox_.reason;
        mailbox_.pending = false;
        handleEvent(event, reason);
    }
    if (errorEvent_.pending) {
        errorEvent_.pending = false;
        transitionTo(SystemState::ERROR, SystemEvent::COMPONENT_SETUP_FAILED, SystemReason::RECOVERY);
    }
    checkTransitionTimeouts();
}
```

Remove the `#if !defined(ARDUINO)` / `#else` / `#endif` guards — the single code path works for both platforms.

### Task 17: Verify Clean Build and Tests

**Files:**
- None (verification only)

- [ ] **Step 1: Run all native tests**

Run: `~/.platformio/penv/bin/platformio test -e native`
Expected: Same pass/fail counts as Task 11. No regressions.

- [ ] **Step 2: Verify production build**

Run: `~/.platformio/penv/bin/platformio run -e production`
Expected: SUCCESS.

---

## Self-Review

**1. Spec coverage (US-0032 acceptance criteria):**
- [x] `STATE_REQUESTED` added to SystemEvent enum — Task 5
- [x] Carries target `SystemState` as payload — Task 6 (Mailbox.targetState)
- [x] Event contract documented: external = STATE_REQUESTED, internal = STATE_ENTERED/STATE_FAILED — Task 12
- [x] Mailbox updated to carry state payload — Task 6
- [x] Existing state-request events reviewed for consolidation — AC says optional, handled via coexistence in handleEvent
- [x] handleEvent() processes STATE_REQUESTED with target-state matching — Task 7
- [x] Backward compatibility maintained — Task 7 (existing handlers unchanged)
- [x] Component event emissions remain unchanged — No component code changed
- [x] Tests validate STATE_REQUESTED routing — Task 10 (7 new tests)
- [x] Debug and production builds succeed — Tasks 4, 11, 17
- [x] All native unit tests pass — Tasks 4, 11, 17

**2. Placeholder scan:** No TBD, TODO, or placeholder patterns found.

**3. Type consistency:**
- `SystemState` used consistently for payload type
- `mailbox_.targetState` naming consistent across Tasks 6, 7, 8
- `postEvent(event, reason, target)` signature consistent across .h and .cpp
- Test constants `kTestMatrix`, `TransitionHooksFixture` reused from existing test file
