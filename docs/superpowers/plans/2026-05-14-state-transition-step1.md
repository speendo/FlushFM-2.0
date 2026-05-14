# Step 1: supervisor_v2.h — Data Structures and Declarations

**Goal:** Add all new structs, member variables, and method declarations to `supervisor_v2.h`.

**Files:**
- Modify: `src/component_types.h`
- Modify: `src/state_machine/supervisor_v2.h`
- Modify: `src/state_machine/supervisor_v2.cpp`

---

### Subtask 1a: Add ComponentMailbox to component_types.h, add spinlocks to Mailbox/ErrorEvent

- [x] Add `ComponentMailbox` struct to `component_types.h` with embedded spinlock
- [x] Add `portMUX_TYPE` native stubs to `component_types.h`
- [x] Add `portMUX_TYPE spinlock` to `Mailbox` struct
- [x] Add `portMUX_TYPE spinlock` to `ErrorEvent` struct
- [x] Remove `ComponentMailbox` and `ComponentMailboxSlot` from `supervisor_v2.h`

### Subtask 1b: Add new public method declarations

- [x] Add `void run()` — main tick function
- [x] Add `void completeTransition(ComponentID id, TransitionStatus status)` — component completion signal
- [x] Add `void registerComponent(ComponentID id, ComponentMailbox* mailbox)` — component presence check-in

### Subtask 1c: Add new private method declarations

- [x] Add `void startOrchestration(SystemState target)` — begin component transitions
- [x] Add `void checkOrchestrationCompletion()` — poll event group bits
- [x] Add `void checkStateTimeout()` — detect stale orchestrations
- [x] Add `void setObservedState(SystemState state)` — commit state, log, reset recovery counter
- [x] Add `SystemState determineRecoveryTarget()` — decide what to aim for after ERROR
- [x] Add `void postNextComponentState(ComponentID id)` — write target from nextState_ to component mailbox
- [x] Add `void handleFatal()` — manage deep sleep transition

### Subtask 1d: Add new member variables (8 members)

- [x] Add `StaticEventGroup_t eventGroupBuffer_` — event group memory (no heap)
- [x] Add `EventGroupHandle_t eventGroup_` — event group handle
- [x] Add `ComponentMailbox* componentMailboxes_[componentCount]` — pointers to component mailboxes
- [x] Add `TickType_t orchestrationDeadlineMs_` — absolute deadline for current orchestration (0 = none)
- [x] Add `bool hasActiveOrchestration_` — orchestration in-flight flag
- [x] Add `TickType_t fatalDeadlineMs_` — absolute deadline for deep sleep (0 = none)
- [x] Add `EventBits_t expectedBits_` — expected event group bitmask
- [x] Add `SystemState lastTargetBeforeError_` — saved target for recovery; TODO: remove once real determineRecoveryTarget() logic supersedes this

### Subtask 1e: Update setup() signature

- [x] Update `setup()` doxygen to mention event group creation
- [x] Add FreeRTOS event groups include

### Verification

- [x] Compile check for syntax errors
- [x] Run `pio test -e native` to confirm no regressions
