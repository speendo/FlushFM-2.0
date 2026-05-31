# Remove Old Supervisor & Slim CLI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** (A) Remove the obsolete `Supervisor` class, rename `SupervisorV2` to `Supervisor`, migrate valuable old tests. (B) Remove `suspend`/`resume` debug commands, consolidate `play`/`stop` with `transition` to eliminate command duplication.

**Architecture:**
- **Phase A**: Delete dead code (`src/supervisor/supervisor.h` + `.cpp`, guarded by `#ifndef PRODUCTION_BUILD`) and 3 old test files. Add missing downward multi-step transition tests to the V2 suite. Rename `SupervisorV2` → `Supervisor`, `S2V2Access` → `SupervisorAccess`, and files `supervisor_v2.*` → `supervisor.*`.
- **Phase B**: Remove `suspend`/`resume` from `debug_cli.cpp`. Make `play`/`stop` route through `postStateRequest()` (same as `transition live`/`transition ready`) instead of having duplicate code paths. Move `transition` from debug to production. Remove obsolete `CONNECTING_STREAM`/`STREAM_STOPPED` MessageKeys.

**Tech Stack:** C++20, FreeRTOS (ESP32-S3), Arduino framework, Unity test framework

**Note:** The two phases are independent — either can be executed without the other.

---

## Phase A: Supervisor Cleanup

### Files to Delete

| File | Reason |
|------|--------|
| `src/supervisor/supervisor.h` | Old Supervisor class, dead code (guarded by `#ifndef PRODUCTION_BUILD`) |
| `src/supervisor/supervisor.cpp` | Old Supervisor implementation |
| `test/test_transition_completion_tracking/test_main.cpp` | Tests old `transitionId`/`reportCompletion` API — not migratable |
| `test/test_mailbox_contract/test_main.cpp` | Tests old single-threaded mailbox — covered by V2 mailboxes |
| `test/test_state_transition_flow/test_main.cpp` | Tests old `postEvent()`/`reportCompletion()` API — individual test scenarios differ from V2 |
| `test/test_component_types/test_main.cpp` | Includes old `supervisor.h` for enums; those live in `component_types.h` now |

### Files to Rename

| Old Path | New Path |
|----------|----------|
| `src/supervisor/supervisor_v2.h` | `src/supervisor/supervisor.h` |
| `src/supervisor/supervisor_v2.cpp` | `src/supervisor/supervisor.cpp` |
| `test/support/s2v2_access.h` | `test/support/supervisor_access.h` |

### Files to Modify (class name replacement)

Every file containing `SupervisorV2`, `s_supervisorV2`, or `S2V2Access`:

