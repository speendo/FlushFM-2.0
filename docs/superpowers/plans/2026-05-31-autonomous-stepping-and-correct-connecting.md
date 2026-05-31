# Autonomous Stepping and Correct CONNECTING Behavior Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix two bugs: (1) state machine requires multiple `postStateRequest()` calls to traverse multi-rank transitions, and (2) `AudioRuntimeComponent::handleCONNECTING()` starts audio streaming even during backward transitions where audio should remain off.

**Architecture:**
- **Fix 1**: In `Supervisor::checkOrchestrationResponse()`, after advancing `observedState_` on COMPLETED, if `targetState_ != observedState_`, call `xTaskNotifyGive(supervisorTaskHandle_)` to self-wake the state machine. This causes the next `ulTaskNotifyTake` in `run()` to return immediately, continuing the march toward the target without waiting for an external event.
- **Fix 2**: Remove audio streaming logic from `AudioRuntimeComponent::handleCONNECTING()`. CONNECTING is for network-level connectivity (WiFi) only. Audio streaming is deferred to `handleLIVE()`. This resolves audio turning on during backward transitions (e.g. READY → SLEEP passes through CONNECTING, which previously started streaming).

**Tech Stack:** C++20, FreeRTOS (ESP32-S3), Arduino framework, Unity test framework

**Testing note:** Fix 1 adds a 3-line `xTaskNotifyGive(self)` call — the FreeRTOS notification mechanism is untestable on native (both `xTaskNotifyGive` and `ulTaskNotifyTake` are stubbed as no-ops). The fix is verified on hardware (Task 5).

---

## Files

| File | Role |
|------|------|
| `src/supervisor/orchestrator.cpp:114-134` | Modify `checkOrchestrationResponse()` to self-wake after state advance |
| `src/components/composition/system_components.cpp:199-215` | Change `AudioRuntimeComponent::handleCONNECTING()` to not start audio |
| `test/test_supervisor_v2_get_next_state/test_main.cpp` | Add test for downward transition path verification |

---

### Task 1: Implement self-wake in checkOrchestrationResponse()

**Files:**
- Modify: `src/supervisor/orchestrator.cpp:114-134`

- [x] **Step 1: Add self-wake after state advance**

In `checkOrchestrationResponse()`, after the `setObservedState()` call in the COMPLETED branch, add a self-wake when the target state has not yet been reached:

```cpp
void Supervisor::checkOrchestrationResponse() {
    OrchestrationResult result;
    EventBits_t timedOutComponents;
    if (!responseMailbox_.consume(result, timedOutComponents)) return;

    hasActiveOrchestration_ = false;

    if (result == OrchestrationResult::COMPLETED) {
        setObservedState(nextState_.transitionTarget);

        // Self-wake if the final target has not been reached.
        // The next run() call will immediately step toward the next
        // intermediate state without waiting for an external event.
        if (targetState_ != observedState_) {
            xTaskNotifyGive(supervisorTaskHandle_);
        }
    } else {
        for (size_t i = 0; i < componentCount; i++) {
            if (!(timedOutComponents & (1 << i))) continue;
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

The only change is the 4 lines after `setObservedState()` — a comment and the `if (targetState_ != observedState_)` block with `xTaskNotifyGive`.

- [x] **Step 2: Run ALL tests to confirm no regressions**

Run: `pio test -e native`
Expected: All 140+ tests PASS.

- [x] **Step 3: Build for hardware**

Run: `pio run -e debug`
Expected: SUCCESS

---

### Task 2: Fix AudioRuntimeComponent::handleCONNECTING() to not start audio

**Files:**
- Modify: `src/components/composition/system_components.cpp:199-215`

- [x] **Step 1: Replace handleCONNECTING with sync completion**

Replace the entire `AudioRuntimeComponent::handleCONNECTING()` body (lines 199-215) with a synchronous completion:

```cpp
void AudioRuntimeComponent::handleCONNECTING() {
    // CONNECTING is for network-level connections (WiFi) only.
    // Audio streaming is deferred to handleLIVE().
    completeTransition(TransitionStatus::Completed);
}
```

The old implementation loaded a persisted station URL, called `connectToHost()`, and set the `pendingStreamingTarget_` flag for async completion via `poll()`. None of that belongs in the CONNECTING handler — audio streaming is only triggered in `handleLIVE()`.

- [x] **Step 2: Build — ensure no compilation errors**

Run: `pio run -e debug`
Expected: SUCCESS

- [x] **Step 3: Run ALL tests**

Run: `pio test -e native`
Expected: All 140+ tests PASS.

---

### Task 3: Add test verifying downward transition path does not route through LIVE

**Files:**
- Modify: `test/test_supervisor_v2_get_next_state/test_main.cpp`

The `getNextState()` function handles the rank-based stepping. CONNECTING (rank 40) no longer starts audio, but the path from READY (50) down to SLEEP (20) still passes through CONNECTING. We verify the path is correct and that LIVE (rank 60) is never on the downward route.

- [x] **Step 1: Add the test**

Add this test after the existing test cases in `test/test_supervisor_v2_get_next_state/test_main.cpp`:

```cpp
void test_get_next_state_downward_path_never_reaches_live() {
    // From READY (50) to SLEEP (20), the path is:
    // READY → CONNECTING → BOOTING → SLEEP
    // LIVE (60) must never appear on any step of this path.

    SystemState next = getNextState(SystemState::READY, SystemState::SLEEP);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::CONNECTING),
                      static_cast<int>(next));
    TEST_ASSERT_NOT_EQUAL(static_cast<int>(SystemState::LIVE),
                          static_cast<int>(next));

    next = getNextState(next, SystemState::SLEEP);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::BOOTING),
                      static_cast<int>(next));
    TEST_ASSERT_NOT_EQUAL(static_cast<int>(SystemState::LIVE),
                          static_cast<int>(next));

    next = getNextState(next, SystemState::SLEEP);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP),
                      static_cast<int>(next));
    TEST_ASSERT_NOT_EQUAL(static_cast<int>(SystemState::LIVE),
                          static_cast<int>(next));
}

