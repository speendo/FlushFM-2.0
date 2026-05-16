# Timeout Clamp Fix — Implementation Plan (2026-05-16)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace absolute deadline storage with relative timeout duration in the orchestration order. Eliminates underflow, the 60s clamp that causes false timeouts for long deadlines, and tick-wrap concerns.

**Architecture:** The `OrchestrationOrder` struct changes from storing `deadlineTicks` (an absolute tick value = `now + timeout`) to `timeoutTicks` (a duration the worker waits from when it reads the order). `xTaskGetTickCount()` is removed from both `startOrchestration()` and the worker — FreeRTOS handles tick wrap internally in `xEventGroupWaitBits`.

**Tech Stack:** C++20, FreeRTOS, Unity, PlatformIO native.

---

### Task 1: Switch from absolute deadline to relative timeout

**Files:**
- Modify: `src/supervisor/orchestrator.h` — rename field, update post() and consume()
- Modify: `src/supervisor/orchestrator.cpp` — simplify startOrchestration and worker
- Modify: `test/test_supervisor_v2_orchestration/test_main.cpp` — update field name

- [ ] **Step 1: Rename field and update post()/consume() in the struct**

Edit `src/supervisor/orchestrator.h`. Change the `OrchestrationOrder` struct:

Field (line 31):
```cpp
    TickType_t deadlineTicks = 0;
```
→
```cpp
    TickType_t timeoutTicks = 0;
```

`post()` signature and body (lines 35-42):
```cpp
    void post(EventBits_t bits, TickType_t deadline, SystemState target) {
        portENTER_CRITICAL(&spinlock);
        expectedBits = bits;
        deadlineTicks = deadline;
        transitionTarget = target;
        pending = true;
        portEXIT_CRITICAL(&spinlock);
    }
```
→
```cpp
    void post(EventBits_t bits, TickType_t timeout, SystemState target) {
        portENTER_CRITICAL(&spinlock);
        expectedBits = bits;
        timeoutTicks = timeout;
        transitionTarget = target;
        pending = true;
        portEXIT_CRITICAL(&spinlock);
    }
```

`consume()` signature and body (lines 44-59): rename `outDeadline` to `outTimeout`, `deadlineTicks` to `timeoutTicks`:
```cpp
    bool consume(EventBits_t& outBits, TickType_t& outTimeout, SystemState& outTarget) {
        portENTER_CRITICAL(&spinlock);
        if (!pending) { portEXIT_CRITICAL(&spinlock); return false; }
        outBits = expectedBits;
        outTimeout = timeoutTicks;
        outTarget = transitionTarget;
        pending = false;
        portEXIT_CRITICAL(&spinlock);
        return true;
    }
```

- [ ] **Step 2: Simplify startOrchestration()**

Edit `src/supervisor/orchestrator.cpp`. Replace lines 98-101:
```cpp
    TickType_t timeoutTicks = pdMS_TO_TICKS(getTransitionTimeout(target,
        getIndex(target) > getIndex(observedState_)));
    TickType_t deadline = xTaskGetTickCount() + timeoutTicks;
    orderMailbox_.post(bits, deadline, target);
```
with:
```cpp
    orderMailbox_.post(bits,
        pdMS_TO_TICKS(getTransitionTimeout(target,
            getIndex(target) > getIndex(observedState_))),
        target);
```

- [ ] **Step 3: Simplify orchestrationWorker()**

Edit `src/supervisor/orchestrator.cpp`. Replace the worker variable declarations and wait computation (lines 143-158):
```cpp
        EventBits_t expectedBits;
        TickType_t deadlineTicks;
        SystemState transitionTarget;
        if (!supervisor->orderMailbox_.consume(expectedBits, deadlineTicks, transitionTarget)) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        TickType_t rawWait = deadlineTicks - xTaskGetTickCount();
        TickType_t waitTicks = (rawWait < pdMS_TO_TICKS(60000)) ? rawWait : 0;

        EventBits_t bits = xEventGroupWaitBits(supervisor->eventGroup_,
                                                 expectedBits,
                                                 pdTRUE,
                                                 pdTRUE,
                                                 waitTicks);
```
with:
```cpp
        EventBits_t expectedBits;
        TickType_t timeoutTicks;
        SystemState transitionTarget;
        if (!supervisor->orderMailbox_.consume(expectedBits, timeoutTicks, transitionTarget)) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        EventBits_t bits = xEventGroupWaitBits(supervisor->eventGroup_,
                                                 expectedBits,
                                                 pdTRUE,
                                                 pdTRUE,
                                                 timeoutTicks);
```

- [ ] **Step 4: Update test field name**

Edit `test/test_supervisor_v2_orchestration/test_main.cpp`. In `test_start_orchestration_sets_deadline_in_order`:
```cpp
    TEST_ASSERT_NOT_EQUAL(0, supervisor.orderMailbox_.deadlineTicks);
```
→
```cpp
    TEST_ASSERT_NOT_EQUAL(0, supervisor.orderMailbox_.timeoutTicks);
```

- [ ] **Step 5: Verify there are no remaining references to the old field name**

Run: `grep -r "deadlineTicks\|deadlineMs" src/ test/`
Expected: No output.

- [ ] **Step 6: Run tests**

Run: `/config/.platformio/penv/bin/platformio test -e native`
Expected: All 139 tests pass.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "fix: switch from absolute deadline to relative timeout — eliminates underflow and 60s clamp false timeouts"
```

---
