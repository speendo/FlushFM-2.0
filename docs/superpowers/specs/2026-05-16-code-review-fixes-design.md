# Code Review Fixes — Design Spec (2026-05-16)

Scope: `src/supervisor/orchestrator.h`, `src/supervisor/orchestrator.cpp`, `src/supervisor/supervisor_v2.h`, `src/supervisor/supervisor_v2.cpp`, `src/supervisor/state_machine.cpp`.

Deferred: #7 (consumeStateRequest/consumeErrorEvent — already correct), #8 (setTargetState/setObservedState — single-task, atomic). Revisit at end.

---

## #1 — OrchestrationOrder::consume() race

**Problem:** `consume()` clears `pending` under spinlock, then releases the lock. Caller reads `expectedBits`, `deadlineMs`, `transitionTarget` without any lock. A concurrent `post()` can overwrite these fields mid-read.

**Proposed fix:** Change `consume()` to copy all fields into out-parameters under the spinlock, matching `ComponentMailbox::consumeNextState()` pattern. Signature becomes `bool consume(EventBits_t& outBits, TickType_t& outDeadline, SystemState& outTarget)`.

**Files:** `orchestrator.h` (struct), `orchestrator.cpp` (caller at line 136 — worker reads fields after consume).

---

## #2 — OrchestrationResponse::consume() race

**Problem:** Same pattern as #1. `checkOrchestrationResponse()` in `orchestrator.cpp:99-105` reads `result` and `timedOutComponents` after `consume()` releases the lock.

**Proposed fix:** Same approach — `bool consume(OrchestrationResult& outResult, EventBits_t& outTimedOut)`.

**Files:** `orchestrator.h` (struct), `orchestrator.cpp` (`checkOrchestrationResponse`).

---

## #3 — Mixed time units and underflow in deadline calculation

**Problem:** `startOrchestration()` at `orchestrator.cpp:89` computes:
```
xTaskGetTickCount() + getTransitionTimeout(target, ...)
```
`xTaskGetTickCount()` returns **ticks**, `getTransitionTimeout()` returns **ms**. Stored in `deadlineMs` (misleading name) and later the worker does:
```
pdMS_TO_TICKS(deadlineMs - now)
```
Where `now` is ticks. The subtraction mixes ticks and ms. If `deadlineMs < now` (can happen with small timeout + task scheduling delay), the unsigned underflow produces a huge wait.

**Proposed fix:** Convert timeout to ticks at store time, not at use time. Rename `deadlineMs` → `deadlineTicks`.

Store in `startOrchestration()`:
```cpp
TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(getTransitionTimeout(target, isForward));
```