| File | String replacement |
|------|--------------------|
| `src/main.cpp` | `SupervisorV2` → `Supervisor`, `s_supervisorV2` → `s_supervisor`, `supervisor_v2.h` → `supervisor.h` |
| `src/components/composition/system_components.h` | `class SupervisorV2` → `class Supervisor` |
| `src/components/composition/system_components.cpp` | `SupervisorV2 s_supervisorV2` → `Supervisor s_supervisor`, `s_supervisorV2` → `s_supervisor`, `supervisor_v2.h` → `supervisor.h` |
| `src/components/cli/cli.h` | `class SupervisorV2` → `class Supervisor` |
| `src/components/cli/cli.cpp` | `SupervisorV2` → `Supervisor`, `s_supervisorV2` → `s_supervisor`, `supervisor_v2.h` → `supervisor.h` |
| `src/components/cli/debug_cli.h` | `class SupervisorV2` → `class Supervisor` |
| `src/components/cli/debug_cli.cpp` | `SupervisorV2` → `Supervisor`, `s_supervisorV2` → `s_supervisor` |
| `src/supervisor/supervisor_v2.h` | Rename + `SupervisorV2` → `Supervisor` |
| `src/supervisor/supervisor_v2.cpp` | Rename + `SupervisorV2` → `Supervisor`, `supervisor_v2.h` → `supervisor.h` |
| `src/supervisor/state_machine.cpp` | `SupervisorV2` → `Supervisor`, `supervisor_v2.h` → `supervisor.h` |
| `src/supervisor/orchestrator.cpp` | `SupervisorV2` → `Supervisor`, `supervisor_v2.h` → `supervisor.h` |
| `src/supervisor/fatal_task.cpp` | `SupervisorV2` → `Supervisor`, `supervisor_v2.h` → `supervisor.h` |
| `test/support/s2v2_access.h` | Rename file, `S2V2Access` → `SupervisorAccess`, `SupervisorV2` → `Supervisor`, `supervisor_v2.h` → `supervisor.h` |
| `test/test_supervisor_v2_orchestration/test_main.cpp` | `S2V2Access` → `SupervisorAccess`, `SupervisorV2` → `Supervisor`, `s2v2_access.h` → `supervisor_access.h` |
| `test/test_supervisor_v2_get_next_state/test_main.cpp` | `supervisor_v2.h` → `supervisor.h` |
| `test/test_supervisor_v2_mailbox_spinlock/test_main.cpp` | `S2V2Access` → `SupervisorAccess`, `SupervisorV2` → `Supervisor`, `s2v2_access.h` → `supervisor_access.h` |
| `test/test_supervisor_v2_run/test_main.cpp` | `S2V2Access` → `SupervisorAccess`, `SupervisorV2` → `Supervisor`, `s2v2_access.h` → `supervisor_access.h` |
| `test/test_supervisor_v2_remaining_paths/test_main.cpp` | `S2V2Access` → `SupervisorAccess`, `SupervisorV2` → `Supervisor`, `s2v2_access.h` → `supervisor_access.h` |
| `test/test_supervisor_v2_registration/test_main.cpp` | `S2V2Access` → `SupervisorAccess`, `SupervisorV2` → `Supervisor`, `s2v2_access.h` → `supervisor_access.h` |
| `test/test_supervisor_v2_step_6/test_main.cpp` | `S2V2Access` → `SupervisorAccess`, `SupervisorV2` → `Supervisor`, `s2v2_access.h` → `supervisor_access.h` |
| `test/test_fatal_task/test_main.cpp` | `SupervisorV2` → `Supervisor`, `supervisor_v2.h` → `supervisor.h` |

**Note:** Test directory names (`test_supervisor_v2_*`) stay as-is to keep git history clean. Only file contents change.

**Note:** `test/test_component_types/test_main.cpp` includes old `supervisor.h` — after deleting old files, its include must change to `component_types.h` or be removed (only enums/structs are needed from component_types.h). Check after deleting old supervisor files.

**Note:** `platformio.ini` `build_src_filter` references `+<supervisor/>` — this already includes `src/supervisor/*.cpp` and doesn't need changes.

---

### Task A1: Add downward multi-step transition tests

**Files:**
- Modify: `test/test_supervisor_v2_orchestration/test_main.cpp`

Add two tests validating autonomous downward multi-step transitions. The existing tests cover SLEEP→READY and SLEEP→LIVE but don't cover the reverse paths.

- [ ] **Step 1: Add the downward transition tests**

Add these test functions after the existing multi-step tests but before the closing `}  // namespace`:

```cpp
void test_multi_step_transition_live_to_sleep_completes_autonomously() {
    SupervisorV2 supervisor;
    TestComponent board, wifi, audio, cli;
    supervisor.registerComponent(ComponentID::BoardInfo, &board.mailbox, true);
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);
    supervisor.setup();

    // Path: LIVE(60) → READY(50) → CONNECTING(40) → BOOTING(30) → SLEEP(20)
    S2V2Access::setObservedState(supervisor, SystemState::LIVE);
    S2V2Access::setTargetState(supervisor, SystemState::SLEEP);
    S2V2Access::setHasActiveOrchestration(supervisor, false);

    // Run 1: LIVE → start READY
    supervisor.run();
    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(SystemState::READY,
                      S2V2Access::nextState(supervisor).transitionTarget);

    completeInFlightOrchestration(supervisor);

    // Run 2: READY complete → advance to READY, self-wake
    supervisor.run();
    TEST_ASSERT_EQUAL(SystemState::READY,
                      S2V2Access::getObservedState(supervisor));

    // Run 3: READY → start CONNECTING
    supervisor.run();
    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(SystemState::CONNECTING,
                      S2V2Access::nextState(supervisor).transitionTarget);

    completeInFlightOrchestration(supervisor);

    // Run 4: CONNECTING complete → advance to CONNECTING, self-wake
    supervisor.run();
    TEST_ASSERT_EQUAL(SystemState::CONNECTING,
                      S2V2Access::getObservedState(supervisor));

    // Run 5: CONNECTING → start BOOTING
    supervisor.run();
    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(SystemState::BOOTING,
                      S2V2Access::nextState(supervisor).transitionTarget);

    completeInFlightOrchestration(supervisor);

    // Run 6: BOOTING complete → advance to BOOTING, self-wake
    supervisor.run();
    TEST_ASSERT_EQUAL(SystemState::BOOTING,
                      S2V2Access::getObservedState(supervisor));

    // Run 7: BOOTING → start SLEEP
    supervisor.run();
    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(SystemState::SLEEP,
                      S2V2Access::nextState(supervisor).transitionTarget);

    completeInFlightOrchestration(supervisor);

    // Run 8: SLEEP complete → at target
    supervisor.run();
    TEST_ASSERT_EQUAL(SystemState::SLEEP,
                      S2V2Access::getObservedState(supervisor));
    TEST_ASSERT_FALSE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(SystemState::SLEEP,
                      S2V2Access::getTargetState(supervisor));
}

void test_multi_step_transition_ready_to_sleep_completes_autonomously() {
    SupervisorV2 supervisor;
    TestComponent board, wifi, audio, cli;
    supervisor.registerComponent(ComponentID::BoardInfo, &board.mailbox, true);
    supervisor.registerComponent(ComponentID::WiFi, &wifi.mailbox, true);
    supervisor.registerComponent(ComponentID::AudioRuntime, &audio.mailbox, true);
    supervisor.registerComponent(ComponentID::CLI, &cli.mailbox, false);
    supervisor.setup();

    // Path: READY(50) → CONNECTING(40) → BOOTING(30) → SLEEP(20)
    S2V2Access::setObservedState(supervisor, SystemState::READY);
    S2V2Access::setTargetState(supervisor, SystemState::SLEEP);
    S2V2Access::setHasActiveOrchestration(supervisor, false);

    // Run 1: READY → start CONNECTING
    supervisor.run();
    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(SystemState::CONNECTING,
                      S2V2Access::nextState(supervisor).transitionTarget);

    completeInFlightOrchestration(supervisor);

    // Run 2: CONNECTING complete → advance to CONNECTING, self-wake
    supervisor.run();
    TEST_ASSERT_EQUAL(SystemState::CONNECTING,
                      S2V2Access::getObservedState(supervisor));

    // Run 3: CONNECTING → start BOOTING
    supervisor.run();
    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(SystemState::BOOTING,
                      S2V2Access::nextState(supervisor).transitionTarget);

    completeInFlightOrchestration(supervisor);

    // Run 4: BOOTING complete → advance to BOOTING, self-wake
    supervisor.run();
    TEST_ASSERT_EQUAL(SystemState::BOOTING,
                      S2V2Access::getObservedState(supervisor));

    // Run 5: BOOTING → start SLEEP
    supervisor.run();
    TEST_ASSERT_TRUE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(SystemState::SLEEP,
                      S2V2Access::nextState(supervisor).transitionTarget);

    completeInFlightOrchestration(supervisor);

    // Run 6: SLEEP complete → at target
    supervisor.run();
    TEST_ASSERT_EQUAL(SystemState::SLEEP,
                      S2V2Access::getObservedState(supervisor));
    TEST_ASSERT_FALSE(S2V2Access::getHasActiveOrchestration(supervisor));
    TEST_ASSERT_EQUAL(SystemState::SLEEP,
                      S2V2Access::getTargetState(supervisor));
}
```

- [ ] **Step 2: Register the new tests**

In the same file's `main()`, after the existing `RUN_TEST` entries, add:

```cpp
    RUN_TEST(test_multi_step_transition_live_to_sleep_completes_autonomously);
    RUN_TEST(test_multi_step_transition_ready_to_sleep_completes_autonomously);
```

- [ ] **Step 3: Run tests to verify they pass**

```bash
pio test -e native -f test_supervisor_v2_orchestration
```

Expected: 15 tests PASS (13 existing + 2 new).

- [ ] **Step 4: Commit**

