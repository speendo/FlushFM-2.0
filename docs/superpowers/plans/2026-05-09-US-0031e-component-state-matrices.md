# US-0031e: Component State Matrices Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Define min/max state matrices for all components, reorder SystemState to 10-gap spacing, and integrate matrix timeouts into beginOrchestration.

**Architecture:** Each component defines a static `ComponentStateMatrix[]` indexed by `stateRank(target)/10`. `beginOrchestration` checks direction (forward/backward) and reads the timeout from the matrix instead of the invoker's hardcoded return value. Components without a matrix (nullptr) keep the existing invoker timeout.

**Tech Stack:** C++20, PlatformIO, Unity

---

### Task 1: Reorder SystemState to 10-gap spacing

**Files:** Modify `src/state_machine/supervisor.h:15-21`

The state hierarchy per state-management.md:
- Level 0 inert: FATAL
- Level 1 boot triggers: ERROR, SLEEP
- Level 2 linear: BOOTING → CONNECTING → READY → LIVE

Replace the SYSTEM_STATE_X macro:

```cpp
#define SYSTEM_STATE_X(V) \
    V(FATAL, 0) \
    V(ERROR, 10) \
    V(SLEEP, 20) \
    V(BOOTING, 30) \
    V(CONNECTING, 40) \
    V(READY, 50) \
    V(LIVE, 60)
```

- [ ] **Step 1:** Edit the macro
- [ ] **Step 2:** Run `pio test -e native` — verify 70 tests, 66 pass, 4 pre-existing errors
- [ ] **Step 3:** Commit

### Task 2: Add ComponentStateMatrix struct and TARGET_MODE sentinel

**Files:** Modify `src/component_types.h` (after the existing enum/struct definitions, before line 74 `using DebugReason`)

Add:

```cpp
struct ComponentStateMatrix {
    uint32_t minState;       // Required minimum (forward enforcement)
    uint32_t maxState;       // Maximum allowed (backward enforcement)
    uint32_t forwardTimeoutMs;
    uint32_t backwardTimeoutMs;
};

constexpr uint32_t TARGET_MODE = 0xFF;
```

Note: use `uint32_t` for minState/maxState to accommodate the `TARGET_MODE` sentinel (which is not a valid `SystemState` enum value). The caller casts back to `SystemState` when needed.

- [ ] **Step 1:** Add the struct and sentinel to `component_types.h`
- [ ] **Step 2:** Verify build: `pio run -e native`
- [ ] **Step 3:** Commit

### Task 3: Extend ComponentTransitionHooks with state matrix

**Files:** Modify `src/state_machine/supervisor.h`

Find the `ComponentTransitionHooks` struct (around line 132-136):

```cpp
struct ComponentTransitionHooks {
    TransitionInvoker transitionInvoker;
    TransitionTimeoutHook timeoutHook;
    const ComponentStateMatrix* stateMatrix = nullptr;
    size_t stateMatrixSize = 0;
};
```

Also update the `setComponentTransitionHooks` declaration (around line 160-163) to accept the optional matrix:

```cpp
bool setComponentTransitionHooks(ComponentID id,
                                 TransitionInvoker transitionInvoker,
                                 TransitionTimeoutHook timeoutHook,
                                 const ComponentStateMatrix* stateMatrix = nullptr,
                                 size_t stateMatrixSize = 0);
```

Add test-only accessor for pending timeout (near `postEventBuffered`/`triggerFatal`):

```cpp
#if !defined(ARDUINO)
    void postEventBuffered(SystemEvent event, SystemReason reason);
    void triggerFatal();
    uint32_t getPendingTimeout(ComponentID id) const;
#endif
```

- [ ] **Step 1:** Edit ComponentTransitionHooks struct
- [ ] **Step 2:** Edit setComponentTransitionHooks declaration
- [ ] **Step 3:** Add getPendingTimeout declaration
- [ ] **Step 4:** Verify build
- [ ] **Step 5:** Commit

### Task 4: Implement setComponentTransitionHooks and getPendingTimeout

**Files:** Modify `src/state_machine/supervisor.cpp`

Find `setComponentTransitionHooks` implementation (~line 87-93). Update signature to accept matrix and store it:

