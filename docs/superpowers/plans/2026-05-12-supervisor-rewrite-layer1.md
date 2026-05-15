# Supervisor Rewrite — Layer 1: New Header

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create the new supervisor header with enums, class skeleton, and public API. No implementation — just types and declarations.

**Architecture:** Single new header (`supervisor_v2.h`) containing `SystemState`, `SystemEvent` (now with `TRANSITION_COMPLETED`), `SystemReason`, `stateRank()`, `toString()`, and the `Supervisor` class skeleton. Old files untouched.

**Tech Stack:** C++20, X-macro pattern, PlatformIO native env, Unity test framework

---

### Task: Write the header and a compile-smoke test

**Files:**
- Create: `src/state_machine/supervisor_v2.h`
- Create: `test/test_supervisor_v2/test_main.cpp`

- [ ] **Step 1: Write the new header file**

Create `src/state_machine/supervisor_v2.h` with the content below:

```cpp
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#if defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#endif

#include "component_types.h"

#define SYSTEM_STATE_X(V) \
    V(FATAL, 0) \
    V(ERROR, 10) \
    V(SLEEP, 20) \
    V(BOOTING, 30) \
    V(CONNECTING, 40) \
    V(READY, 50) \
    V(LIVE, 60)

#define SYSTEM_STATE_ENUM(name, value) name = value,

enum class SystemState : uint8_t {
    SYSTEM_STATE_X(SYSTEM_STATE_ENUM)
};

constexpr uint8_t stateRank(SystemState state) {
    return static_cast<uint8_t>(state);
}

inline const char* toString(SystemState state) {
    switch (state) {
#define SYSTEM_STATE_STRING(name, value) case SystemState::name: return #name;
        SYSTEM_STATE_X(SYSTEM_STATE_STRING)
#undef SYSTEM_STATE_STRING
    }
    return "UNKNOWN";
}

#undef SYSTEM_STATE_ENUM
#undef SYSTEM_STATE_X

#define SYSTEM_EVENT_X(V) \
    V(COMPONENT_SETUP_FAILED) \
    V(STATE_REQUESTED) \
    V(TRANSITION_COMPLETED)

#define SYSTEM_EVENT_ENUM(name) name,

enum class SystemEvent {
    SYSTEM_EVENT_X(SYSTEM_EVENT_ENUM)
};

inline const char* toString(SystemEvent event) {
    switch (event) {
#define SYSTEM_EVENT_STRING(name) case SystemEvent::name: return #name;
        SYSTEM_EVENT_X(SYSTEM_EVENT_STRING)
#undef SYSTEM_EVENT_STRING
    }
    return "UNKNOWN";
}

#undef SYSTEM_EVENT_ENUM
#undef SYSTEM_EVENT_X

#define SYSTEM_REASON_X(V) \
    V(NONE) \
    V(COMPONENT_SETUP) \
    V(WIFI_INITIALIZED) \
    V(AUDIO_TASK_STARTED) \
    V(AUDIO_TASK_INIT_FAILED) \
    V(USER_REQUEST) \
    V(RECOVERY) \
    V(POWER_POLICY)

#define SYSTEM_REASON_ENUM(name) name,

enum class SystemReason {
    SYSTEM_REASON_X(SYSTEM_REASON_ENUM)
};

inline const char* toString(SystemReason reason) {
    switch (reason) {
#define SYSTEM_REASON_STRING(name) case SystemReason::name: return #name;
        SYSTEM_REASON_X(SYSTEM_REASON_STRING)
#undef SYSTEM_REASON_STRING
    }
    return "UNKNOWN";
}

#undef SYSTEM_REASON_ENUM
#undef SYSTEM_REASON_X

class Supervisor {
public:
    using StateObserver = std::function<void(SystemState)>;

    Supervisor();
    SystemState state() const;
    void subscribe(StateObserver observer);
    void setup();
    bool postEvent(SystemEvent event, SystemReason reason);
    bool postEvent(SystemEvent event, SystemReason reason, SystemState target);
    void setErrorEvent(DebugReason reason, ComponentID source);
    bool registerComponent(ComponentID id, bool isRequired);

#if defined(ARDUINO)
    void run();
#else
    void processMailbox();
#endif

private:
    void handleEvent(SystemEvent event, SystemReason reason);
    void setObservedStateImmediate(SystemState next, SystemEvent trigger, SystemReason reason);
    void stepTowardTarget(SystemEvent event, SystemReason reason);
    void beginTransition(SystemState target);
    void checkTimeouts();

    SystemState observedState_ = SystemState::BOOTING;
    SystemState targetMode_ = SystemState::SLEEP;
    bool transientError_ = false;

    std::array<ComponentRegistryEntry, static_cast<size_t>(ComponentID::Count)> registry_{};
    std::vector<StateObserver> observers_;

    uint32_t nextId_ = 1;

    struct CompletionSlot {
        ComponentID component = ComponentID::Count;
        TransitionStatus status = TransitionStatus::Completed;
        DebugReason reason = nullptr;
        bool pending = false;
    };
    CompletionSlot completionSlot_{};
};
```

- [ ] **Step 2: Write a minimal compile-smoke test**

Create `test/test_supervisor_v2/test_main.cpp`:

```cpp
#include <unity.h>

#include "../../src/state_machine/supervisor_v2.h"

namespace {

void test_state_rank_values() {
    TEST_ASSERT_EQUAL_UINT8(0, stateRank(SystemState::FATAL));
    TEST_ASSERT_EQUAL_UINT8(30, stateRank(SystemState::BOOTING));
    TEST_ASSERT_EQUAL_UINT8(60, stateRank(SystemState::LIVE));
}

void test_new_event_exists() {
    // TRANSITION_COMPLETED must be defined in SystemEvent
    SystemEvent e = SystemEvent::TRANSITION_COMPLETED;
    TEST_ASSERT_EQUAL_STRING("TRANSITION_COMPLETED", toString(e));
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_state_rank_values);
    RUN_TEST(test_new_event_exists);
    return UNITY_END();
}
```

- [ ] **Step 3: Run the test and verify it passes**

```bash
pio test -e native -f test_supervisor_v2
```

Expected: 2 tests pass.

- [ ] **Step 4: Run full test suite to verify no regressions**

```bash
pio test -e native
```

Expected: All existing tests still pass.

- [ ] **Step 5: Commit**

```bash
git add src/state_machine/supervisor_v2.h test/test_supervisor_v2/test_main.cpp
git commit -m "supervisor-rewrite: add supervisor_v2 header with enums and class skeleton"
```
