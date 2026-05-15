# Restore Stripped Comments Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore the 7 comment blocks that were stripped during the step 4.1a–4.1c file split, after verifying each comment is still valid.

**Architecture:** 3 `.cpp` files need inline and Doxygen comments restored. All comments were manually verified against the original source (commit `5305981`) and confirmed valid — no code changed during or after the split, so every comment still accurately describes its function.

**Tech Stack:** C++17, FreeRTOS (ESP32-S3). PlatformIO for testing.

**Background:** During step 4.1a–4.1c, functions were extracted from `supervisor_v2.cpp` into `state_machine.cpp` and `orchestrator.cpp`. All comments (`//` inline and `/** */` Doxygen) were stripped during the move. The header (`supervisor_v2.h`) already carries Doxygen for public API methods — this plan restores only the implementation-level comments in `.cpp` files.

**Sanity check performed:** Each comment is still valid:
- `getNextState()` logic unchanged (FATAL absorbent, ERROR immediate, recovery jump, rank stepping)
- `checkComponentPresence()` still scans for null mailboxes on required components
- `registerComponent()` still stores mailbox and isRequired
- `postNextComponentState()` still writes to mailbox under spinlock
- `completeTransition()` still sets event group bits on completion, posts error for required failures, marks optional as DEGRADED

---

### Task 1: Restore comments in `state_machine.cpp` — `getNextState()`

**Files:**
- Modify: `src/supervisor/state_machine.cpp:28-54`

- [ ] **Step 1: Add Doxygen comment + inline comments to `getNextState()`**

```cpp
/** @brief Get the next system state based on the current and target states.
 *  @param current The current system state.
 *  @param target The target system state.
 *  @return The next system state.
 */
SystemState getNextState(SystemState current, SystemState target) {
    // FATAL is absorbent — no state transitions out of FATAL.
    if (current == SystemState::FATAL) return SystemState::FATAL;

    // ERROR and FATAL as target are immediate — no stepping needed.
    if (isErrorState(target)) {
        return target;
    }

    // Recovery from ERROR jumps directly to BOOTING, skipping SLEEP.
    // Only applies when the target is not SLEEP itself.
    if (isErrorState(current) && target != SystemState::SLEEP) {
        return SystemState::BOOTING;
    }

    // For all other combinations, step through the route based on rank comparison.
    // Lower rank = less active (FATAL=0, ERROR=10, SLEEP=20, BOOTING=30,
    // CONNECTING=40, READY=50, LIVE=60). Step up or down one rank at a time.
    int currentIndex = getIndex(current);
    int targetIndex = getIndex(target);

    if (currentIndex < 0 || targetIndex < 0) {
        ERROR_LOG(kLogSource, "Invalid state in getNextState: current=%s target=%s; falling back to FATAL",
                  stateToString(current), stateToString(target));
        return SystemState::FATAL;
    }

    if (currentIndex < targetIndex) {
        return stateRoute[currentIndex + 1]; // Step up toward target
    }
    if (currentIndex > targetIndex) {
        return stateRoute[currentIndex - 1]; // Step down toward target
    }
    return current; // Already at target
}
```

Apply using two edits:
1. Insert Doxygen block between line 27 (blank line after `using` block) and line 28 (function signature)
2. Add inline comments on lines 29, 31, 35-36, 39-41, 49, 52, 54

- [ ] **Step 2: Run tests to verify no regression**

```bash
pio test -e native
```
Expected: All tests PASS.

- [ ] **Step 3: Commit**

```bash
git add src/supervisor/state_machine.cpp
git commit -m "step 4.1d: restore comments in state_machine.cpp (getNextState)"
```

---

### Task 2: Restore comment in `state_machine.cpp` — `checkComponentPresence()`

**Files:**
- Modify: `src/supervisor/state_machine.cpp:57`

- [ ] **Step 1: Add inline comment before `checkComponentPresence()` loop**

```cpp
void SupervisorV2::checkComponentPresence() {
    // Scan all registered components. Post an error for any required
    // component that never called registerComponent (null mailbox pointer).
    for (size_t i = 0; i < componentCount; i++) {
```

- [ ] **Step 2: Run tests to verify no regression**

```bash
pio test -e native
```
Expected: All tests PASS.

- [ ] **Step 3: Commit**

```bash
git add src/supervisor/state_machine.cpp
git commit -m "step 4.1d: restore comment for checkComponentPresence()"
```

---

### Task 3: Restore comments in `orchestrator.cpp` — `postNextComponentState()`

**Files:**
- Modify: `src/supervisor/orchestrator.cpp:3`

- [ ] **Step 1: Add inline comment before `postNextComponentState()`**

```cpp
void SupervisorV2::postNextComponentState(ComponentID id) {
    // Write the current stepping state to a component's mailbox under spinlock.
    // The component will read this in its own loop and react.
    ComponentMailbox* mailbox = componentMailboxes_[static_cast<int>(id)];
```

- [ ] **Step 2: Run tests to verify no regression**

```bash
pio test -e native
```
Expected: All tests PASS.

- [ ] **Step 3: Commit**

```bash
git add src/supervisor/orchestrator.cpp
git commit -m "step 4.1d: restore comment for postNextComponentState()"
```

---

### Task 4: Restore comments in `orchestrator.cpp` — `completeTransition()`

**Files:**
- Modify: `src/supervisor/orchestrator.cpp:12-23`

- [ ] **Step 1: Add multi-line comment to `completeTransition()`**

```cpp
void SupervisorV2::completeTransition(ComponentID id, TransitionStatus status) {
    if (status == TransitionStatus::Completed) {
        // Set this component's bit in the event group. The orchestration
        // completes when all required, non-degraded components have set
        // their bits — checked on each run() tick.
        xEventGroupSetBits(eventGroup_, 1 << static_cast<int>(id));
        return;
    }

    // Component reported Failed. How we handle it depends on whether this
    // component is required or optional:
    //   - Required: post an error event which the supervisor consumes on the
    //     next run() tick. This sets targetState_ to ERROR and aborts the
    //     current orchestration. The recovery logic then decides what to do.
    //   - Optional: mark as DEGRADED and exclude from the orchestration
    //     quorum. The remaining components are expected to finish normally.
    if (isRequired_[static_cast<int>(id)]) {
```

- [ ] **Step 2: Run tests to verify no regression**

```bash
pio test -e native
```
Expected: All tests PASS.

- [ ] **Step 3: Commit**

```bash
git add src/supervisor/orchestrator.cpp
git commit -m "step 4.1d: restore comments for completeTransition()"
```

---

### Task 5: Restore comments in `supervisor_v2.cpp` — `registerComponent()`

**Files:**
- Modify: `src/supervisor/supervisor_v2.cpp:42-45`

- [ ] **Step 1: Add inline comments to `registerComponent()`**

```cpp
void SupervisorV2::registerComponent(ComponentID id, ComponentMailbox* mailbox, bool isRequired) {
    // Store the mailbox pointer for cross-core writes. Null means absent.
    componentMailboxes_[static_cast<int>(id)] = mailbox;
    // Track required/optional for boot presence checks and failure handling.
    isRequired_[static_cast<int>(id)] = isRequired;
}
```

- [ ] **Step 2: Run tests to verify no regression**

```bash
pio test -e native
```
Expected: All tests PASS.

- [ ] **Step 3: Commit**

```bash
git add src/supervisor/supervisor_v2.cpp
git commit -m "step 4.1d: restore comments for registerComponent()"
```