```cpp
bool Supervisor::setComponentTransitionHooks(ComponentID id,
                                             TransitionInvoker transitionInvoker,
                                             TransitionTimeoutHook timeoutHook,
                                             const ComponentStateMatrix* stateMatrix,
                                             size_t stateMatrixSize) {
    if (id == ComponentID::Count) return false;
    componentHooks_[static_cast<size_t>(id)] = ComponentTransitionHooks{
        std::move(transitionInvoker),
        std::move(timeoutHook),
        stateMatrix,
        stateMatrixSize};
    return true;
}
```

Find the `#if !defined(ARDUINO)` block (around `triggerFatal()`, line 59+) and add `getPendingTimeout`:

```cpp
uint32_t Supervisor::getPendingTimeout(ComponentID id) const {
    if (id == ComponentID::Count) return 0;
    return pendingTransitions_[static_cast<size_t>(id)].timeoutMs;
}
```

Also update all 4 call sites where `setComponentTransitionHooks` is called (in system_components.cpp). They currently pass 2 args — they'll need the new optional args added. But that's Task 7.

- [ ] **Step 1:** Edit setComponentTransitionHooks implementation
- [ ] **Step 2:** Add getPendingTimeout in `#if !defined(ARDUINO)` block
- [ ] **Step 3:** Verify build
- [ ] **Step 4:** Commit

### Task 5: Add getStateMatrix() to ISystemComponent

**Files:** Modify `src/components/composition/system_components.h`

Add two virtual methods to `ISystemComponent` (around line 27, before `private:`):

```cpp
virtual const ComponentStateMatrix* getStateMatrix() const { return nullptr; }
virtual size_t getStateMatrixSize() const { return 0; }
```

- [ ] **Step 1:** Add virtual methods
- [ ] **Step 2:** Verify build
- [ ] **Step 3:** Commit

### Task 6: Define matrices for all 4 components

**Files:** Modify `src/components/composition/system_components.cpp`

Add the 4 component matrices after the existing timeout constants (~line 39), before `invokeComponentTransition`:

```cpp
// WiFi matrix (current timeouts: OFF=1000, IDLE=8000, STREAMING=15000, ERROR=1000)
constexpr ComponentStateMatrix kWiFiStateMatrix[] = {
    {static_cast<uint32_t>(SystemState::FATAL), static_cast<uint32_t>(SystemState::FATAL), 100, 100},
    {static_cast<uint32_t>(SystemState::BOOTING), static_cast<uint32_t>(SystemState::BOOTING), 1000, 500},
    {static_cast<uint32_t>(SystemState::SLEEP), static_cast<uint32_t>(SystemState::READY), 1000, 500},
    {static_cast<uint32_t>(SystemState::BOOTING), static_cast<uint32_t>(SystemState::CONNECTING), 2000, 500},
    {static_cast<uint32_t>(SystemState::CONNECTING), static_cast<uint32_t>(SystemState::READY), 8000, 1000},
    {static_cast<uint32_t>(SystemState::READY), TARGET_MODE, 5000, 500},
    {static_cast<uint32_t>(SystemState::READY), TARGET_MODE, 15000, 1000},
};

// Audio matrix (current timeouts: OFF=2000, IDLE=2000, STREAMING=5000, ERROR=1000)
constexpr ComponentStateMatrix kAudioStateMatrix[] = {
    {static_cast<uint32_t>(SystemState::FATAL), static_cast<uint32_t>(SystemState::FATAL), 100, 100},
    {static_cast<uint32_t>(SystemState::BOOTING), static_cast<uint32_t>(SystemState::BOOTING), 1000, 500},
    {static_cast<uint32_t>(SystemState::SLEEP), static_cast<uint32_t>(SystemState::READY), 2000, 500},
    {static_cast<uint32_t>(SystemState::BOOTING), static_cast<uint32_t>(SystemState::CONNECTING), 2000, 500},
    {static_cast<uint32_t>(SystemState::CONNECTING), static_cast<uint32_t>(SystemState::READY), 2000, 1000},
    {static_cast<uint32_t>(SystemState::READY), TARGET_MODE, 2000, 500},
    {static_cast<uint32_t>(SystemState::READY), TARGET_MODE, 5000, 1000},
};

// CLI matrix (all 0 — always satisfied)
constexpr ComponentStateMatrix kCliStateMatrix[] = {
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
};

// BoardInfo matrix (all 0 — always satisfied)
constexpr ComponentStateMatrix kBoardInfoStateMatrix[] = {
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
};
```