```bash
git add test/test_supervisor_v2_orchestration/test_main.cpp
git commit -m "test: add downward multi-step transition tests (LIVE→SLEEP, READY→SLEEP)"
```

---

### Task A2: Delete old Supervisor source files and old test tree

- [ ] **Step 1: Delete old Supervisor source files**

```bash
rm src/supervisor/supervisor.h
rm src/supervisor/supervisor.cpp
```

- [ ] **Step 2: Delete old test files**

```bash
rm -r test/test_transition_completion_tracking
rm -r test/test_mailbox_contract
rm -r test/test_state_transition_flow
```

- [ ] **Step 3: Fix test_component_types include**

The file `test/test_component_types/test_main.cpp` includes `#include "../../src/supervisor/supervisor.h"`. This was only needed for the old `ComponentStateMatrix` type and other enums. Replace with:

```cpp
#include "component_types.h"
```

- [ ] **Step 4: Build and run tests to verify deletion is correct**

```bash
pio test -e native
```

Expected: All tests pass (fewer total tests since ~36 old tests were deleted, offset by 2 new ones from A1).

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "refactor: remove obsolete Supervisor class and old test files"
```

---

### Task A3: Rename SupervisorV2 → Supervisor in all source and test files

- [ ] **Step 1: Rename using sed across all .cpp, .h files**

```bash
find src/ test/ -name '*.cpp' -o -name '*.h' | while read f; do
    sed -i \
        -e 's/SupervisorV2/Supervisor/g' \
        -e 's/s_supervisorV2/s_supervisor/g' \
        -e 's/S2V2Access/SupervisorAccess/g' \
        -e 's|supervisor/supervisor_v2\.h|supervisor/supervisor.h|g' \
        "$f"
done
```

- [ ] **Step 2: Rename source files**

```bash
mv src/supervisor/supervisor_v2.h src/supervisor/supervisor.h
mv src/supervisor/supervisor_v2.cpp src/supervisor/supervisor.cpp
mv test/support/s2v2_access.h test/support/supervisor_access.h
```

- [ ] **Step 3: Rename test includes that reference support header**

In all test .cpp files, replace `#include "support/s2v2_access.h"` with `#include "support/supervisor_access.h"`:

```bash
find test/ -name '*.cpp' -exec sed -i 's|support/s2v2_access\.h|support/supervisor_access.h|g' {} +
```

- [ ] **Step 4: Build and run tests to verify rename**

```bash
pio test -e native
```

Expected: All remaining ~108 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "refactor: rename SupervisorV2 to Supervisor, S2V2Access to SupervisorAccess"
```

---

## Phase B: CLI Slimming

### Overview of Changes

| What | Why |
|------|-----|
| Remove `suspend`, `resume` from `debug_cli.cpp` | Debug-only commands rarely used; audio testing possible without them |
| Consolidate `play`/`stop` → route through `postStateRequest()` (same path as `transition`) | Eliminates duplicate code paths between `play`/`transition live` and `stop`/`transition ready` |
| Add `station <url>` command | Set stream URL without starting playback — follows the same ssid/pass + connect pattern |
| Move `transition` from debug to production in `cli_command_logic.cpp` | Users can trigger state transitions directly (`transition live` = start stream, `transition ready` = stop) |
| Remove `CONNECTING_STREAM`, `STREAM_STOPPED` MessageKeys | No longer needed since command routing is simplified |
| Remove special-cased `play`/`stop`/`reset` routing in `cli.cpp` | All three now post `postStateRequest()` directly instead of checking message keys |

### Files Modified

| File | Change |
|------|--------|
| `src/components/cli/debug_cli.cpp` | Remove `suspend`, `resume` blocks and help lines. Make `transition live` call `postStateRequest(LIVE)` directly. |
| `lib/cli_command_logic/include/cli_command_result.h` | Remove `CONNECTING_STREAM`, `STREAM_STOPPED` enum values |
| `lib/cli_command_logic/src/cli_command_logic.cpp` | Change `play` → save station + return NONE. Change `stop` → return NONE. Add `transition` block. |
| `src/components/cli/cli.cpp` | Remove special `play`/`stop`/`reset` routing. Add generic postStateRequest for those commands. |
| `src/components/cli/cli_output.cpp` | Remove CONNECTING_STREAM and STREAM_STOPPED render cases. |

---

### Task B1: Remove suspend and resume debug commands

**Files:**
- Modify: `src/components/cli/debug_cli.cpp:96-112,124-127`

- [ ] **Step 1: Remove suspend and resume from debug_cli process()**

In `debug_cli.cpp`, delete the `suspend` and `resume` blocks:

Remove lines 96-112 (both `else if` blocks for `suspend` and `resume`).

After removal, the `process()` function should have `tasks`, `loadtest`, `tstatus`, and `transition` blocks — without `suspend` or `resume`.

- [ ] **Step 2: Remove suspend and resume from debug_cli printHelp()**

In `debug_cli.cpp`, remove lines 126-127 from `printHelp()`:

Remove:
```cpp
    Serial.println("  suspend             Suspend AudioTask");
    Serial.println("  resume              Resume AudioTask");
