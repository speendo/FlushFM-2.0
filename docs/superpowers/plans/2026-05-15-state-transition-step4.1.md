# Step 4.1: Split supervisor_v2 into 6 Files (Configuration, Orchestrator, State Machine)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split the existing `supervisor_v2.h`/`.cpp` into three modules — supervisor_v2 (configuration/registration), orchestrator (cross-core I/O), state_machine (state logic + free functions) — each with a `.h` and `.cpp`. Move every existing method to the file it belongs to. No new features. No code lost. All existing tests must keep passing.

**Architecture:** All method declarations stay in `supervisor_v2.h` (unchanged). Method definitions split across three `.cpp` files. New header wrappers `orchestrator.h` and `state_machine.h` each include `supervisor_v2.h`. Tests are updated to include the additional `.cpp` files.

**Tech Stack:** C++17, PlatformIO native, Unity test framework, `#define private public` access pattern

---

### File Structure

```
src/state_machine/
├── supervisor_v2.h          — unchanged (all declarations stay)
├── supervisor_v2.cpp        — thin: constructor, setup(), getters, config, registerComponent()
├── orchestrator.h           — thin wrapper: #pragma once + #include "state_machine/supervisor_v2.h"
├── orchestrator.cpp         — cross-core I/O: postNextComponentState, completeTransition,
│                               postStateRequest, postErrorEvent
├── state_machine.h          — thin wrapper: #pragma once + #include "state_machine/supervisor_v2.h"
├── state_machine.cpp        — state logic + free functions: getNextState, stateToString,
│                               isErrorState, consumeStateRequest, consumeErrorEvent,
│                               setTargetState, resetRecoveryIfOutOfError, checkComponentPresence
```

---

### Method Categorization

**supervisor_v2.cpp** — Configuration & Registration (9 methods, lines 71-125):
- `SupervisorV2()` — constructor (line 71)
- `setup()` (lines 73-76)
- `getObservedState()` / `getTargetState()` (lines 112-118)
- `getMaxRecoveries()` / `setMaxRecoveries()` (lines 88-96)
- `getTransitionTimeout()` / `loadTransitionTimeoutConfig()` (lines 98-110)
- `registerComponent()` (lines 120-125)

**orchestrator.cpp** — Cross-Core Write Operations (4 methods, lines 127-176):
- `postNextComponentState()` (lines 127-136)
- `completeTransition()` (lines 138-159)
- `postStateRequest()` (lines 161-166)
- `postErrorEvent()` (lines 168-176)

**state_machine.cpp** — State Logic + Free Functions (8 items, lines 13-69 + 78-86 + 178-236):
- `stateToString()` (free function, lines 13-24)
- `isErrorState()` (free function, lines 26-28)
- `getNextState()` (free function, lines 35-69)
- `checkComponentPresence()` (lines 78-86)
- `consumeStateRequest()` (lines 178-194)
- `consumeErrorEvent()` (lines 196-225)
- `setTargetState()` (lines 227-230)
- `resetRecoveryIfOutOfError()` (lines 232-236)

---

### Dependency Notes

- `kLogSource` (anonymous namespace) is only used in `getNextState`, `consumeErrorEvent`, `setTargetState`. All move to `state_machine.cpp`. Removed from `supervisor_v2.cpp`.
- `#include "core/debug.h"` only needed by `state_machine.cpp` (for PROD_LOG/ERROR_LOG). Removed from `supervisor_v2.cpp`.
- `orchestrator.h` and `state_machine.h` are thin wrappers — each just includes `supervisor_v2.h`.
- Each `.cpp` file includes its own `.h` wrapper (not `supervisor_v2.h` directly).

---

### Test File Updates

Three existing tests include `supervisor_v2.cpp` and must be updated:

| Test file | Before | After |
|-----------|--------|-------|
| `test_supervisor_v2_registration/test_main.cpp:4` | `#include "../../src/state_machine/supervisor_v2.cpp"` | + `orchestrator.cpp` + `state_machine.cpp` |
| `test_supervisor_v2_mailbox_spinlock/test_main.cpp:4` | same | + `orchestrator.cpp` + `state_machine.cpp` |
| `test_supervisor_v2_get_next_state/test_main.cpp:3` | same | + `state_machine.cpp` |

Pattern for the first two:
```cpp
#define private public
#include "../../src/state_machine/supervisor_v2.cpp"
#include "../../src/state_machine/orchestrator.cpp"
#include "../../src/state_machine/state_machine.cpp"
#undef private
```

Pattern for get_next_state (no `#define private public`):
```cpp
#include "../../src/state_machine/supervisor_v2.cpp"
#include "../../src/state_machine/state_machine.cpp"
```

---

### Task 4.1a: Create state_machine.cpp + update tests