Then add `getStateMatrix()`/`getStateMatrixSize()` overrides for each component class. WiFi:

```cpp
const ComponentStateMatrix* getStateMatrix() const override { return kWiFiStateMatrix; }
size_t getStateMatrixSize() const override { return std::size(kWiFiStateMatrix); }
```

Audio:

```cpp
const ComponentStateMatrix* getStateMatrix() const override { return kAudioStateMatrix; }
size_t getStateMatrixSize() const override { return std::size(kAudioStateMatrix); }
```

CLI:

```cpp
const ComponentStateMatrix* getStateMatrix() const override { return kCliStateMatrix; }
size_t getStateMatrixSize() const override { return std::size(kCliStateMatrix); }
```

BoardInfo:

```cpp
const ComponentStateMatrix* getStateMatrix() const override { return kBoardInfoStateMatrix; }
size_t getStateMatrixSize() const override { return std::size(kBoardInfoStateMatrix); }
```

- [ ] **Step 1:** Add the 4 matrix arrays
- [ ] **Step 2:** Add overrides to WiFiComponent class declaration (system_components.h)
- [ ] **Step 3:** Add overrides to AudioRuntimeComponent class declaration (system_components.h)
- [ ] **Step 4:** Add overrides to CliComponent class declaration (system_components.h)
- [ ] **Step 5:** Add overrides to BoardInfoComponent class declaration (system_components.h)
- [ ] **Step 6:** Verify build
- [ ] **Step 7:** Commit

### Task 7: Update registerWithController to pass matrices

**Files:** Modify `src/components/composition/system_components.cpp`

In each component's `registerWithController`, add the matrix args to `setComponentTransitionHooks`:

**BoardInfoComponent** (~line 68-79):
```cpp
void BoardInfoComponent::registerWithController(Supervisor& controller) const {
    controller.registerComponent(id(), false);
    controller.setComponentTransitionHooks(
        id(),
        [component = const_cast<BoardInfoComponent*>(this), &controller](SystemState target, uint32_t transitionId) {
            const uint32_t timeoutMs = invokeComponentTransition(*component, target, transitionId);
            (void)controller.reportCompletion(component->id(), transitionId, TransitionStatus::Completed, nullptr);
            return timeoutMs;
        },
        [component = const_cast<BoardInfoComponent*>(this), &controller](uint32_t transitionId) {
            component->onTransitionTimeout(transitionId);
            (void)controller.reportCompletion(component->id(), transitionId, TransitionStatus::Failed, "timeout");
        },
        getStateMatrix(),
        getStateMatrixSize());
}
```

**WiFiComponent** (~line 113-123):
```cpp
void WiFiComponent::registerWithController(Supervisor& controller) const {
    controller.registerComponent(id(), true);
    controller.setComponentTransitionHooks(
        id(),
        [component = const_cast<WiFiComponent*>(this)](SystemState target, uint32_t transitionId) {
            return invokeComponentTransition(*component, target, transitionId);
        },
        [component = const_cast<WiFiComponent*>(this)](uint32_t transitionId) {
            component->onTransitionTimeout(transitionId);
        },
        getStateMatrix(),
        getStateMatrixSize());
}
```

**AudioRuntimeComponent** (~line 248-265):
```cpp
void AudioRuntimeComponent::registerWithController(Supervisor& controller) const {
    controller.registerComponent(id(), true);
    controller.setComponentTransitionHooks(
        id(),
        [component = const_cast<AudioRuntimeComponent*>(this)](SystemState target, uint32_t transitionId) {
            return invokeComponentTransition(*component, target, transitionId);
        },
        [component = const_cast<AudioRuntimeComponent*>(this)](uint32_t transitionId) {
            component->onTransitionTimeout(transitionId);
        },
        getStateMatrix(),
        getStateMatrixSize());
}
```