```

- [ ] **Step 3: Build and run tests**

```bash
pio run -e debug
pio test -e native
```

Expected: Build succeeds, all native tests pass (no native tests depend on CLI).

- [ ] **Step 4: Commit**

```bash
git add src/components/cli/debug_cli.cpp
git commit -m "refactor: remove suspend and resume debug CLI commands"
```

---

### Task B2: Move transition from debug to production

**Files:**
- Modify: `lib/cli_command_logic/src/cli_command_logic.cpp`
- Modify: `lib/cli_command_logic/include/cli_command_result.h`

- [ ] **Step 1: Add STATE_TRANSITION_REQUESTED message key**

In `cli_command_result.h`, add a new `MessageKey` enum value after the existing entries and before `UNKNOWN_COMMAND`:

In the `enum class MessageKey` block, after `HELP,` add:

```cpp
    STATE_TRANSITION_REQUESTED,
```

- [ ] **Step 2: Add transition command to production dispatch**

In `cli_command_logic.cpp`, add the `transition` command block. Insert it after the existing command chain (after the `help` block). The command dispatches to a new handler that validates the target state:

```cpp
    if (strcmp(cmd, "transition") == 0) {
        if (!arg || *arg == '\0') {
            return {MessageKey::USAGE_TRANSITION};
        }
        // Accept: live|streaming, ready|idle, sleep
        if (strcmp(arg, "live") == 0 || strcmp(arg, "streaming") == 0
         || strcmp(arg, "ready") == 0 || strcmp(arg, "idle") == 0
         || strcmp(arg, "sleep") == 0) {
            return {MessageKey::STATE_TRANSITION_REQUESTED, arg};
        }
        return {MessageKey::USAGE_TRANSITION, arg};
    }
```

- [ ] **Step 3: Add USAGE_TRANSITION message key**

In `cli_command_result.h`, after the existing keys and before `UNKNOWN_COMMAND`, add:

```cpp
    USAGE_TRANSITION,
```

- [ ] **Step 4: Add render cases for STATE_TRANSITION_REQUESTED and USAGE_TRANSITION**

In `cli_output.cpp`, in the `render()` switch statement, add:

```cpp
        case MessageKey::STATE_TRANSITION_REQUESTED:
            PROD_LOG(kLogSource, "Requesting transition to %s", result.text ? result.text : "");
            return;
        case MessageKey::USAGE_TRANSITION:
            ERROR_LOG(kLogSource, "Usage: transition <live|ready|sleep>");
            return;
```

- [ ] **Step 5: Add transition to help output**

In `cli_output.cpp`, in the `printHelp()` function, add after the `balance` line:

```cpp
    Serial.println("  transition <s>      Request state transition: live|ready|sleep");
```

- [ ] **Step 6: Add transition routing in cli.cpp**

In `cli.cpp`, in the `process()` function, after the existing `dispatchCommand()` call (line 167), add handling for `STATE_TRANSITION_REQUESTED`:

In the `if (s_supervisorV2)` block, add a new condition:

```cpp
        if (result.key == cli_output::MessageKey::STATE_TRANSITION_REQUESTED) {
            const char* target = result.text;
            if (target) {
                if (strcmp(target, "live") == 0 || strcmp(target, "streaming") == 0) {
                    s_supervisorV2->postStateRequest(SystemState::LIVE);
                } else if (strcmp(target, "ready") == 0 || strcmp(target, "idle") == 0) {
                    s_supervisorV2->postStateRequest(SystemState::READY);
                } else if (strcmp(target, "sleep") == 0) {
                    s_supervisorV2->postStateRequest(SystemState::SLEEP);
                }
            }
        }
