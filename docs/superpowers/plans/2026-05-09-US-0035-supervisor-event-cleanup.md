# Supervisor Event Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove component commands from the Supervisor's handleEvent, add an Error Event flag mechanism, and make supervisor.cpp lean — only global state transitions remain.

**Architecture:** Component commands (WIFI_READY, AUDIO_INIT_OK, etc.) route directly to components, not through the Supervisor. The orchestration system handles component readiness via COMMITTED/FAILED reports. Component errors use a single Error Event flag `{reason, source}` checked in processMailbox(), per state-management.md §"Error Events". The Supervisor shrinks to only system events (PLAY_REQUESTED, STOP_REQUESTED, ENTER_SLEEP, BOOT, COMPONENT_SETUP_FAILED, RECOVER).

**Tech Stack:** C++20, PlatformIO, Unity

---

### Task 1: Add Error Event flag structure and setter

**Files:**
- Modify: `src/state_machine/supervisor.h` (add struct + method declaration)
- Modify: `src/state_machine/supervisor.cpp` (add setter implementation)

Add to supervisor.h (near the Mailbox declaration, ~line 215):

```cpp
struct ErrorEvent {
    bool pending = false;
    DebugReason reason = nullptr;
    ComponentID source = ComponentID::Count;
};
```

Add public method declaration (near postEvent, ~line 149):

```cpp
void setErrorEvent(DebugReason reason, ComponentID source);
```

Add private member (near the private section headers):

```cpp
ErrorEvent errorEvent_{};
```

Add implementation in supervisor.cpp (after `postEventBuffered`, before `processMailbox`):

```cpp
void Supervisor::setErrorEvent(DebugReason reason, ComponentID source) {
    if (!errorEvent_.pending) {
        errorEvent_.pending = true;
        errorEvent_.reason = reason;
        errorEvent_.source = source;
    }
}
```

- [ ] **Step 1:** Add ErrorEvent struct and setErrorEvent declaration to supervisor.h
- [ ] **Step 2:** Add errorEvent_ member
- [ ] **Step 3:** Add setErrorEvent implementation to supervisor.cpp
- [ ] **Step 4:** Verify build: `pio run -e native`
- [ ] **Step 5:** Commit with message "US-0035: Add Error Event flag structure and setter"

### Task 2: Check Error Event in processMailbox

**Files:** Modify `src/state_machine/supervisor.cpp`

In `processMailbox()`, after the FATAL guard and after the Mailbox drain (or before the return), add Error Event check. For BOTH the `#if !defined(ARDUINO)` and `#else` branches:

```cpp
void Supervisor::processMailbox() {
#if !defined(ARDUINO)
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
    return;
#else
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
#endif
}
```

- [ ] **Step 1:** Add Error Event check to both processMailbox branches
- [ ] **Step 2:** Verify build: `pio run -e native`
- [ ] **Step 3:** Commit with message "US-0035: Check Error Event flag in processMailbox"

### Task 3: Remove component command handlers from handleEvent

**Files:** Modify `src/state_machine/supervisor.cpp`

Remove from handleEvent:

**3a. Remove SLEEP handler (lines ~508-517):**
```cpp
// REMOVE ENTIRE BLOCK:
        case SystemState::SLEEP:
            if (event == SystemEvent::WIFI_READY) {
                componentRegistry_[static_cast<size_t>(ComponentID::WiFi)].lifeCycleStatus = ComponentLifecycleStatus::Ready;
            } else if (event == SystemEvent::AUDIO_INIT_OK) {
                componentRegistry_[static_cast<size_t>(ComponentID::AudioRuntime)].lifeCycleStatus = ComponentLifecycleStatus::Ready;
            } else if (event == SystemEvent::WIFI_DISCONNECTED) {
                componentRegistry_[static_cast<size_t>(ComponentID::WiFi)].lifeCycleStatus = ComponentLifecycleStatus::Unknown;
            } else if (event == SystemEvent::AUDIO_INIT_FAILED) {
                componentRegistry_[static_cast<size_t>(ComponentID::AudioRuntime)].lifeCycleStatus = ComponentLifecycleStatus::Failed;
            }
            break;
```

Replace with empty case:
```cpp
        case SystemState::SLEEP:
            break;
```

**3b. Remove CONNECTING handler (lines ~520-535):**
```cpp
// REMOVE ENTIRE BLOCK:
        case SystemState::CONNECTING:
            if (event == SystemEvent::AUDIO_INIT_OK) { ... }
            ...
            break;
```