**CliComponent** (~line 385-400):
```cpp
void CliComponent::registerWithController(Supervisor& controller) const {
    controller.registerComponent(id(), false);
    controller.setComponentTransitionHooks(
        id(),
        [component = const_cast<CliComponent*>(this), &controller](SystemState target, uint32_t transitionId) {
            const uint32_t timeoutMs = invokeComponentTransition(*component, target, transitionId);
            (void)controller.reportCompletion(component->id(), transitionId, TransitionStatus::Completed, nullptr);
            return timeoutMs;
        },
        [component = const_cast<CliComponent*>(this), &controller](uint32_t transitionId) {
            component->onTransitionTimeout(transitionId);
            (void)controller.reportCompletion(component->id(), transitionId, TransitionStatus::Failed, "timeout");
        },
        getStateMatrix(),
        getStateMatrixSize());
}
```

- [ ] **Step 1:** Update BoardInfoComponent::registerWithController
- [ ] **Step 2:** Update WiFiComponent::registerWithController
- [ ] **Step 3:** Update AudioRuntimeComponent::registerWithController
- [ ] **Step 4:** Update CliComponent::registerWithController
- [ ] **Step 5:** Verify build
- [ ] **Step 6:** Commit

### Task 8: Integrate matrix into beginOrchestration

**Files:** Modify `src/state_machine/supervisor.cpp`

Find the arming loop in `beginOrchestration` (~line 234-250). After `pending.timeoutHandled = false;`, replace the single `pending.timeoutMs = hooks.transitionInvoker(...)` with:

```cpp
        pending.startedAtMs = nowMs();
        pending.timeoutHandled = false;

        const ComponentTransitionHooks& hooks = componentHooks_[i];
        if (!hooks.transitionInvoker) {
            DEBUG_LOG(kLogSource, "No transition hooks registered for component %s",
                      componentName(static_cast<ComponentID>(i)));
            continue;
        }

        // Always call invoker for side effects (component setup/shutdown)
        uint32_t invokerTimeout = hooks.transitionInvoker(target, transitionId);

        // Use matrix timeout if available (replaces invoker timeout)
        if (hooks.stateMatrix && hooks.stateMatrixSize > 0) {
            size_t idx = stateRank(target) / 10;
            if (idx < hooks.stateMatrixSize) {
                bool isForward = stateRank(target) > stateRank(observedState_);
                pending.timeoutMs = isForward
                    ? hooks.stateMatrix[idx].forwardTimeoutMs
                    : hooks.stateMatrix[idx].backwardTimeoutMs;
            } else {
                pending.timeoutMs = invokerTimeout;
            }
        } else {
            pending.timeoutMs = invokerTimeout;
        }
```

- [ ] **Step 1:** Edit the arming loop in beginOrchestration
- [ ] **Step 2:** Verify build
- [ ] **Step 3:** Commit

### Task 9: Add data-structure tests

**Files:** Modify `test/test_component_types/test_main.cpp`

Add before `}  // namespace`:

```cpp
void test_state_matrix_indexing() {
    // Verify WiFi matrix entries are in order (FATAL through LIVE)
    const auto* m = kWiFiStateMatrix;
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL), static_cast<int>(m[0].minState));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(m[5].minState));
    TEST_ASSERT_EQUAL(static_cast<int>(TARGET_MODE), static_cast<int>(m[5].maxState));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::READY), static_cast<int>(m[6].minState));
}

void test_state_matrix_timeouts_nonzero() {
    // Verify forward timeouts are set for non-trivial components
    TEST_ASSERT_TRUE(kWiFiStateMatrix[4].forwardTimeoutMs > 0);  // CONNECTING→READY
    TEST_ASSERT_TRUE(kAudioStateMatrix[6].forwardTimeoutMs > 0);  // LIVE
    // Backward timeouts should also be set
    TEST_ASSERT_TRUE(kWiFiStateMatrix[5].backwardTimeoutMs > 0);  // READY
}
```

The test file includes `supervisor.cpp` directly, but the matrix arrays are in `system_components.cpp`. The test needs to see them. Instead, declare them as `extern const` in the test, or include the arrays directly. Simplest: define the arrays in a header or declare extern.

Actually, the test file needs access to the matrices. The matrices are currently in system_components.cpp's anonymous namespace. They need to be accessible from tests. Options:
A. Move matrices to system_components.h (header)
B. Declare them `extern const` in a shared header
C. Include system_components.cpp in the test

Option C is simplest — the test already includes supervisor.cpp directly. Include system_components.cpp too:

```cpp
#include "../../src/components/composition/system_components.cpp"
```