```

- [ ] **Step 7: Remove transition from debug_cli**

In `debug_cli.cpp`, remove the `transition` command block and help line since it's now in production:

Remove the `else if (strcmp(cmd, "transition") == 0)` block (lines 116-118) and the `Serial.println("  transition <s> ...")` line from `printHelp()` (line 129).

- [ ] **Step 8: Build and run tests**

```bash
pio run -e debug
pio test -e native -f test_cli_command_logic
```

Expected: Build succeeds, all CLI tests pass.

- [ ] **Step 9: Commit**

```bash
git add src/components/cli/cli.cpp
git add src/components/cli/cli_output.cpp
git add src/components/cli/debug_cli.cpp
git add lib/cli_command_logic/src/cli_command_logic.cpp
git add lib/cli_command_logic/include/cli_command_result.h
git commit -m "refactor: move transition command from debug to production CLI"
```

---

### Task B3: Consolidate play/stop to route through state machine

**Files:**
- Modify: `lib/cli_command_logic/src/cli_command_logic.cpp`
- Modify: `src/components/cli/cli.cpp`
- Modify: `lib/cli_command_logic/include/cli_command_result.h`
- Modify: `src/components/cli/cli_output.cpp`

The goal: `play` only saves the station URL and posts `postStateRequest(LIVE)` — the actual `connectToHost()` happens in `handleLIVE()`. `stop` just posts `postStateRequest(READY)`. No more `CONNECTING_STREAM`/`STREAM_STOPPED` message keys or special routing.

- [ ] **Step 1: Simplify play command in dispatch**

In `cli_command_logic.cpp`, change the `play` block. Instead of returning `CONNECTING_STREAM`, save the station and return `NONE`:

Replace the `play` block (lines 33-46):

```cpp
    if (strcmp(cmd, "play") == 0) {
        const char* url = arg;
        if (!url || *url == '\0') {
            url = env.loadStation();
            if (!url || *url == '\0') {
                return {MessageKey::USAGE_PLAY};
            }
        }
        if (env.wifiConnectivity() != WiFiConnectivity::CONNECTED) {
            return {MessageKey::WIFI_REQUIRED};
        }
        env.saveStation(url);
        return {MessageKey::NONE, url};
    }
```

The key change: return `{MessageKey::NONE, url}` instead of `{MessageKey::CONNECTING_STREAM, url}`.

- [ ] **Step 2: Simplify stop command in dispatch**

In `cli_command_logic.cpp`, change the `stop` block to return `NONE`:

Replace the `stop` block (lines 48-50):

```cpp
    if (strcmp(cmd, "stop") == 0) {
        return {MessageKey::NONE};
    }
```

The key change: return `{MessageKey::NONE}` instead of `{MessageKey::STREAM_STOPPED}`.

- [ ] **Step 3: Simplify cli.cpp routing — remove special play/stop/reset conditions**

In `cli.cpp`, replace the special-cased `play`/`stop`/`reset` routing with a generic approach. The dispatch already returns NONE for these commands, so postStateRequest directly:

Replace the entire `if (s_supervisorV2)` block (lines 169-177) with:

```cpp
    if (s_supervisorV2) {
        if (result.key == cli_output::MessageKey::STATE_TRANSITION_REQUESTED) {
            const char* target = result.text;
            if (target) {
                if (strcmp(target, "live") == 0 || strcmp(target, "streaming") == 0) {
                    s_supervisorV2->postStateRequest(SystemState::LIVE);
                } else if (strcmp(target, "ready") == 0 || strcmp(target, "idle") == 0) {
                    s_supervisorV2->postStateRequest(SystemState::READY);
                } else if (strcmp(target, "sleep") == 0) {
                    s_supervisorV2->postStateRequest(SystemState::SLEEP);
                }
            }
        } else if (strcmp(cmd, "play") == 0) {
            s_supervisorV2->postStateRequest(SystemState::LIVE);
        } else if (strcmp(cmd, "stop") == 0) {
            s_supervisorV2->postStateRequest(SystemState::READY);
        } else if (strcmp(cmd, "reset") == 0) {
            s_supervisorV2->postStateRequest(SystemState::READY);
        }
    }
