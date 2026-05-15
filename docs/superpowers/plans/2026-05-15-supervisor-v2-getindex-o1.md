# SupervisorV2 getIndex() O(1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the O(n) linear-scan `getIndex()` in SupervisorV2 with an O(1) compile-time lookup table built from the SYSTEM_STATE_X macro.

**Architecture:** A constexpr `detail` namespace in the header extracts state rank values from the X-macro at compile time, builds a `kRankTable[]` indexed by rank value (0, 10, 20...), and `getIndex()` reads it with a single array load. Backward-compatible aliases (`stateRoute`, `stateCount`) keep the `.cpp` and existing tests unchanged.

**Tech Stack:** C++17 constexpr, PlatformIO, Unity test framework

---

### File Structure

| File | Responsibility |
|------|---------------|
| `src/state_machine/supervisor_v2.h` | Single source of truth for states. Generates enum, rank lookup table, route array, count. Exposes `constexpr int getIndex(SystemState)` and backward-compat aliases. |
| `src/state_machine/supervisor_v2.cpp` | Removes old `static int getIndex()`. Uses header-provided `getIndex()`, `stateRoute`, `stateCount` unchanged. |
| `test/test_supervisor_v2_get_next_state/test_main.cpp` | Unchanged — tests `getNextState()` which transitively uses `getIndex()`. Optionally add a direct `getIndex()` unit test. |
| `test/test_supervisor_v2_mailbox_spinlock/test_main.cpp` | No changes needed — doesn't use `getIndex()` directly. |
| `test/test_supervisor_v2_registration/test_main.cpp` | No changes needed — doesn't use `getIndex()` directly. |

---

### Task 1: Add compile-time rank table to header

**Files:**
- Modify: `src/state_machine/supervisor_v2.h:53-62`

Replace the `SYSTEM_STATE_ARRAY` / `stateRoute[]` / `stateCount` section with a constexpr lookup table infrastructure. Keep `SYSTEM_STATE_X` and the enum generation as-is. Add a `detail` namespace right after the enum, then reinstate `stateRoute` and `stateCount` as backward-compat aliases.

- [ ] **Step 1: Read current lines 53-62 to confirm the exact text to replace**

Current content (lines 53-62):
```cpp
/* Generate the array of state names */
#define SYSTEM_STATE_ARRAY(name, value) SystemState::name,

const SystemState stateRoute[] = {
	SYSTEM_STATE_X(SYSTEM_STATE_ARRAY)
};

#undef SYSTEM_STATE_ARRAY

constexpr size_t stateCount = sizeof(stateRoute) / sizeof(SystemState);
```

- [ ] **Step 2: Replace with constexpr rank table infrastructure**

Replace lines 53-62 with:
```cpp
namespace detail {

    /* Rank values in declaration order — extracted at compile time */
    constexpr uint8_t kValues[] = {
    #define X(name, value) value,
        SYSTEM_STATE_X(X)
    #undef X
    };

    /* State enum values in declaration order — replaces old stateRoute[] */
    constexpr SystemState kRoute[] = {
    #define X(name, value) SystemState::name,
        SYSTEM_STATE_X(X)
    #undef X
    };

    constexpr uint8_t kCount = sizeof(kValues) / sizeof(kValues[0]);

    /* Maximum rank value, determines lookup table size */
    constexpr uint8_t kMaxValue = [] {
        uint8_t m = 0;
        for (auto v : kValues) if (v > m) m = v;
        return m;
    }();

    /* Build a table: index by uint8_t rank value → positional index (or -1) */
    constexpr auto buildRankTable() {
        std::array<int, kMaxValue + 1> t{};
        for (auto& v : t) v = -1;
        for (uint8_t i = 0; i < kCount; ++i)
            t[kValues[i]] = static_cast<int>(i);
        return t;
    }

    constexpr auto kRankTable = buildRankTable();

}  // namespace detail

/* Backward-compatible aliases — .cpp and existing tests use these unchanged */
constexpr auto& stateRoute = detail::kRoute;
constexpr size_t stateCount = detail::kCount;
```

- [ ] **Step 3: Add `constexpr int getIndex(SystemState)` after the aliases**

Insert right after the `stateCount` alias:
```cpp
/** @brief O(1) lookup: state rank value → positional index.
 *  @param state The system state to look up.
 *  @return Index (0, 1, 2...) or -1 if the rank value has no mapping.
 */
inline constexpr int getIndex(SystemState state) {
    const uint8_t raw = static_cast<uint8_t>(state);
    if (raw > detail::kMaxValue) return -1;
    return detail::kRankTable[raw];
}
```

Note: the `if (raw > detail::kMaxValue)` guard catches rank values that are outside the table (e.g. uninitialized `uint8_t`), returning -1 like the old implementation did.

- [ ] **Step 4: Build to verify header compiles**

Run: `/config/.platformio/penv/bin/platformio test -e native -f test_supervisor_v2_get_next_state`
Expected: COMPILATION FAILED (getIndex() now exists in header but .cpp still defines its own `static int getIndex()` — duplicate symbol error, or compilation warning depending on linkage)

- [ ] **Step 5: Commit**

```bash
git add src/state_machine/supervisor_v2.h
git commit -m "v2: add compile-time rank lookup table to header"
```

---

