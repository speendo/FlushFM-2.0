# Step 1: supervisor_v2.h — Data Structures and Declarations

**Goal:** Add all new structs, member variables, and method declarations to `supervisor_v2.h`.

**Files:**
- Modify: `src/state_machine/supervisor_v2.h`

---

### Subtask 1a: Add ComponentMailbox and ComponentMailboxSlot structs

- [ ] Add `ComponentMailbox` struct (pending + targetState)
- [ ] Add `ComponentMailboxSlot` struct (mailbox + spinlock)

These go before the class, alongside the existing ErrorEvent/RetryPolicy structs.

### Subtask 1b: Add new public method declarations

- [ ] Add `void run()` — main tick function
- [ ] Add `void completeTransition(ComponentID id, TransitionStatus status)` — component completion signal
- [ ] Add `void registerComponent(ComponentID id, bool isRequired, ComponentMailboxSlot* slot)` — component registration

### Subtask 1c: Add new private method declarations

- [ ] Add `void drainMailbox()` — consume state request under spinlock
- [ ] Add `void drainErrorEvent()` — consume error event under spinlock
- [ ] Add `void startOrchestration(SystemState target)` — begin component transitions
- [ ] Add `void checkOrchestrationCompletion()` — poll event group bits
- [ ] Add `void checkStateTimeout()` — detect stale orchestrations
- [ ] Add `void setObservedState(SystemState state)` — commit state, log, reset recovery counter
- [ ] Add `SystemState determineRecoveryTarget()` — decide what to aim for after ERROR
- [ ] Add `void postNextComponentState(ComponentID id, SystemState target)` — write to component mailbox
- [ ] Add `void handleFatal()` — manage deep sleep transition

### Subtask 1d: Add new member variables

- [ ] Add `StaticEventGroup_t eventGroupBuffer_` — event group memory (no heap)
- [ ] Add `EventGroupHandle_t eventGroup_` — event group handle
- [ ] Add `ComponentMailboxSlot* componentMailboxSlots_[componentCount]` — pointers to component mailboxes
- [ ] Add `TickType_t orchestrationStartMs_` — start time of current orchestration
- [ ] Add `uint32_t currentTimeoutMs_` — current orchestration deadline
- [ ] Add `bool hasActiveOrchestration_` — orchestration in-flight flag
- [ ] Add `EventBits_t expectedBits_` — expected event group bitmask
- [ ] Add `TickType_t fatalEnteredAtMs_` — when FATAL was entered
- [ ] Add `uint32_t fatalDwellMs_` — dwell time before deep sleep (default 60s)
- [ ] Add `SystemState lastTargetBeforeError_` — saved target for recovery

### Subtask 1e: Update setup() signature

- [ ] Update `setup()` doxygen to mention event group creation
- [ ] Add FreeRTOS event groups include

### Verification

- [ ] Compile check for syntax errors
- [ ] Run `pio test -e native` to confirm no regressions