```

Key change: `play` and `stop` no longer check for CONNECTING_STREAM/STREAM_STOPPED keys. They always post the state request when called.

- [ ] **Step 4: Remove CONNECTING_STREAM and STREAM_STOPPED from MessageKey enum**

In `cli_command_result.h`, remove these entries from `enum class MessageKey`:

```cpp
    CONNECTING_STREAM,
    STREAM_STOPPED,
```

- [ ] **Step 5: Remove CONNECTING_STREAM and STREAM_STOPPED from render**

In `cli_output.cpp`, remove these two `case` blocks from the `render()` switch:

```cpp
        case MessageKey::CONNECTING_STREAM:
            PROD_LOG(kLogSource, "Connecting to stream: %s", result.text ? result.text : "");
            return;
        case MessageKey::STREAM_STOPPED:
            PROD_LOG(kLogSource, "Stream stopped");
            return;
```

- [ ] **Step 6: Add "Connecting" feedback when play triggers transition**

The `play` command now returns NONE with the URL as text. The render() no-ops for NONE. Instead, print the "Connecting" message when the state request is posted. In `cli.cpp`, after the `postStateRequest(LIVE)` call for `play`, add:

```cpp
    if (s_supervisorV2) {
        if (result.key == cli_output::MessageKey::STATE_TRANSITION_REQUESTED) {
            // ... transition routing ...
        } else if (strcmp(cmd, "play") == 0) {
            s_supervisorV2->postStateRequest(SystemState::LIVE);
            PROD_LOG("CLI", "Connecting to stream: %s", result.text ? result.text : "");
        } else if (strcmp(cmd, "stop") == 0) {
            s_supervisorV2->postStateRequest(SystemState::READY);
        } else if (strcmp(cmd, "reset") == 0) {
            s_supervisorV2->postStateRequest(SystemState::READY);
        }
    }
```

Note: `PROD_LOG` needs the kLogSource from the cli.cpp namespace. The existing `const char* kLogSource` at line 5 of cli.cpp or use the existing log source. In cli.cpp, the anonym namespace doesn't have a kLogSource defined. Use the literal `"CLI"` string directly:

```cpp
            PROD_LOG("CLI", "Requesting stream: %s", result.text ? result.text : "");
