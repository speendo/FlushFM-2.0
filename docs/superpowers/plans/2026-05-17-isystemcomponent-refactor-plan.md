# ISystemComponent Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor `ISystemComponent` into a proper abstract base class that owns the mailbox, provides `loop()` dispatch, and defines a 9-method pure virtual contract for all concrete components.

**Architecture:** Move `ComponentMailbox` and `loop()` dispatch into the base class. Add `isRequired_` and a `mailbox()` accessor. Replace 4 per-component switch statements and `setOFF/setIDLE/setSTREAMING/setERROR` with 7 `handleX()` methods (one per `SystemState` value). Add `poll()` as an async hook. Add protected helpers `registerWithSupervisor()` and `completeTransition()`. Concrete components slim down to only `setup()` + the `handleX()`/`poll()` methods they actually need.

**Tech Stack:** C++20, ESP32-Arduino, FreeRTOS, Unity test framework

---

## File Structure

| File | Action | Responsibility |
|---|---|---|
| `src/components/composition/system_components.h` | Modify | New `ISystemComponent` base class; slim concrete class declarations |
| `src/components/composition/system_components.cpp` | Modify | Base class impl (`loop`, `dispatch`, helpers); concrete component `handleX()` bodies |
| `platformio.ini` | Modify | Add `-Isrc` to native test build flags |
| `test/test_component_interface/test_main.cpp` | Create | Compilation smoke test for the new interface |
| `src/main.cpp` | No change | Already iterates `ISystemComponent*` via `setup()` and `loop()` — still works |
| `src/component_types.h` | No change | SystemState enum unchanged |
| `src/supervisor/supervisor.h` | No change | Frozen API |

---

### Task 1: Add include path and create compilation smoke test

**Files:**
- Modify: `platformio.ini:62-67`
- Create: `test/test_component_interface/test_main.cpp`

- [x] **Step 1: Add `-Isrc` to native build flags**

- [x] **Step 2: Create compilation smoke test**

- [x] **Step 3: Run tests to verify the smoke test is accepted**

- [x] **Step 4: Commit**

- [x] **Step 1: Write the new `ISystemComponent` base class**

- [x] **Step 2: Declare concrete component classes**

- [x] **Step 3: Run compilation smoke test**

- [x] **Step 4: Commit**

- [x] **Step 1: Add base class `loop()` and `dispatch()` at the top of the file**

- [x] **Step 2: Remove old includes that won't be needed (keep for now — will clean up as concrete components are rewritten)**

- [x] **Step 3: Run existing tests to confirm no regression**

- [x] **Step 4: Commit**

- [x] **Step 1: Replace the old `BoardInfoComponent` constructor and methods**

- [x] **Step 2: Remove kBoardInfoTimeout constants from anonymous namespace**

- [x] **Step 3: Run compilation smoke test**

- [x] **Step 4: Commit**

- [x] **Step 1: Replace CliComponent methods**

- [x] **Step 2: Remove old CliComponent code from `.cpp`**

- [x] **Step 3: Run compilation smoke test**

- [x] **Step 4: Commit**

- [x] **Step 1: Replace the old `WiFiComponent` methods**

- [x] **Step 2: Remove old anonymous namespace WiFi timeout constants**

- [x] **Step 3: Run compilation smoke test**

- [x] **Step 4: Commit**

- [x] **Step 1: Replace the old `AudioRuntimeComponent` methods**

- [x] **Step 2: Remove old anonymous namespace Audio timeout constants**

- [x] **Step 3: Verify includes are still needed**

- [x] **Step 4: Run compilation smoke test**

- [x] **Step 5: Commit**

- [x] **Step 1: Verify the final `.cpp` is coherent**

- [x] **Step 2: Remove the old anonymous namespace constants**

- [x] **Step 3: Run full test suite**

- [x] **Step 4: Do a manual review of header and source**

- [x] **Step 5: Final commit**

```bash
git add src/components/composition/system_components.cpp src/components/composition/system_components.h
git commit -m "refactor: complete ISystemComponent refactor - remove old lifecycle methods"
```