**Files:**
- Create: `src/state_machine/state_machine.cpp`
- Modify: `src/state_machine/supervisor_v2.cpp` — remove the 8 moved items
- Modify: `test/test_supervisor_v2_registration/test_main.cpp` — add include
- Modify: `test/test_supervisor_v2_mailbox_spinlock/test_main.cpp` — add include
- Modify: `test/test_supervisor_v2_get_next_state/test_main.cpp` — add include

- [ ] **Step 4.1a.1: Create `src/state_machine/state_machine.cpp`**

```cpp
#include "state_machine/supervisor_v2.h"

#include "core/debug.h"

namespace {

constexpr const char* kLogSource = "Supervisor";

}  // namespace

const char* stateToString(SystemState state) {
    switch (state) {
        case SystemState::FATAL: return "FATAL";
        case SystemState::ERROR: return "ERROR";
        case SystemState::SLEEP: return "SLEEP";
        case SystemState::BOOTING: return "BOOTING";
        case SystemState::CONNECTING: return "CONNECTING";
        case SystemState::READY: return "READY";
        case SystemState::LIVE: return "LIVE";
    }
    return "UNKNOWN";
}

bool isErrorState(SystemState state) {
    return state == SystemState::ERROR || state == SystemState::FATAL;
}

SystemState getNextState(SystemState current, SystemState target) {
    if (current == SystemState::FATAL) return SystemState::FATAL;

    if (isErrorState(target)) {
        return target;
    }

    if (isErrorState(current) && target != SystemState::SLEEP) {
        return SystemState::BOOTING;
    }

    int currentIndex = getIndex(current);
    int targetIndex = getIndex(target);

    if (currentIndex < 0 || targetIndex < 0) {
        ERROR_LOG(kLogSource, "Invalid state in getNextState: current=%s target=%s; falling back to FATAL",
                  stateToString(current), stateToString(target));
        return SystemState::FATAL;
    }

    if (currentIndex < targetIndex) {
        return stateRoute[currentIndex + 1];
    }
    if (currentIndex > targetIndex) {
        return stateRoute[currentIndex - 1];
    }
    return current;
}

void SupervisorV2::checkComponentPresence() {
    for (size_t i = 0; i < componentCount; i++) {
        if (componentMailboxes_[i] == nullptr && isRequired_[i]) {
            postErrorEvent("component absent", static_cast<ComponentID>(i));
        }
    }
}

bool SupervisorV2::consumeStateRequest() {
    SystemState target;
    bool hadPending = false;

    portENTER_CRITICAL(&stateRequestMailbox_.spinlock);
    if (stateRequestMailbox_.pending) {
        target = stateRequestMailbox_.requestedTarget;
        stateRequestMailbox_.pending = false;
        hadPending = true;
    }
    portEXIT_CRITICAL(&stateRequestMailbox_.spinlock);

    if (hadPending) {
        setTargetState(target);
    }
    return hadPending;
}

void SupervisorV2::consumeErrorEvent() {
    DebugReason reasonCopy = nullptr;
    ComponentID sourceCopy = ComponentID::Count;
    bool gotError = false;

    portENTER_CRITICAL(&errorEvent_.spinlock);
    if (errorEvent_.pending) {
        reasonCopy = errorEvent_.reason;
        sourceCopy = errorEvent_.source;
        errorEvent_.pending = false;
        errorEvent_.reason = nullptr;
        errorEvent_.source = ComponentID::Count;
        gotError = true;
    }
    portEXIT_CRITICAL(&errorEvent_.spinlock);

    if (!gotError) return;

    PROD_LOG(kLogSource, "[%s] %s - recovery attempt #%d/%d",
             componentName(sourceCopy), reasonCopy,
             retryPolicy_.recoveryCounter + 1, retryPolicy_.maxRecoveries);

    retryPolicy_.recoveryCounter++;

    if (retryPolicy_.isExhausted()) {
        setTargetState(SystemState::FATAL);
    } else {
        setTargetState(SystemState::ERROR);
    }
}

void SupervisorV2::setTargetState(SystemState target) {
    PROD_LOG(kLogSource, "Setting target state to %s", stateToString(target));
    targetState_ = target;
}

void SupervisorV2::resetRecoveryIfOutOfError() {
    if (!isErrorState(observedState_)) {
        retryPolicy_.recoveryCounter = 0;
    }
}
```

- [ ] **Step 4.1a.2: Trim `src/state_machine/supervisor_v2.cpp`** — remove lines 5-9 (anonymous namespace), 13-69 (free functions), 78-86 (checkComponentPresence), 178-236 (consume/setTarget/resetRecovery). Remove `#include "core/debug.h"` (line 3). The result:

```cpp
#include "state_machine/supervisor_v2.h"

SupervisorV2::SupervisorV2() = default;

void SupervisorV2::setup() {
    eventGroup_ = xEventGroupCreateStatic(&eventGroupBuffer_);
    loadTransitionTimeoutConfig();
}

int SupervisorV2::getMaxRecoveries() const {
    return retryPolicy_.maxRecoveries;
}

void SupervisorV2::setMaxRecoveries(int recoveries) {
    if (recoveries >= 1) {
        retryPolicy_.maxRecoveries = recoveries;
    }
}

uint32_t SupervisorV2::getTransitionTimeout(SystemState state, bool isForward) const {
    int idx = getIndex(state);
    if (idx >= 0 && idx < static_cast<int>(stateCount)) {
        return isForward ? timeoutConfig_.forwardTimeouts[idx]
                            : timeoutConfig_.backwardTimeouts[idx];
    }
    return 0;
}

void SupervisorV2::loadTransitionTimeoutConfig() {
    timeoutConfig_.forwardTimeouts = kDefaultForwardTimeouts;
    timeoutConfig_.backwardTimeouts = kDefaultBackwardTimeouts;
}

SystemState SupervisorV2::getObservedState() const {
    return observedState_;
}

SystemState SupervisorV2::getTargetState() const {
    return targetState_;
}

void SupervisorV2::registerComponent(ComponentID id, ComponentMailbox* mailbox, bool isRequired) {
    componentMailboxes_[static_cast<int>(id)] = mailbox;
    isRequired_[static_cast<int>(id)] = isRequired;
}
```

- [ ] **Step 4.1a.3: Update test includes**

`test/test_supervisor_v2_registration/test_main.cpp` — change lines 3-5:
```cpp
#define private public
#include "../../src/state_machine/supervisor_v2.cpp"
#include "../../src/state_machine/orchestrator.cpp"
#include "../../src/state_machine/state_machine.cpp"
#undef private
```

Wait — `orchestrator.cpp` doesn't exist yet (created in task 4.1b). But this test uses `completeTransition`, `postNextComponentState`, and `postErrorEvent` (via `checkComponentPresence`). These methods will be in `orchestrator.cpp` once it's created, but `completeTransition` and `postNextComponentState` are still in `supervisor_v2.cpp` until task 4.1b.

To avoid a chicken-and-egg problem, I need to split in the right order: state_machine.cpp created first, but the missing orchestrator.cpp methods stay in supervisor_v2.cpp until 4.1b.

So for task 4.1a, only update the `get_next_state` test (since it only needs state_machine.cpp). The registration and mailbox_spinlock tests keep including `supervisor_v2.cpp` which still contains the orchestrator methods — they don't need `state_machine.cpp` yet because the methods they use haven't moved.

Wait, they DO need state_machine.cpp — `checkComponentPresence` is used in registration tests, and `consumeStateRequest`/`consumeErrorEvent`/`setTargetState` are used in mailbox_spinlock tests. These methods moved to state_machine.cpp.