Replace with empty case:
```cpp
        case SystemState::CONNECTING:
            break;
```

**3c. Remove READY handler (lines ~538-545):**
```cpp
// REMOVE ENTIRE BLOCK:
        case SystemState::READY:
            if (event == SystemEvent::WIFI_DISCONNECTED) { ... }
            ...
            break;
```

Replace with empty case:
```cpp
        case SystemState::READY:
            break;
```

**3d. Remove LIVE handler (lines ~548-555):**
```cpp
// REMOVE ENTIRE BLOCK:
        case SystemState::LIVE:
            if (event == SystemEvent::WIFI_DISCONNECTED) { ... }
            ...
            break;
```

Replace with empty case:
```cpp
        case SystemState::LIVE:
            break;
```

**3e. Remove PLAY_REQUESTED quick path (lines ~482-484):**
```cpp
// REMOVE:
            if (getComponentStatus(ComponentID::WiFi) == ComponentLifecycleStatus::Ready &&
                getComponentStatus(ComponentID::AudioRuntime) == ComponentLifecycleStatus::Ready) {
                requestStateTransition(SystemState::READY);
            }
```

After `transitionTo(SystemState::CONNECTING, event, reason);`, always call `requestStateTransition(SystemState::READY);`:

```cpp
        if (observedState_ == SystemState::SLEEP) {
            targetMode_ = SystemState::LIVE;
            transitionTo(SystemState::CONNECTING, event, reason);
            requestStateTransition(SystemState::READY);
            return;
        }
```

**3f. Add COMPONENT_SETUP_FAILED as state-independent handler:**

After the PLAY_REQUESTED handler block, before the switch statement, add:

```cpp
    if (event == SystemEvent::COMPONENT_SETUP_FAILED) {
        transitionTo(SystemState::ERROR, event, reason);
        return;
    }
```

- [ ] **Step 1:** Replace SLEEP handler with empty case
- [ ] **Step 2:** Replace CONNECTING handler with empty case
- [ ] **Step 3:** Replace READY handler with empty case
- [ ] **Step 4:** Replace LIVE handler with empty case
- [ ] **Step 5:** Remove PLAY_REQUESTED quick path, always orchestrate
- [ ] **Step 6:** Add state-independent COMPONENT_SETUP_FAILED handler
- [ ] **Step 7:** Verify build: `pio run -e native`
- [ ] **Step 8:** Commit with message "US-0035: Remove component command handlers from handleEvent"

### Task 4: Remove unused SystemEvent enum values

**Files:** Modify `src/state_machine/supervisor.h`

Remove from `SYSTEM_EVENT_X`:
- `V(WIFI_READY)`
- `V(AUDIO_INIT_OK)`
- `V(AUDIO_INIT_FAILED)`
- `V(WIFI_DISCONNECTED)`
- `V(STREAM_LOST)`

Keep: BOOT, COMPONENT_SETUP_FAILED, PLAY_REQUESTED, STOP_REQUESTED, RECOVER, ENTER_SLEEP

New macro:
```cpp
#define SYSTEM_EVENT_X(V) \
    V(BOOT) \
    V(COMPONENT_SETUP_FAILED) \
    V(PLAY_REQUESTED) \
    V(STOP_REQUESTED) \
    V(RECOVER) \
    V(ENTER_SLEEP)
```

- [ ] **Step 1:** Edit SYSTEM_EVENT_X macro
- [ ] **Step 2:** Verify build — will fail until test/call-site updates in Tasks 5-6
- [ ] **Step 3:** Commit with message "US-0035: Remove unused SystemEvent enum values"

### Task 5: Update test_component_types round-trip tests

**Files:** Modify `test/test_component_types/test_main.cpp`

Update `events` array from 11 entries to 6:

```cpp
const std::array<EnumLabelCase<SystemEvent>, 6> events = {{
    {SystemEvent::BOOT, "BOOT"},
    {SystemEvent::COMPONENT_SETUP_FAILED, "COMPONENT_SETUP_FAILED"},
    {SystemEvent::PLAY_REQUESTED, "PLAY_REQUESTED"},
    {SystemEvent::STOP_REQUESTED, "STOP_REQUESTED"},
    {SystemEvent::RECOVER, "RECOVER"},
    {SystemEvent::ENTER_SLEEP, "ENTER_SLEEP"},
}};
```

Also update `STATE_FAILED` error list in error state transitions test (line ~322-326) — this test's `ENTER_SLEEP` assertion should still pass if the enum was cleaned up properly.