### Task 2: Remove old `static int getIndex()` from .cpp

**Files:**
- Modify: `src/state_machine/supervisor_v2.cpp:11-20`

Delete the O(n) `getIndex()` function. The header's `constexpr int getIndex()` is now visible via `#include "state_machine/supervisor_v2.h"`. No other changes needed — `stateRoute` and `stateCount` in the `.cpp` resolve to the header aliases.

- [ ] **Step 1: Delete lines 11-20 from `supervisor_v2.cpp`**

Remove:
```cpp
/** @brief Get the index of a system state in the state route array.
 *  @param state The system state to find.
 *  @return The index of the state, or -1 if not found.
 */
static int getIndex(SystemState state) {
    for (size_t i = 0; i < stateCount; ++i) {
        if (stateRoute[i] == state) return static_cast<int>(i);
    }
    return -1; // Invalid state
}
```

This deletes 10 lines. Delete exactly the old function including its doc comment.

- [ ] **Step 2: Verify nothing else in the .cpp references `getIndex` via the old `static` linkage**

Check: `getIndex` is called in `getNextState()` (lines 62-63) and `getTransitionTimeout()` (line 108). These are free function / member function calls, not `detail::getIndex` — they resolve to the `constexpr int getIndex(SystemState)` from the header since the old `static` one is gone. No changes needed at call sites.

- [ ] **Step 3: Run all V2 tests to verify**

Run: `/config/.platformio/penv/bin/platformio test -e native -f test_supervisor_v2_get_next_state -f test_supervisor_v2_mailbox_spinlock -f test_supervisor_v2_registration`
Expected: All 3 test suites PASS

- [ ] **Step 4: Commit**

```bash
git add src/state_machine/supervisor_v2.cpp
git commit -m "v2: remove O(n) getIndex(), use constexpr lookup from header"
```

---

### Task 3: Add unit test for `getIndex()` (optional but recommended)

**Files:**
- Modify: `test/test_supervisor_v2_get_next_state/test_main.cpp`

Add direct tests for `getIndex()` to cover edge cases the `getNextState()` tests don't explicitly exercise (boundary values, invalid states).

- [ ] **Step 1: Add `test_get_index_valid_states` test**

Insert before `int main()`:
```cpp
void test_get_index_valid_states() {
    TEST_ASSERT_EQUAL_INT(0, getIndex(SystemState::FATAL));
    TEST_ASSERT_EQUAL_INT(1, getIndex(SystemState::ERROR));
    TEST_ASSERT_EQUAL_INT(2, getIndex(SystemState::SLEEP));
    TEST_ASSERT_EQUAL_INT(3, getIndex(SystemState::BOOTING));
    TEST_ASSERT_EQUAL_INT(4, getIndex(SystemState::CONNECTING));
    TEST_ASSERT_EQUAL_INT(5, getIndex(SystemState::READY));
    TEST_ASSERT_EQUAL_INT(6, getIndex(SystemState::LIVE));
}
```

- [ ] **Step 2: Add `test_get_index_invalid_returns_negative_one` test**

Insert before `int main()`:
```cpp
void test_get_index_invalid_returns_negative_one() {
    // Any uint8_t value that doesn't match a defined state
    TEST_ASSERT_EQUAL_INT(-1, getIndex(static_cast<SystemState>(1)));
    TEST_ASSERT_EQUAL_INT(-1, getIndex(static_cast<SystemState>(42)));
    TEST_ASSERT_EQUAL_INT(-1, getIndex(static_cast<SystemState>(100)));
    TEST_ASSERT_EQUAL_INT(-1, getIndex(static_cast<SystemState>(255)));
}
```

- [ ] **Step 3: Register both tests in `main()`**

Update `main()` to add the new tests before the existing ones:
```cpp
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_get_index_valid_states);
    RUN_TEST(test_get_index_invalid_returns_negative_one);
    RUN_TEST(test_get_next_state_fatal_absorbent);
    RUN_TEST(test_get_next_state_fatal_to_fatal);
    RUN_TEST(test_get_next_state_fatal_to_error);
    RUN_TEST(test_get_next_state_error_recovery_toward_live);
    RUN_TEST(test_get_next_state_error_to_sleep);
    return UNITY_END();
}
```

- [ ] **Step 4: Run all V2 tests to verify**

Run: `/config/.platformio/penv/bin/platformio test -e native -f test_supervisor_v2_get_next_state -f test_supervisor_v2_mailbox_spinlock -f test_supervisor_v2_registration`
Expected: All suites PASS (7 tests total in get_next_state suite)

- [ ] **Step 5: Commit**

```bash
git add test/test_supervisor_v2_get_next_state/test_main.cpp
git commit -m "v2: add unit tests for getIndex() O(1) lookup"
```

---

### Task 4: Run full test suite to confirm no regressions

**Files:** (none)

Run the complete native test suite to ensure the changes don't break anything else (e.g. old Supervisor tests that might share state types).

- [ ] **Step 1: Run full test suite**

Run: `/config/.platformio/penv/bin/platformio test -e native`
Expected: 99+ test cases, same pass/fail distribution as before (the old Supervisor's 4 pre-existing failures should still fail, all V2 tests should pass)

- [ ] **Step 2: Commit if any fixes were needed, otherwise confirm clean**

```bash
git log --oneline -3
```

---
