# Cleanup: Move Orchestration Types to orchestrator.h

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Extract `OrchestrationResult`, `OrchestrationOrder`, and `OrchestrationResponse` from `supervisor_v2.h` into `orchestrator.h`, making the orchestrator header a real file with its own types. Keep `state_machine.h` as a thin wrapper.

**Architecture:** `orchestrator.h` becomes a standalone header that includes only `component_types.h` and the FreeRTOS stubs. `supervisor_v2.h` includes `orchestrator.h` to access the types for its member declarations. This removes ~70 lines from `supervisor_v2.h` and gives `orchestrator.h` a clear responsibility: the types and API that the orchestration worker and component-completion system use.

**Tech Stack:** C++17, PlatformIO native, Unity test framework.

**Prerequisite:** Step 10 complete (production build succeeds, 145 passed on native).

---

## File Structure

- **Modify:** `src/supervisor/orchestrator.h` — rewrite from 3-line passthrough to standalone header with `OrchestrationResult`, `OrchestrationOrder`, `OrchestrationResponse`
- **Modify:** `src/supervisor/supervisor_v2.h` — remove the three types, add `#include "orchestrator.h"`
- **Modify:** `src/supervisor/orchestrator.cpp` — add `#include "orchestrator.h"` alongside `supervisor_v2.h`
- **Modify:** `src/supervisor/state_machine.h` — verify it still works as a thin wrapper (no changes needed)
- **Modify:** ALL `.cpp` files in `test/` and `src/` that include `supervisor_v2.h` — verify they still compile (no changes expected: everything is available transitively)

---

### Task 1: Build orchestrator.h as a standalone header

**Files:**
- Modify: `src/supervisor/orchestrator.h`

- [x] **Step 1.1: Read the current orchestrator.h**

```bash
cat src/supervisor/orchestrator.h
```

Expected output:
```cpp
#include "supervisor/supervisor_v2.h"
```

- [x] **Step 1.2: Read the three types from supervisor_v2.h to know exactly what to extract**

The types to move are defined in `src/supervisor/supervisor_v2.h`:

1. `OrchestrationResult` enum (lines ~139–142)
2. `OrchestrationOrder` struct (lines ~148–174) with `post()` and `consume()` methods
3. `OrchestrationResponse` struct (lines ~179–203) with `post()` and `consume()` methods

Each uses types that must be available in `orchestrator.h`:
- `uint8_t` from `<cstdint>`
- `EventBits_t` from FreeRTOS or `native_stubs.h`
- `TickType_t` from FreeRTOS or `native_stubs.h`
- `SystemState` from `component_types.h`
- `portMUX_TYPE`, `portMUX_INITIALIZER_UNLOCKED`, `portENTER_CRITICAL`, `portEXIT_CRITICAL` from FreeRTOS or stub macros in `component_types.h`

The non-ARDUINO stubs are already defined in `native_stubs.h`. To avoid duplicating them, `orchestrator.h` will include `native_stubs.h` on native builds instead of defining parallel `using` aliases.

- [x] **Step 1.3: Rewrite orchestrator.h**

Replace the entire file with:

```cpp
#pragma once

#include <cstdint>
#include <cstddef>

#if defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#else
#include "native_stubs.h"
#endif

#include "component_types.h"
```

- [x] **Step 1.4: Commit**

```bash
git add src/supervisor/orchestrator.h
git commit -m "refactor: extract OrchestrationResult/Order/Response into orchestrator.h"
```

---

### Task 2: Remove the three types from supervisor_v2.h, add include

**Files:**
- Modify: `src/supervisor/supervisor_v2.h`

- [x] **Step 2.1: Remove `OrchestrationResult` enum**

Remove lines ~139–142:
```cpp
/** @brief Result of an orchestration attempt.
 *  COMPLETED: all required bits were set before the deadline.
 *  TIMED_OUT: the deadline elapsed with bits still missing.
 */
enum class OrchestrationResult : uint8_t {
    COMPLETED,
    TIMED_OUT
};
```

- [x] **Step 2.2: Remove `OrchestrationOrder` struct**

Remove from the `#endif` after the `ErrorEvent` struct to just before the `OrchestrationResponse` struct. This is all lines related to `OrchestrationOrder`.

- [x] **Step 2.3: Remove `OrchestrationResponse` struct**

Remove the `OrchestrationResponse` struct definition, from its opening comment to its closing brace.

- [x] **Step 2.4: Add `#include "orchestrator.h"`**

Add after the `#include "native_stubs.h"` line in the `#if defined(ARDUINO) ... #else ... #endif` block:

```cpp
#else
#include "native_stubs.h"
#endif

#include "orchestrator.h"
```

- [x] **Step 2.5: Build and test**

```bash
pio run -e production
pio test -e native
```

Expected: Both succeed. The types are now available via the new include.

- [x] **Step 2.6: Commit**

```bash
git add src/supervisor/supervisor_v2.h
git commit -m "refactor: supervisor_v2.h includes orchestrator.h instead of defining types inline"
```

---

### Task 3: Update orchestrator.cpp include

**Files:**
- Modify: `src/supervisor/orchestrator.cpp`

- [x] **Step 3.1: Remove the old include of orchestrator.h**

Currently `orchestrator.cpp` includes `"supervisor/orchestrator.h"` which is the OLD 3-line wrapper that just included `supervisor_v2.h`. Now `orchestrator.h` is a real header. Add it explicitly:

In `orchestrator.cpp`, after the existing `#include "supervisor/supervisor_v2.h"`, add:

```cpp
#include "supervisor/supervisor_v2.h"
#include "supervisor/orchestrator.h"
```

(Note: the order doesn't matter — `orchestrator.h` has no dependency on `supervisor_v2.h`.)

- [x] **Step 3.2: Build and test**

```bash
pio test -e native
```

Expected: 145 succeeded.

- [x] **Step 3.3: Commit**

```bash
git add src/supervisor/orchestrator.cpp
git commit -m "refactor: orchestrator.cpp includes orchestrator.h explicitly"
```

---

### Task 4: Verify state_machine.h is unchanged

**Files:**
- Read: `src/supervisor/state_machine.h`

- [x] **Step 4.1: Confirm state_machine.h is still a thin wrapper**

```bash
cat src/supervisor/state_machine.h
```

Expected:
```cpp
#include "supervisor/supervisor_v2.h"
```

This file stays as-is. No changes needed.

- [x] **Step 4.2: No commit needed**

---

### Task 5: Final verification

- [x] **Step 5.1: Full build and test**

```bash
pio run -e production
pio test -e native
```

Expected: Production build succeeds, 145 native tests pass, 4 pre-existing errors.

- [x] **Step 5.2: Final commit**

```bash
git commit --allow-empty -m "refactor: final verification after orchestrator.h extraction"
```
