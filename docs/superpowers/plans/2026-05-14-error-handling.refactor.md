## Plan: Error Handling Refactor (API + Logic)

Refactor error event handling from passive buffer to active error management: rename methods for consistency with `postStateRequest`/`consumeStateRequest` pattern, implement recovery attempt tracking with logging, and establish clear reset semantics when exiting ERROR state.

**TL;DR:**
- Rename: `setErrorEvent()` → `postErrorEvent()`, `takeErrorEvent()` → `consumeErrorEvent()`
- Rename config: `maxRetries` → `maxRecoveries` (>= 1), keep `recoveryCounter` as-is
- Implement `postErrorEvent()`: guard (first-write-wins)
- Implement `consumeErrorEvent()`: log with attempt #, increment counter, check exhaustion → FATAL or keep target
- Add recovery counter reset logic when observedState leaves ERROR (only reset point)
- Fix bug: `postStateRequest()` has spurious `return true;`

**Steps**

1. **Update supervisor_v2.h**
   - Rename `setErrorEvent()` → `postErrorEvent()` (keep signature identical: `void postErrorEvent(DebugReason, ComponentID)`)
   - Rename `takeErrorEvent()` → `consumeErrorEvent()` (change signature: remove output params, return void)
   - Rename `getMaxRetries()` → `getMaxRecoveries()`; rename `setMaxRetries()` → `setMaxRecoveries()`
   - Add docstring to `consumeErrorEvent()` describing 4-step logic
   - Rename member `maxRetries_` → `maxRecoveries_` (default = 3, min valid = 1)

2. **Update supervisor_v2.cpp - `postErrorEvent()`**
   - Guard: if `errorEvent_.pending` already true, return (first-write-wins)
   - Set `errorEvent_.pending = true`, store reason/source

3. **Update supervisor_v2.cpp - `consumeErrorEvent()`**
   - Check if `errorEvent_.pending`; if false, return
   - Log: `"[%s] %s - recovery attempt #%d/%d"` with source, reason, `recoveryCounter+1`, `maxRecoveries_`
   - Increment `retryPolicy_.recoveryCounter++`
   - If `recoveryCounter >= maxRecoveries_` → `setTargetState(FATAL)`
   - Clear errorEvent (pending=false, reason=nullptr, source=Count)

4. **Add recovery counter reset logic**
   - Add private method `resetRecoveryIfOutOfError()` 
   - Called whenever `observedState_` changes; if new observed state is not ERROR/FATAL → `recoveryCounter = 0`
   - This will be called from `run()` (future work, but document the contract)

5. **Fix bugs**
   - Remove spurious `return true;` from `postStateRequest()` (already void)

6. **Update method calls**
   - Grep for `setErrorEvent`, `takeErrorEvent`, `getMaxRetries`, `setMaxRetries` in codebase
   - Update all call sites to new names

7. **Update validation**
   - `setMaxRecoveries()`: change validation from `>= 0` to `>= 1` (can't set to 0)

**Relevant files**
- `src/state_machine/supervisor_v2.h` — method signatures, member renames
- `src/state_machine/supervisor_v2.cpp` — implementations of postErrorEvent, consumeErrorEvent, resetRecoveryIfOutOfError, validation
- Grep results: find all call sites to old method names

**Verification**
1. Compile clean (no method signature mismatches)
2. Run `pio test -e native` to confirm all tests pass
3. Manually verify: 
   - `consumeErrorEvent()` logs with correct attempt number
   - Second error while still pending is ignored (first-write-wins)
   - Counter continues incrementing if error occurs multiple times while in ERROR state
   - Counter resets to 0 when exiting ERROR state via `resetRecoveryIfOutOfError()`

**Decisions**
- `recoveryCounter` name stays (user confirmed "glaub ich auch nicht die kernfrage")
- Recovery target is NOT preset (no auto-reset to SLEEP); determined by next external event
- Reset happens when observedState leaves ERROR, not when error event is consumed
- maxRecoveries must be >= 1 (enforced in setter)
