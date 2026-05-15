# Step 9: Wire Up FreeRTOS State Machine Task in main.cpp

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create a FreeRTOS task pinned to Core 0 that runs the `SupervisorV2::run()` event loop. The task calls `setup()` (which stores its handle via `xTaskGetCurrentTaskHandle()` and spawns the orchestration worker), then enters an infinite `run()` loop.

**Architecture:** The new state machine task coexists with the existing `Supervisor` and component `loop()` calls. Both old and new supervisors run side by side until step 10 migrates components to the new mailbox pattern. The `main.cpp` file is hardware-only (`#include <Arduino.h>`) — not compiled in the native/test environment.

**Tech Stack:** Arduino framework, FreeRTOS (`xTaskCreatePinnedToCore`). No new files, no test file.

**Prerequisite:** Step 8 complete (145 passed, 4 pre-existing errors). SupervisorV2 and all its methods compiled and tested.

---

## File Structure

- **Modify:** `src/main.cpp` — add SupervisorV2 include, static instance, task entry function, task creation in setup()

---

### Task 9: Wire the state machine task

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 9.1: Add SupervisorV2 include and static instance**

Add after the existing `#include "components/composition/system_components.h"` (line 10):

```cpp
#include "components/composition/system_components.h"
#include "supervisor/supervisor_v2.h"
```

Add after the existing `static CliComponent s_cli(...)` (line 27):

```cpp
static CliComponent s_cli(s_audio, s_system);
static SupervisorV2 s_supervisorV2;
```

- [ ] **Step 9.2: Add the state machine task entry function**

Add before the `// --- Arduino entry points ---` comment (before line 36):

```cpp
// ---------------------------------------------------------------------------
// SupervisorV2 state machine task — pinned to Core 0
// ---------------------------------------------------------------------------

/** @brief FreeRTOS task entry point for the SupervisorV2 state machine.
 *
 *  This task owns the SupervisorV2 lifecycle. It calls setup() which:
 *    1. Creates the event group for component orchestration
 *    2. Stores this task's handle via xTaskGetCurrentTaskHandle() so that
 *       postStateRequest(), postErrorEvent(), and the orchestration worker
 *       can wake it via xTaskNotifyGive()
 *    3. Spawns the orchestration worker task (also Core 0)
 *
 *  After setup, the task enters an infinite loop calling run() which blocks
 *  on ulTaskNotifyTake(portMAX_DELAY) until woken by a notification.
 *
 *  @param param  Pointer to the SupervisorV2 instance (cast from void*).
 */
static void stateMachineTask(void* param) {
    auto* supervisorV2 = static_cast<SupervisorV2*>(param);
    supervisorV2->setup();
    for (;;) {
        supervisorV2->run();
    }
}
```

- [ ] **Step 9.3: Create the task in Arduino setup()**

Add at the end of the existing `setup()` function, after the component registration loop (after line 54):

```cpp
    for (ISystemComponent* component : s_components) {
        component->registerWithController(s_system);
    }

    (void)s_system.setup();

    // Create the SupervisorV2 state machine task, pinned to Core 0.
    // Priority 2 (default Arduino loop task priority) ensures the state
    // machine is responsive without starving the idle task. The orchestration
    // worker created inside setup() runs at priority 1 (below this task).
    // Stack size 8KB should be plenty for the event loop + mailbox access.
    xTaskCreatePinnedToCore(
        stateMachineTask,     // Task entry function
        "StateMachine",       // Human-readable name for debugging
        8192,                 // Stack size in bytes
        &s_supervisorV2,      // Parameter passed to the task
        2,                    // Priority (same as Arduino loop task)
        nullptr,              // Task handle (not needed — stored in setup())
        0                     // Core 0 — shared with orchestration worker
    );
```

- [ ] **Step 9.4: Build the production target to verify compilation**

Since main.cpp is hardware-only (not compiled in the native test environment), verify the production build compiles:

```bash
pio run -e production
```

Expected: BUILD SUCCESS. No linker errors (all SupervisorV2 methods defined). The build will show warnings about unused s_supervisorV2 from the old component code, which is expected until step 10.

- [ ] **Step 9.5: Run the full native test suite to verify no regressions**

The native test environment does not compile `main.cpp`, so SupervisorV2 changes should not affect existing tests:

```bash
pio test -e native
```

Expected: 145 succeeded, 4 pre-existing errors unchanged.

- [ ] **Step 9.6: Commit**

```bash
git add src/main.cpp
git commit -m "step 9: wire SupervisorV2 state machine task in main.cpp"
```