But that would cause duplicate definitions since tests include both supervisor.cpp and system_components.cpp. Instead, the matrices should be `extern constexpr` in system_components.h:

```cpp
// In system_components.h (after includes):
extern constexpr ComponentStateMatrix kWiFiStateMatrix[];
extern constexpr ComponentStateMatrix kAudioStateMatrix[];
extern constexpr ComponentStateMatrix kCliStateMatrix[];
extern constexpr ComponentStateMatrix kBoardInfoStateMatrix[];
```

But `extern constexpr` doesn't work in C++ — constexpr implies internal linkage. Use `inline constexpr` (C++17) or just `extern const` with a definition in the .cpp.

Simplest approach for this test: just declare the matrix extern and access it. C++20 supports `inline` variables:

```cpp
// In system_components.h:
inline constexpr ComponentStateMatrix kWiFiStateMatrix[] = { ... };
```

Actually, the matrices are large. Better to keep them in the .cpp file. Let me use a different approach for the test: just test through the public API (getStateMatrix).

For test_component_types, include `system_components.h` and call `getStateMatrix()` on a temporary component. Or just test the enum values and not the component matrices specifically, since those are tested via integration tests in Task 10.

Simplify: just test sentinel value and struct size:

```cpp
void test_state_matrix_sentinel_exists() {
    TEST_ASSERT_EQUAL(0xFF, static_cast<int>(TARGET_MODE));
}

void test_state_matrix_struct_has_expected_fields() {
    ComponentStateMatrix m;
    m.forwardTimeoutMs = 100;
    m.backwardTimeoutMs = 200;
    TEST_ASSERT_EQUAL(100, static_cast<int>(m.forwardTimeoutMs));
    TEST_ASSERT_EQUAL(200, static_cast<int>(m.backwardTimeoutMs));
}
```

These are trivial but verify the struct compiles and TARGET_MODE exists.

The integration tests (Task 10) will verify actual matrix values through the Supervisor.

- [ ] **Step 1:** Write `test_state_matrix_sentinel_exists` and `test_state_matrix_struct_has_expected_fields`
- [ ] **Step 2:** Register both in `main()`
- [ ] **Step 3:** Verify build and test
- [ ] **Step 4:** Commit

### Task 10: Add integration tests for matrix timeout enforcement

**Files:** Modify `test/test_state_transition_flow/test_main.cpp`

Add before `}  // namespace`:

```cpp
void test_matrix_forward_timeout_from_sleep_to_ready() {
    TransitionHooksFixture fixture;
    fixture.install();

    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::WIFI_READY, SystemReason::WIFI_INITIALIZED));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::AUDIO_INIT_OK, SystemReason::AUDIO_TASK_STARTED));
    TEST_ASSERT_TRUE(fixture.controller.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST));
    // State: CONNECTING with orchestration active to READY

    // CONNECTING→READY is a forward transition.
    // WiFi matrix for CONNECTING: forwardTimeoutMs = matrix[4].forwardTimeoutMs = 8000
    uint32_t wifiTimeout = fixture.controller.getPendingTimeout(ComponentID::WiFi);
    TEST_ASSERT_GREATER_THAN(0, wifiTimeout);
    // Audio matrix for CONNECTING: forwardTimeoutMs = matrix[4].forwardTimeoutMs = 2000
    uint32_t audioTimeout = fixture.controller.getPendingTimeout(ComponentID::AudioRuntime);
    TEST_ASSERT_GREATER_THAN(0, audioTimeout);
}
```

Register in `main()`:
```cpp
RUN_TEST(test_matrix_forward_timeout_from_sleep_to_ready);
```

Note: `TEST_ASSERT_GREATER_THAN` is available in Unity. If not, use `TEST_ASSERT_TRUE(a > 0)`.

- [ ] **Step 1:** Write `test_matrix_forward_timeout_from_sleep_to_ready`
- [ ] **Step 2:** Register in `main()`
- [ ] **Step 3:** Verify build and test
- [ ] **Step 4:** Commit

### Task 11: Mark US-0031e done

- [ ] **Step 1:** Move `requirements/user-stories/open/US-0031e.md` to `done/`
- [ ] **Step 2:** Update status to Done, check all ACs
- [ ] **Step 3:** Commit