Update the invalid enum test (line ~117-118): change from `static_cast<SystemEvent>(255)` — now `toString()` returns "UNKNOWN" for removed values too (since the switch has no case for them). No change needed — 255 is still invalid.

- [ ] **Step 1:** Update events array from 11 to 6 entries
- [ ] **Step 2:** Verify build compiles this file
- [ ] **Step 3:** Commit with message "US-0035: Update test_component_types round-trip tests"

### Task 6: Update component call sites

**Files:** Modify `src/components/composition/system_components.cpp`

**6a. WiFiComponent::onConnected (line 211):**
Remove: `self->system_.postEvent(SystemEvent::WIFI_READY, SystemReason::WIFI_INITIALIZED);`

```cpp
void WiFiComponent::onConnected(void* context) {
    auto* self = static_cast<WiFiComponent*>(context);
    if (!self) return;
    if (self->transitionPending_ && self->pendingStreamingTarget_) {
        self->completePendingTransition(TransitionStatus::Completed, nullptr);
    }
    // No longer posts WIFI_READY — orchestration handles readiness via COMMITTED
}
```

**6b. WiFiComponent::onDisconnected (line 224):**
Replace: `self->system_.postEvent(SystemEvent::WIFI_DISCONNECTED, SystemReason::NONE);`

```cpp
void WiFiComponent::onDisconnected(void* context) {
    auto* self = static_cast<WiFiComponent*>(context);
    if (!self) return;
    if (self->transitionPending_ && self->pendingStreamingTarget_) {
        self->completePendingTransition(TransitionStatus::Failed, "wifi disconnected");
    } else {
        self->system_.setErrorEvent("wifi disconnected", ComponentID::WiFi);
    }
}
```

**6c. AudioRuntimeComponent::onAudioSignal INIT_OK (line 354):**
Remove: `self->system_.postEvent(SystemEvent::AUDIO_INIT_OK, SystemReason::AUDIO_TASK_STARTED);`

```cpp
    if (signal == audio_runtime::Signal::INIT_OK) {
        if (self->transitionPending_ && self->pendingStreamingTarget_) {
            self->completePendingTransition(TransitionStatus::Completed, nullptr);
        }
        // No longer posts AUDIO_INIT_OK — orchestration handles readiness via COMMITTED
    }
```

**6d. AudioRuntimeComponent::onAudioSignal STREAM_LOST (line 359):**
Replace: `self->system_.postEvent(SystemEvent::STREAM_LOST, SystemReason::NONE);`

```cpp
    } else if (signal == audio_runtime::Signal::STREAM_LOST) {
        if (self->transitionPending_ && self->pendingStreamingTarget_) {
            self->completePendingTransition(TransitionStatus::Failed, "stream lost");
        } else {
            self->system_.setErrorEvent("stream lost", ComponentID::AudioRuntime);
        }
    }
```

**6e. AudioRuntimeComponent::onAudioSignal else (line 364):**
Replace: `self->system_.postEvent(SystemEvent::AUDIO_INIT_FAILED, SystemReason::AUDIO_TASK_INIT_FAILED);`

```cpp
    } else {
        if (self->transitionPending_ && self->pendingStreamingTarget_) {
            self->completePendingTransition(TransitionStatus::Failed, "audio init failed");
        } else {
            self->system_.setErrorEvent("audio init failed", ComponentID::AudioRuntime);
        }
    }
```

**6f. debug_cli.cpp (line 68):**
Replace: `(void)s_controller->postEvent(SystemEvent::AUDIO_INIT_FAILED, SystemReason::USER_REQUEST);`

```cpp
    if (strcmp(targetState, "error") == 0) {
        (void)s_controller->postEvent(SystemEvent::COMPONENT_SETUP_FAILED, SystemReason::USER_REQUEST);
        PROD_LOG(kLogSource, "Transition request posted: error");
        return true;
    }
```

- [ ] **Step 1:** Update WiFi::onConnected
- [ ] **Step 2:** Update WiFi::onDisconnected
- [ ] **Step 3:** Update Audio::INIT_OK
- [ ] **Step 4:** Update Audio::STREAM_LOST
- [ ] **Step 5:** Update Audio::else
- [ ] **Step 6:** Update debug_cli.cpp
- [ ] **Step 7:** Verify build: `pio run -e native` (should now compile)
- [ ] **Step 8:** Commit with message "US-0035: Update component call sites to use Error Event flag"

