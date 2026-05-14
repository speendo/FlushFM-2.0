# Step 4: Guard getNextState against FATAL as current state

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `getNextState(FATAL, X)` return FATAL for any target, preventing the recovery path from being reached when the system is in FATAL.

**Architecture:** Add an explicit early-return at the top of `getNextState()`: `if (current == SystemState::FATAL) return SystemState::FATAL;`. The `run()` method already guards against calling `getNextState` when in FATAL (belt-and-suspenders from the `run()` logic), so this is a defensive lower-layer guard.

**Tech Stack:** C++17, Unity test framework

---

### Files
- Modify: `src/state_machine/supervisor_v2.cpp` — one-line addition to `getNextState()`
- Create: `test/test_supervisor_v2_get_next_state/test_main.cpp`
- Modify: `platformio.ini` — add test_ignore while test is red

---

### Current bug

`getNextState(FATAL, READY)` currently returns `BOOTING` because the recovery path (`if (isErrorState(current) && target != SLEEP)`) fires for FATAL. `getNextState(FATAL, SLEEP)` returns `ERROR` because it falls through to rank-based stepping. Both are wrong — FATAL must be absorbent.

---

### Task 4a: Write the failing test

Create `test/test_supervisor_v2_get_next_state/test_main.cpp`:

```cpp
#include <unity.h>

#include "../../src/state_machine/supervisor_v2.h"

void test_get_next_state_fatal_absorbent() {
    // From FATAL, any target should return FATAL — no stepping out
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL),
                      static_cast<int>(getNextState(SystemState::FATAL, SystemState::SLEEP)));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL),
                      static_cast<int>(getNextState(SystemState::FATAL, SystemState::READY)));
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL),
                      static_cast<int>(getNextState(SystemState::FATAL, SystemState::LIVE)));
}

void test_get_next_state_fatal_to_fatal() {
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL),
                      static_cast<int>(getNextState(SystemState::FATAL, SystemState::FATAL)));
}

void test_get_next_state_fatal_to_error() {
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::FATAL),
                      static_cast<int>(getNextState(SystemState::FATAL, SystemState::ERROR)));
}

void test_get_next_state_error_recovery_toward_live() {
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::BOOTING),
                      static_cast<int>(getNextState(SystemState::ERROR, SystemState::LIVE)));
}

void test_get_next_state_error_to_sleep() {
    TEST_ASSERT_EQUAL(static_cast<int>(SystemState::SLEEP),
                      static_cast<int>(getNextState(SystemState::ERROR, SystemState::SLEEP)));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_get_next_state_fatal_absorbent);
    RUN_TEST(test_get_next_state_fatal_to_fatal);
    RUN_TEST(test_get_next_state_fatal_to_error);
    RUN_TEST(test_get_next_state_error_recovery_toward_live);
    RUN_TEST(test_get_next_state_error_to_sleep);
    return UNITY_END();
}
```

Note: includes `supervisor_v2.h` only (not .cpp). The `getNextState` is a free function declared in the header, testable without instantiating `SupervisorV2`.

Add to `platformio.ini`:
```ini
test_ignore = test_supervisor_v2_get_next_state
```

- [x] **Step: Add test_ignore to platformio.ini**
- [x] **Step: Create test file, expect failure**

---

### Task 4b: Add FATAL guard and comments to getNextState()

- [x] **Step: Replace getNextState() with commented version**

---

### Task 4c: Run tests, verify

- [x] **Step: Remove test_ignore from platformio.ini**
- [x] **Step: Run `pio test -e native --filter test_supervisor_v2_get_next_state`** — all 5 tests pass
- [x] **Step: Run full suite** — `pio test -e native` — 95 succeeded, 4 pre-existing errors

---

### Verification

| Check | Command |
|-------|---------|
| New tests pass | `pio test -e native --filter test_supervisor_v2_get_next_state` |
| No regressions | Full suite — existing passes unchanged |
| FATAL absorbent | `getNextState(FATAL, ANY) == FATAL` |
| ERROR recovery unaffected | `getNextState(ERROR, LIVE) == BOOTING`, `getNextState(ERROR, SLEEP) == SLEEP` |