Consume in worker (after #1 fix copies to local `deadlineTicks` under lock):
```cpp
TickType_t rawWait = deadlineTicks - xTaskGetTickCount();
// Unsigned subtraction handles tick wrap. If rawWait exceeds a reasonable max
// (e.g. max possible timeout), the deadline has already passed — use zero wait.
TickType_t waitTicks = (rawWait < pdMS_TO_TICKS(60000)) ? rawWait : 0;
```

Why this works:
- **Normal:** `deadlineTicks > now` → `rawWait` = remaining time. Correct.
- **Tick wrap (deadline wrapped, now not yet):** Unsigned subtraction `wrapped_deadline - near_max_now` produces the correct remaining ticks. Correct.
- **Deadline passed:** `now > deadlineTicks` → `rawWait` wraps to near-UINT32_MAX → cap clamps to 0. Correct.

**Files:** `orchestrator.h` (field rename), `orchestrator.cpp` (both `startOrchestration` and worker).

---

## #4 — EventGroup bits cleared after mailbox writes

**Problem:** `startOrchestration()` at `orchestrator.cpp:70-84` writes all component mailboxes via `postNextComponentState()`, THEN calls `xEventGroupClearBits()`. If a component sees the mailbox write on another core and immediately calls `completeTransition()` → `xEventGroupSetBits()`, that bit is set before the clear and gets erased. Result: lost signal, false timeout.

**Proposed fix:** Clear event group bits BEFORE writing component mailboxes.

**Files:** `orchestrator.cpp` (`startOrchestration`).

---

## #5 — Uninitialized member variables

**Problem:** `SupervisorV2::SupervisorV2() = default` leaves many members uninitialized:
- `observedState_`, `targetState_`, `lastTargetBeforeError_`
- `hasActiveOrchestration_`
- `workerTaskHandle_`, `supervisorTaskHandle_`
- `fatalDeadlineMs_`, `fatalDeadlineElapsed_`, `fatalEntered_`
- `nextState_.transitionTarget` (subState has a default)

**Proposed fix:** Add value-initializers to all uninitialized members in the header. `SystemState` defaults to `SystemState::BOOTING` (first non-FATAL state). Handles default to `nullptr`. Bools to `false`. Ticks to `0`.

**Files:** `supervisor_v2.h` (member declarations).

---

## #6 — TaskHandle NULL dereference in postStateRequest/postErrorEvent

**Problem:** `postStateRequest()` and `postErrorEvent()` in `orchestrator.cpp:45,57` call `xTaskNotifyGive(supervisorTaskHandle_)` unconditionally. `supervisorTaskHandle_` is set in `setup()` at `supervisor_v2.cpp:9`. If a component calls these during its own init (before `SupervisorV2::setup()`), the handle is garbage/zero → undefined behavior.

**Proposed fix:** Guard with `if (supervisorTaskHandle_ != nullptr)` before calling `xTaskNotifyGive`. `xTaskNotifyGive(nullptr)` is technically valid on FreeRTOS (ignores), but the guard is clearer and documents intent.

**Files:** `orchestrator.cpp` (`postStateRequest`, `postErrorEvent`).

---

## #9 — Polling busy-loop in orchestrationWorker

**Problem:** `orchestrationWorker()` at `orchestrator.cpp:132-138` polls `orderMailbox_.consume()` every 10ms via `vTaskDelay`. Wakes 100×/second even when idle, wasting CPU.

**Proposed fix:** Replace polling with a task notification. Worker blocks on `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)` instead of polling. `startOrchestration()` signals via `xTaskNotifyGive(workerTaskHandle_)`.

Each FreeRTOS task has its own notification state, so there's no collision with the existing `xTaskNotifyGive(supervisorTaskHandle_)` used for response delivery — each direction has its own notifier/notifiee pair.

---

## #10 — EventGroup handle NULL check missing

**Problem:** `setup()` calls `xEventGroupCreateStatic(&eventGroupBuffer_)` without checking the return value. On allocation failure, `eventGroup_` is NULL and subsequent `xEventGroupSetBits`/`xEventGroupWaitBits` crash.

**Proposed fix:** Add NULL check after `xEventGroupCreateStatic`. On failure, log error and enter FATAL immediately.

**Files:** `supervisor_v2.cpp` (`setup`).

---

## #11 — componentStatuses_ never reset to COMMITTED

**Problem:** Once a component hits `DEGRADED` or `FAILED`, no code resets it back to `COMMITTED`. The review flagged this as a bug.

**Resolution: Not a bug — design decision (G).** `componentStatuses_` only resets on reboot (zero-initialization of BSS for the global instance). This is the simplest, most predictable approach. A component that failed once is not trusted again until a full system restart.

Document the intent in a comment on the `componentStatuses_` declaration in `supervisor_v2.h` so future reviewers understand it's deliberate.

**Future options (if ever revisited):**
- **H — Per-component self-healing:** A DEGRADED component that reports `Completed` gets reset to COMMITTED. The component proves health individually without a blanket reset.
- **C — Reset on leaving ERROR:** Clear all statuses when the system transitions from ERROR to a non-error state. More conservative than H — only resets at the recovery boundary.


---

## #12 — TickType_t wrap-around in deadline calculations

**Problem:** Both `fatalDeadlineMs_` (`state_machine.cpp:231`) and `orderMailbox_.post()` deadline (`orchestrator.cpp:89`) use `xTaskGetTickCount() + duration`. `xTaskGetTickCount()` wraps at `UINT32_MAX` (~49.7 days). After wrap, the stored deadline can be a small value, causing the `>=` comparison to trigger immediately or never.

**Proposed fix:** For `handleFatal()`: Use delta comparison instead of absolute comparison. Store `fatalEnteredTicks_` and compare `(xTaskGetTickCount() - fatalEnteredTicks_) >= pdMS_TO_TICKS(kFatalDwellMs)`. Unsigned subtraction handles wrap naturally (provided the delta fits in 32 bits, which 60s does).

For orchestration deadline: covered by fix #3 (store deadline as ticks, compute remaining via capped subtraction).

**Files:** `state_machine.cpp` (`handleFatal`), `supervisor_v2.h` (rename `fatalDeadlineMs_` to `fatalEnteredTicks_`).

---

## #13 — Required component failure doesn't abort active orchestration

**Problem:** `completeTransition()` at `orchestrator.cpp:31-32` posts an error event when a required component fails, but does not cancel the in-flight orchestration. The comment says it "aborts the current orchestration." Actual behavior: worker keeps blocking until timeout.

**Resolution: Fix the comment.** The delay is bounded by `getTransitionTimeout()` (configurable, 5s default). Components get a graceful cleanup path via mailbox writes. The in-flight orchestration still completes (or times out) before the error is processed on the next `run()` tick.

Updated comment: "posts an error event; the state machine processes it after the current orchestration cycle ends (via timeout or completion), then decides the next target."

**Files:** `orchestrator.cpp` (comment fix in `completeTransition`).

---

## #15 — ERROR/FATAL targets still orchestrate

**Problem:** `getNextState()` returns ERROR/FATAL immediately (no stepping), but `stepTowardTarget()` calls `startOrchestration()` for it. Comment says "immediate — no stepping needed" but the behavior is not truly immediate — components get mailbox writes and the worker waits for acknowledgment.

**Resolution: Fix the comment.** Orchestration toward ERROR/FATAL is intentional — components need to be notified of the error state and given a chance to clean up. The comment should say "immediate target selection (no stepping)" instead of "immediate execution." No code change needed.

**Files:** `state_machine.cpp` (comment fix in `getNextState`).

---

## #16 — checkComponentPresence() never called

**Problem:** `checkComponentPresence()` is defined at `state_machine.cpp:54-59` but never invoked. Missing required components are never flagged.

**Proposed fix:** Call `checkComponentPresence()` once at the start of the first orchestration, or in `run()` on the first tick after setup. Simplest: call it in `startOrchestration()` guarded by a `bool firstOrchestration_` flag.

**Files:** `state_machine.cpp` (`checkComponentPresence` already exists), `supervisor_v2.h` (new `firstOrchestration_` flag), `supervisor_v2.cpp` (call site).

---

## Implementation Plan Sequence

Ordered by dependency and risk:

1. **Trivial batch** (#5, #6, #10, #13, #15, #16) — member initializers, null guards, EventGroup NULL check, and three comment fixes. Independent, no design risk.
2. **#1 + #2** — `consume()` race fix for both Order and Response. Touches 3 call sites.
3. **#3** — Time unit fix. Depends on #1/#2 (consume signature change already done).
4. **#4** — EventGroup clear order. Independent, same function as #3.
5. **#12** — TickType wrap-around. Partially overlaps with #3 (deadline math already fixed there; this is just handleFatal).
6. **#9** — Polling → task notification. Structural change to worker loop. Independent.

Deferred after all fixes: #7, #8, #14 (pushbacks and duplicates).