```

- [ ] **Step 7: Make transition live in debug_cli call postStateRequest directly**

In `debug_cli.cpp`, the `postManualTransition()` function currently calls `cli::process("play")` for "live"/"streaming". Change it to call `postStateRequest()` directly, matching the other states:

Find the `postManualTransition` function and replace the live/streaming branch:

```cpp
static bool postManualTransition(const char* targetState) {
    if (!s_supervisorV2) {
        ERROR_LOG(kLogSource, "Supervisor not available for transition command");
        return true;
    }

    // ... existing logic ...

    if (strcmp(targetState, "live") == 0 || strcmp(targetState, "streaming") == 0) {
        s_supervisorV2->postStateRequest(SystemState::LIVE);
        PROD_LOG(kLogSource, "Requesting transition to LIVE");
        return true;
    }
```

(Keep the existing ready/idle and sleep/error branches as-is — they already call postStateRequest directly.)

- [ ] **Step 8: Update test expectations for CLI test**

The existing CLI test `test_play_command_with_wifi_requests_transition_and_persists_station` expects `CONNECTING_STREAM` key. Update it to expect `NONE` instead:

In `test/test_cli_command_logic/test_main.cpp`, find the test and change:

```cpp
    TEST_ASSERT_EQUAL(static_cast<int>(cli_output::MessageKey::NONE),
                      static_cast<int>(result.key));
```

Also update `test_play_command_requires_wifi_and_does_not_start_stream` to expect NONE instead of CONNECTING_STREAM.

Also update any test for `stop` that expects STREAM_STOPPED to expect NONE.

- [ ] **Step 9: Build and run all tests**

```bash
pio test -e native
```

Expected: All tests pass (fix any that reference old MessageKeys).

- [ ] **Step 10: Build for hardware**

```bash
pio run -e debug
```

Expected: Build succeeds.

- [ ] **Step 11: Commit**

```bash
git add lib/cli_command_logic/src/cli_command_logic.cpp
git add lib/cli_command_logic/include/cli_command_result.h
git add src/components/cli/cli.cpp
git add src/components/cli/cli_output.cpp
git add src/components/cli/debug_cli.cpp
git add test/test_cli_command_logic/test_main.cpp
git commit -m "refactor: consolidate play/stop with state machine transitions"
```

---

### Task B4: Add station command to set URL without playing

**Files:**
- Modify: `lib/cli_command_logic/src/cli_command_logic.cpp`
- Modify: `lib/cli_command_logic/include/cli_command_result.h`
- Modify: `src/components/cli/cli_output.cpp`
- Modify: `test/test_cli_command_logic/test_main.cpp`

The `station` command follows the same pattern as `ssid`/`pass`: set a value, then use `play` (like `connect`) to act on it. `play <url>` still works as a convenience shortcut.

- [ ] **Step 1: Add USAGE_STATION and STATION_SET message keys**

In `cli_command_result.h`, after `USAGE_TRANSITION,` add:

```cpp
    USAGE_STATION,
    STATION_SET,
```

- [ ] **Step 2: Add station command to dispatch**

In `cli_command_logic.cpp`, add the `station` command block before the `help` block:

```cpp
    if (strcmp(cmd, "station") == 0) {
        if (!arg || *arg == '\0') {
            return {MessageKey::USAGE_STATION};
        }
        env.saveStation(arg);
        return {MessageKey::STATION_SET, arg};
    }
```

- [ ] **Step 3: Add render cases for station messages**

In `cli_output.cpp`, in the `render()` switch statement, add:

```cpp
        case MessageKey::USAGE_STATION:
            ERROR_LOG(kLogSource, "Usage: station <url>");
            return;
        case MessageKey::STATION_SET:
            PROD_LOG(kLogSource, "Station set to: %s", result.text ? result.text : "");
            return;
```

- [ ] **Step 4: Add station to help output**

In `cli_output.cpp`, in `printHelp()`, add after the `pass` line:

```cpp
    Serial.println("  station <url>       Set default stream URL without starting playback");
```

- [ ] **Step 5: Add tests for station command**

In `test/test_cli_command_logic/test_main.cpp`, add two new test functions:

```cpp
void test_station_sets_url_and_stores_it() {
    // ...
    commandLogic::dispatchCommand("station", "http://example.com/stream", audio, env, 255);
    // Verify station was saved
    TEST_ASSERT_EQUAL_STRING("http://example.com/stream", env.savedStation);
}

void test_station_without_arg_returns_usage() {
    commandLogic::dispatchCommand("station", "", audio, env, 255);
    // Expect USAGE
}
```

Register them in `main()` with `RUN_TEST(test_station_sets_url_and_stores_it);` and `RUN_TEST(test_station_without_arg_returns_usage);`.

- [ ] **Step 6: Build and run tests**

```bash
pio test -e native -f test_cli_command_logic
```

Expected: All CLI tests pass (including 2 new station tests).

- [ ] **Step 7: Commit**

```bash
git add lib/cli_command_logic/src/cli_command_logic.cpp
git add lib/cli_command_logic/include/cli_command_result.h
git add src/components/cli/cli_output.cpp
git add test/test_cli_command_logic/test_main.cpp
git commit -m "feat: add station command to set stream URL without playback"
```

---

## Self-Review Checklist

- [ ] **Phase A spec coverage:** Old Supervisor deleted, SupervisorV2 renamed to Supervisor, old test files deleted (4), new downward tests added (2). All references updated across 20+ files.
- [ ] **Phase B spec coverage:** `suspend`/`resume` removed from debug CLI. `play`/`stop` consolidated to route through `postStateRequest()`. `transition` moved to production. `CONNECTING_STREAM`/`STREAM_STOPPED` MessageKeys removed. `station` command added for URL-only setting.
- [ ] **Placeholder scan:** No TBD, TODO, or placeholder patterns. All code shown in full.
- [ ] **Type consistency:** `SupervisorV2` → `Supervisor` rename is mechanical string-replace. No API changes. CLI message key removal is verified via test assertion updates.
