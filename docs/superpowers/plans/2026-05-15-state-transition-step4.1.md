# Step 4.1: Add Orchestration Types and Members to SupervisorV2 (Structural Split)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prepare `supervisor_v2.h` for the split-task orchestration architecture by adding `OrchestrationOrder`/`OrchestrationResponse` types, replacing polling member variables and method declarations with their split-task equivalents, and adding native stubs for worker task creation.

**Architecture:** Pure structural changes — no new behavior. The `OrchestrationOrder` and `OrchestrationResponse` structs with embedded spinlocks replace `expectedBits_`/`orchestrationDeadlineMs_`. `checkOrchestrationCompletion()` and `checkStateTimeout()` declarations are replaced by `checkOrchestrationResponse()`. A `friend` declaration grants the `orchestrationWorker` free function access to private members.

**Tech Stack:** C++17, PlatformIO native, all changes to `supervisor_v2.h` and `supervisor_v2.cpp` only

---

### File Structure

- **Modify:** `src/state_machine/supervisor_v2.h` — add `OrchestrationResult`, `OrchestrationOrder`, `OrchestrationResponse` structs; remove `expectedBits_`/`orchestrationDeadlineMs_`; add `orderMailbox_`/`responseMailbox_`/`workerTaskHandle_`; replace method declarations; add friend; add native stubs
- **Modify:** `src/state_machine/supervisor_v2.cpp` — no changes needed (methods not yet defined)

---

### Task 4.1a: Add orchestrator structs to supervisor_v2.h

**Files:** Modify `src/state_machine/supervisor_v2.h`

- [ ] **Step 4.1a.1: Add `OrchestrationResult`, `OrchestrationOrder`, `OrchestrationResponse`**

Add after the `ErrorEvent` struct definition (after line 136):

```cpp
enum class OrchestrationResult : uint8_t {
    COMPLETED,
    TIMED_OUT
};

struct OrchestrationOrder {
    bool pending = false;
    EventBits_t expectedBits = 0;
    TickType_t deadlineMs = 0;
    SystemState transitionTarget;
    portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;

    void post(EventBits_t bits, TickType_t deadline, SystemState target) {
        portENTER_CRITICAL(&spinlock);
        expectedBits = bits;
        deadlineMs = deadline;
        transitionTarget = target;
        pending = true;
        portEXIT_CRITICAL(&spinlock);
    }

    bool consume(EventBits_t& outBits, TickType_t& outDeadline, SystemState& outTarget) {
        portENTER_CRITICAL(&spinlock);
        if (!pending) { portEXIT_CRITICAL(&spinlock); return false; }
        outBits = expectedBits;
        outDeadline = deadlineMs;
        outTarget = transitionTarget;
        pending = false;
        portEXIT_CRITICAL(&spinlock);
        return true;
    }
};

struct OrchestrationResponse {
    bool pending = false;
    OrchestrationResult result = OrchestrationResult::COMPLETED;
    EventBits_t timedOutComponents = 0;
    portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;

    void post(OrchestrationResult r, EventBits_t timedOut = 0) {
        portENTER_CRITICAL(&spinlock);
        result = r;
        timedOutComponents = timedOut;
        pending = true;
        portEXIT_CRITICAL(&spinlock);
    }

    bool consume(OrchestrationResult& outResult, EventBits_t& outTimedOut) {
        portENTER_CRITICAL(&spinlock);
        if (!pending) { portEXIT_CRITICAL(&spinlock); return false; }
        outResult = result;
        outTimedOut = timedOutComponents;
        pending = false;
        portEXIT_CRITICAL(&spinlock);
        return true;
    }
};
```

- [ ] **Step 4.1a.2: Verify compilation**

```bash
pio run -e native
```

Expected: SUCCESS (structs declared but not used, no regressions)

- [ ] **Step 4.1a.3: Commit**

```bash
git add src/state_machine/supervisor_v2.h
git commit -m "step 4.1a: add OrchestrationOrder/Response/Result structs"
```

---

### Task 4.1b: Replace polling members/methods with split-task equivalents

**Files:** Modify `src/state_machine/supervisor_v2.h`

- [ ] **Step 4.1b.1: Remove old member variables, add new ones**

In the private section of `SupervisorV2`, remove:
```cpp
    TickType_t orchestrationDeadlineMs_{};
```
and
```cpp
    EventBits_t expectedBits_{};
```

Add these members after `hasActiveOrchestration_`:
```cpp
    OrchestrationOrder orderMailbox_{};
    OrchestrationResponse responseMailbox_{};
    TaskHandle_t workerTaskHandle_{};
```

(Note: `hasActiveOrchestration_` stays. Only the deadline and bit members are replaced.)

- [ ] **Step 4.1b.2: Replace method declarations**

Remove:
```cpp
    void checkOrchestrationCompletion();
    void checkStateTimeout();
```

Replace with:
```cpp
    void checkOrchestrationResponse();
```

The `startOrchestration()` declaration stays unchanged (signature is the same).

- [ ] **Step 4.1b.3: Add friend declaration**

Add before the closing `};` of class `SupervisorV2`:
```cpp
    friend void orchestrationWorker(void* param);
```

- [ ] **Step 4.1b.4: Verify compilation and run full suite**

```bash
pio test -e native
```

Expected: 95 succeeded. 4 pre-existing errors unchanged. No new compile errors (members are declared but unused; old members removed but only referenced in not-yet-implemented methods).

- [ ] **Step 4.1b.5: Commit**

```bash
git add src/state_machine/supervisor_v2.h
git commit -m "step 4.1b: replace polling members with order/response mailboxes, update method declarations"
```

---

### Task 4.1c: Add native stubs for worker task creation

**Files:** Modify `src/state_machine/supervisor_v2.h`

- [ ] **Step 4.1c.1: Add `TaskHandle_t` and `xTaskCreatePinnedToCore` to native stubs**

Under the `#if !defined(ARDUINO)` block, after the existing `using` lines, add:

```cpp
using TaskHandle_t = void*;
inline void xTaskCreatePinnedToCore(void (*task)(void*), const char*, uint32_t,
                                     void* param, uint32_t, TaskHandle_t*, int) {
    // No-op on native — worker task is not tested on native
}
```

Also add `vTaskDelay` and `pdMS_TO_TICKS` stubs for the worker function:

```cpp
inline constexpr TickType_t pdMS_TO_TICKS(TickType_t ms) { return ms; }
inline void vTaskDelay(TickType_t) { /* no-op on native */ }
```

- [ ] **Step 4.1c.2: Verify compilation**

```bash
pio run -e native
```

Expected: SUCCESS

- [ ] **Step 4.1c.3: Commit**

```bash
git add src/state_machine/supervisor_v2.h
git commit -m "step 4.1c: add native stubs for TaskHandle_t, xTaskCreatePinnedToCore, vTaskDelay"
```