### Task 7: Rewrite test_state_transition_flow tests

**Files:** Modify `test/test_state_transition_flow/test_main.cpp`

The following tests **post WIFI_READY/AUDIO_INIT_OK** to trigger CONNECTING→READY transitions via the removed handlers. These need to be rewritten to use `completeAllActive()` instead:

**Tests to remove (CONNECTING handler behavior no longer exists):**
- `test_connecting_waits_for_both_ready_signals_audio_first` — posts AUDIO_INIT_OK then WIFI_READY to trigger READY
- `test_connecting_waits_for_both_ready_signals_wifi_first` — same, reversed order

These tests verified the CONNECTING handler's "wait for both" logic. After cleanup, CONNECTING always goes through orchestration — components are armed, they report COMMITTED when ready. Replace with a test that verifies orchestration completes to READY after completeAllActive:

```cpp
void test_connecting_advances_to_ready_after_orchestration_completes() {
    TransitionHooksFixture fixture;
    fixture.install();
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    // PLAY_REQUESTED from SLEEP → CONNECTING → requestStateTransition(READY) → orchestration active
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING), static_cast<int>(fixture.controller.state()));
    TEST_ASSERT_TRUE(fixture.controller.isOrchestrationActive());
    fixture.completeAllActive();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(fixture.controller.state()));
}
```

**Tests that post WIFI_READY/AUDIO_INIT_OK in CONNECTING context — replace with completeAllActive:**

All of these tests currently post WIFI_READY and AUDIO_INIT_OK to trigger CONNECTING→READY:
- `test_stop_requested_during_connecting_cancels_deferred_play` (line 123-135)
- `test_play_requested_during_connecting_is_deferred_until_ready` (line 76-98)
- `test_play_requested_requires_orchestration_completion_before_streaming` (line 168-200)
- `test_stop_requested_requires_orchestration_completion_before_idle` (line 202-224)
- `test_play_requested_while_streaming_restarts_after_idle_transition` (line 226-252)
- `test_enter_sleep_requires_orchestration_completion_before_sleep` (line 254-276)
- `test_play_requested_while_sleep_wakes_to_connecting_then_streaming` (line 278-318)
- `test_error_state_transitions` (line 320-348)
- `test_ready_wifi_disconnect_triggers_error` (line 350-361)
- `test_ready_setup_failure_triggers_error` (line 363-374)
- `test_live_wifi_disconnect_triggers_error` (line 376-390)
- `test_live_stream_lost_triggers_error` (line 392-402)
- `test_optional_component_failure_does_not_block_orchestration` (line 434-456)
- `test_observed_state_lags_until_orchestration_confirms` (line 458-473)
- `test_matrix_forward_timeout_from_sleep_to_ready` (line 507-521)

Each of these posts `WIFI_READY` and/or `AUDIO_INIT_OK` at some point. After cleanup, these event names no longer compile. These tests need to be rewritten.

**Strategy:** For each test:
1. Remove `postEvent(WIFI_READY, ...)` — not needed, orchestration handles it
2. Remove `postEvent(AUDIO_INIT_OK, ...)` — not needed, orchestration handles it
3. Keep `completeAllActive()` calls — these fire COMMITTED through the orchestrator
4. Remove `test_sleep_wifi_disconnect_updates_registry` and `test_sleep_audio_failed_updates_registry` — these test the SLEEP handler which is removed
5. Remove `test_ready_wifi_disconnect_triggers_error`, `test_live_wifi_disconnect_triggers_error`, `test_live_stream_lost_triggers_error` — these test event handlers that are removed. These error paths now go through the Error Event flag (tested in Task 8)

**Replace `test_ready_setup_failure_triggers_error`:** COMPONENT_SETUP_FAILED is now state-independent, so READY+COMPONENT_SETUP_FAILED→ERROR still works. Keep this test but remove the setup boilerplate:

```cpp
void test_setup_failure_triggers_error() {
    TransitionHooksFixture fixture;
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(fixture.controller.state()));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::COMPONENT_SETUP_FAILED, SystemReason::COMPONENT_SETUP));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(fixture.controller.state()));
}
```

**Replace `test_connecting_transitions_to_error_on_init_failure_events`:**
The old test posted AUDIO_INIT_FAILED in CONNECTING to trigger ERROR. After cleanup, COMPONENT_SETUP_FAILED triggers ERROR from any state. Simplify:

