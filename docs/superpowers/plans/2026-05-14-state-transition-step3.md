# Step 3: Add spinlock guards to post/consume methods

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Protect the shared mailbox and error event structs against cross-core races by wrapping read/write access with the embedded spinlocks.

**Architecture:** `portENTER_CRITICAL(&struct.spinlock)`/`portEXIT_CRITICAL(...)` around all access to `stateRequestMailbox_` and `errorEvent_` in `postStateRequest`, `postErrorEvent`, `consumeStateRequest`, and `consumeErrorEvent`. On native the macros are no-ops, so thread safety testing requires hardware.

**Tech Stack:** C++17, FreeRTOS spinlocks, Unity test framework

---

### Files
- Modify: `src/state_machine/supervisor_v2.cpp`
- Create: `test/test_supervisor_v2_mailbox_spinlock/test_main.cpp`
- Modify: `platformio.ini` — add test_ignore while tests are red

---

### Task 3a: Write the test file

...

- [x] **Step: Add test_ignore to platformio.ini**
- [x] **Step: Create test file**

---

### Task 3b: Add spinlock guard to postStateRequest

...

- [x] **Step: Add spinlock to postStateRequest**

---

### Task 3c: Add spinlock guard to postErrorEvent

...

- [x] **Step: Add spinlock to postErrorEvent**

---

### Task 3d: Add spinlock guard to consumeStateRequest

...

- [x] **Step: Add spinlock to consumeStateRequest**

---

### Task 3e: Add spinlock guard to consumeErrorEvent

...

- [x] **Step: Add spinlock to consumeErrorEvent**

---

### Task 3f: Add consumeNextState method to ComponentMailbox

...

- [x] **Step: Add consumeNextState method to ComponentMailbox**

---

### Task 3g: Run tests, verify

- [x] **Step: Remove test_ignore from platformio.ini**
- [x] **Step: Run `pio test -e native --filter test_supervisor_v2_mailbox_spinlock`** — all 7 new tests pass
- [x] **Step: Run full suite** — `pio test -e native` — 90 succeeded, 4 pre-existing errors

---

### Verification

| Check | Command |
|-------|---------|
| New tests pass | `pio test -e native --filter test_supervisor_v2_mailbox_spinlock` |
| No regressions | Full suite — existing passes unchanged |