void test_get_next_state_downward_path_never_reaches_live_when_target_is_ready() {
    // From LIVE (60) to READY (50): direct one-step path.
    // From CONNECTING (40) to SLEEP (20): CONNECTING → BOOTING → SLEEP.
    // LIVE must never appear on any step.

    SystemState next = getNextState(SystemState::LIVE, SystemState::READY);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY),
                      static_cast<int>(next));

    next = getNextState(SystemState::CONNECTING, SystemState::SLEEP);
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::BOOTING),
                      static_cast<int>(next));
    TEST_ASSERT_NOT_EQUAL(static_cast<int>(SystemState::LIVE),
                          static_cast<int>(next));
}
```

- [x] **Step 2: Register the tests**

In the same file's `main()` function, add after the existing `RUN_TEST` lines:

```cpp
RUN_TEST(test_get_next_state_downward_path_never_reaches_live);
RUN_TEST(test_get_next_state_downward_path_never_reaches_live_when_target_is_ready);
```

- [x] **Step 3: Run the tests**

Run: `pio test -e native -f test_supervisor_v2_get_next_state`
Expected: All 9 tests PASS (7 existing + 2 new).

---

### Task 4: Run full test suite

- [x] **Step 1: Run all native tests**

```bash
pio test -e native
```

Expected: All ~142 tests PASS (140 existing + 2 new).

---

### Task 5: Verify end-to-end on hardware

- [x] **Step 1: Flash to ESP32-S3**

```bash
pio run -e debug --target upload
```

- [x] **Step 2: Open serial monitor**

```bash
pio device monitor -b 115200
```

- [x] **Step 3: Test multi-step forward transition**

From any state (e.g. SLEEP), type `transition ready`. Verify:
- System steps to BOOTING immediately
- Then autonomously steps to CONNECTING (WiFi connects, audio stays silent)
- Then autonomously steps to READY
- All steps complete **without retyping the command**

- [x] **Step 4: Test backward transition from READY to SLEEP**

Type `transition sleep` from READY state. Verify:
- System steps to CONNECTING (WiFi stays connected, audio stays OFF)
- Then autonomously to BOOTING
- Then autonomously to SLEEP (WiFi disconnects, audio off)
- No audio click or pop heard during the transition

- [x] **Step 5: Test full audio lifecycle**

Type `play` to enter LIVE (audio streaming). Verify audio plays.
Type `stop` — verify audio stops and system returns to READY.
Type `transition sleep` — verify system reaches SLEEP, audio never restarts.

---

### Task 6: Commit

- [ ] **Step 1: Stage and commit**

```bash
git add src/supervisor/orchestrator.cpp
git add src/components/composition/system_components.cpp
git add test/test_supervisor_v2_get_next_state/test_main.cpp
git commit -m "fix: autonomous stepping and correct CONNECTING audio behavior"
```

---

## Self-Review Checklist

- [ ] **Spec coverage:** Both bugs are fixed: Fix 1 (Task 1) adds self-wake for autonomous stepping; Fix 2 (Task 2) removes audio from CONNECTING. Tests added in Task 3 verify the downward state path.
- [ ] **Placeholder scan:** No TBD, TODO, or placeholder patterns. All code shown in full.
- [ ] **Type consistency:** Method signatures unchanged. No new public API.
- [ ] **No extra scope:** Only the two bugs are fixed. No unrelated refactoring.
- [ ] **Native testability:** Fix 1's self-wake is a FreeRTOS primitive (xTaskNotifyGive) — verified on hardware (Task 5), not testable on native. Fix 2 is fully testable.