```cpp
void test_setup_failure_triggers_error_from_connecting() {
    TransitionHooksFixture fixture;
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING), static_cast<int>(fixture.controller.state()));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::COMPONENT_SETUP_FAILED, SystemReason::COMPONENT_SETUP));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(fixture.controller.state()));
}
```

**For all other tests:** Systematically replace `postEvent(WIFI_READY)` and `postEvent(AUDIO_INIT_OK)` with `completeAllActive()` calls at the appropriate points. The orchestration handles component readiness now.

**Key pattern for transition tests:**

Old flow:
```
BOOT → SLEEP
PLAY_REQUESTED → CONNECTING → (handler waits)
AUDIO_INIT_OK → (handler checks if both ready)
WIFI_READY → (handler: both ready) → requestStateTransition(READY)
completeAllActive → READY → (targetMode check) → PLAY_REQUESTED → orchestration → LIVE
```

New flow:
```
BOOT → SLEEP
PLAY_REQUESTED → CONNECTING → requestStateTransition(READY) always
completeAllActive → all components COMMITTED → READY → (targetMode check) → PLAY_REQUESTED → orchestration → LIVE
```

Since the test code is large (~22 test functions), this task should be done carefully. Each WIFI_READY and AUDIO_INIT_OK line should be removed. The remaining orchestration flow (completeAllActive) already works correctly.

- [ ] **Step 1:** Remove `test_connecting_waits_for_both_ready_signals_audio_first` and `test_connecting_waits_for_both_ready_signals_wifi_first` from both function definitions and main()
- [ ] **Step 2:** Rewrite `test_connecting_transitions_to_error_on_init_failure_events` to use COMPONENT_SETUP_FAILED
- [ ] **Step 3:** Rewrite `test_ready_wifi_disconnect_triggers_error` → all state COMPONENT_SETUP_FAILED test
- [ ] **Step 4:** Remove `test_live_wifi_disconnect_triggers_error`, `test_live_stream_lost_triggers_error`, `test_sleep_wifi_disconnect_updates_registry`, `test_sleep_audio_failed_updates_registry` (tested via Error Event in Task 8)
- [ ] **Step 5:** For remaining tests, remove all `postEvent(WIFI_READY)` and `postEvent(AUDIO_INIT_OK)` lines — replace with additional `completeAllActive()` calls where needed
- [ ] **Step 6:** Update main() RUN_TEST registrations to match
- [ ] **Step 7:** Run: `pio test -e native` — validate tests pass with updated flow
- [ ] **Step 8:** Commit with message "US-0035: Rewrite test_state_transition_flow tests for new event model"

### Task 8: Add Error Event tests

**Files:** Modify `test/test_state_transition_flow/test_main.cpp`

Add these test functions:

```cpp
void test_error_event_triggers_error_from_ready() {
    Supervisor controller;
    controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(controller.state()));

    // Transition to READY via orchestration (just post COMPONENT_SETUP_FAILED to reach SLEEP again)
    // Use Error Event to trigger ERROR from any state
    controller.setErrorEvent("wifi disconnected", ComponentID::WiFi);
    controller.processMailbox();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(controller.state()));
}

void test_error_event_ignores_duplicates() {
    Supervisor controller;
    controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE);
    controller.setErrorEvent("first error", ComponentID::WiFi);
    controller.setErrorEvent("second error", ComponentID::AudioRuntime);
    // Only first error is stored
    controller.processMailbox();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::ERROR), static_cast<int>(controller.state()));
    // After processing, flag is cleared
    controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE);
    controller.processMailbox();
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP), static_cast<int>(controller.state()));
}
```

Register in main():
```cpp
RUN_TEST(test_error_event_triggers_error_from_ready);
RUN_TEST(test_error_event_ignores_duplicates);
```

- [ ] **Step 1:** Add test functions
- [ ] **Step 2:** Register in main()
- [ ] **Step 3:** Run `pio test -e native` — verify new tests pass
- [ ] **Step 4:** Commit with message "US-0035: Add Error Event flag tests"

### Task 9: Verify full test suite and clean up

- [ ] **Step 1:** Run `pio test -e native` — verify all tests pass
- [ ] **Step 2:** Search for remaining references to removed events: `grep -rn "WIFI_READY\|WIFI_DISCONNECTED\|AUDIO_INIT_OK\|AUDIO_INIT_FAILED\|STREAM_LOST" src/ test/`
- [ ] **Step 3:** Commit with message "US-0035: Finalize supervisor event cleanup"