Hmm, so the test update needs:
- `test_supervisor_v2_get_next_state`: supervisor_v2.cpp + state_machine.cpp
- `test_supervisor_v2_registration`: supervisor_v2.cpp + state_machine.cpp (since checkComponentPresence moved but completeTransition/postNextComponentState/postErrorEvent haven't — those are still in supervisor_v2.cpp until 4.1b)
- `test_supervisor_v2_mailbox_spinlock`: supervisor_v2.cpp + state_machine.cpp (consumeStateRequest/consumeErrorEvent/setTargetState moved)

I don't need orchestrator.cpp includes until 4.1b.

`test/test_supervisor_v2_registration/test_main.cpp` lines 3-5 →:
```cpp
#define private public
#include "../../src/state_machine/supervisor_v2.cpp"
#include "../../src/state_machine/state_machine.cpp"
#undef private
```

`test/test_supervisor_v2_mailbox_spinlock/test_main.cpp` lines 3-5 →:
```cpp
#define private public
#include "../../src/state_machine/supervisor_v2.cpp"
#include "../../src/state_machine/state_machine.cpp"
#undef private
```

`test/test_supervisor_v2_get_next_state/test_main.cpp` line 3 →:
```cpp
#include "../../src/state_machine/supervisor_v2.cpp"
#include "../../src/state_machine/state_machine.cpp"
```

- [ ] **Step 4.1a.4: Run full suite**

```bash
pio test -e native
```

Expected: 95 succeeded. 4 pre-existing errors unchanged. No regressions.

- [ ] **Step 4.1a.5: Commit**

```bash
git add src/state_machine/supervisor_v2.cpp src/state_machine/state_machine.cpp test/test_supervisor_v2_registration/test_main.cpp test/test_supervisor_v2_mailbox_spinlock/test_main.cpp test/test_supervisor_v2_get_next_state/test_main.cpp
git commit -m "step 4.1a: extract state_machine.cpp from supervisor_v2.cpp"
```

---

### Task 4.1b: Create orchestrator.cpp + update tests

**Files:**
- Create: `src/state_machine/orchestrator.cpp`
- Modify: `src/state_machine/supervisor_v2.cpp` — remove the 4 moved methods
- Modify: `test/test_supervisor_v2_registration/test_main.cpp` — add `orchestrator.cpp` include
- Modify: `test/test_supervisor_v2_mailbox_spinlock/test_main.cpp` — add `orchestrator.cpp` include

- [ ] **Step 4.1b.1: Create `src/state_machine/orchestrator.cpp`**

```cpp
#include "state_machine/supervisor_v2.h"

void SupervisorV2::postNextComponentState(ComponentID id) {
    ComponentMailbox* mailbox = componentMailboxes_[static_cast<int>(id)];
    if (mailbox == nullptr) return;
    portENTER_CRITICAL(&mailbox->spinlock);
    mailbox->pending = true;
    mailbox->targetState = nextState_.transitionTarget;
    portEXIT_CRITICAL(&mailbox->spinlock);
}

void SupervisorV2::completeTransition(ComponentID id, TransitionStatus status) {
    if (status == TransitionStatus::Completed) {
        xEventGroupSetBits(eventGroup_, 1 << static_cast<int>(id));
        return;
    }

    if (isRequired_[static_cast<int>(id)]) {
        postErrorEvent("component failed", id);
    } else {
        componentStatuses_[static_cast<int>(id)] = ComponentStatus::DEGRADED;
    }
}

void SupervisorV2::postStateRequest(SystemState target) {
    portENTER_CRITICAL(&stateRequestMailbox_.spinlock);
    stateRequestMailbox_.pending = true;
    stateRequestMailbox_.requestedTarget = target;
    portEXIT_CRITICAL(&stateRequestMailbox_.spinlock);
}

void SupervisorV2::postErrorEvent(DebugReason reason, ComponentID source) {
    portENTER_CRITICAL(&errorEvent_.spinlock);
    if (!errorEvent_.pending) {
        errorEvent_.pending = true;
        errorEvent_.reason = reason;
        errorEvent_.source = source;
    }
    portEXIT_CRITICAL(&errorEvent_.spinlock);
}
```

- [ ] **Step 4.1b.2: Remove moved methods from `supervisor_v2.cpp`**

Remove lines 127-176 (`postNextComponentState`, `completeTransition`, `postStateRequest`, `postErrorEvent`).

- [ ] **Step 4.1b.3: Update test includes**

`test/test_supervisor_v2_registration/test_main.cpp` — add orchestrator.cpp:
```cpp
#define private public
#include "../../src/state_machine/supervisor_v2.cpp"
#include "../../src/state_machine/orchestrator.cpp"
#include "../../src/state_machine/state_machine.cpp"
#undef private
```

`test/test_supervisor_v2_mailbox_spinlock/test_main.cpp` — add orchestrator.cpp:
```cpp
#define private public
#include "../../src/state_machine/supervisor_v2.cpp"
#include "../../src/state_machine/orchestrator.cpp"
#include "../../src/state_machine/state_machine.cpp"
#undef private
```

- [ ] **Step 4.1b.4: Run full suite**

```bash
pio test -e native
```

Expected: 95 succeeded. 4 pre-existing errors unchanged.

- [ ] **Step 4.1b.5: Commit**

```bash
git add src/state_machine/supervisor_v2.cpp src/state_machine/orchestrator.cpp test/test_supervisor_v2_registration/test_main.cpp test/test_supervisor_v2_mailbox_spinlock/test_main.cpp
git commit -m "step 4.1b: extract orchestrator.cpp from supervisor_v2.cpp"
```

---

### Task 4.1c: Create header wrappers

**Files:**
- Create: `src/state_machine/orchestrator.h`
- Create: `src/state_machine/state_machine.h`

- [ ] **Step 4.1c.1: Create `src/state_machine/orchestrator.h`**

```cpp
#pragma once

#include "state_machine/supervisor_v2.h"
```

- [ ] **Step 4.1c.2: Create `src/state_machine/state_machine.h`**

```cpp
#pragma once

#include "state_machine/supervisor_v2.h"
```

- [ ] **Step 4.1c.3: Run full suite** (headers are unused at this point, but verify no breakage)

```bash
pio test -e native
```

Expected: 95 succeeded. 4 pre-existing errors unchanged.

- [ ] **Step 4.1c.4: Commit**

```bash
git add src/state_machine/orchestrator.h src/state_machine/state_machine.h
git commit -m "step 4.1c: add orchestrator.h and state_machine.h header wrappers"
```
