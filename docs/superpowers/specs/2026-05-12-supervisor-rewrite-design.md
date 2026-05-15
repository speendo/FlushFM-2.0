# Supervisor Rewrite Design

**Date:** 2026-05-12 | **Status:** Draft

## Motivation

The current supervisor (supervisor.h: 545 lines, supervisor.cpp: 897 lines) has accumulated complexity across user stories US-0025 through US-0037. It mixes concerns (state machine, component orchestration, cross-core dispatch, test scaffolding) into a single class with 20+ public methods and a callback-heavy architecture. The rewrite aims to:

1. Simplify code — fewer types, fewer methods, flatter control flow
2. Align with guidelines (`state-management.md`, `concurrency.md`) — use FreeRTOS primitives, event-driven push model

## Approach

**Event-driven state machine backed by FreeRTOS.** The supervisor runs as a task on Core 0, blocking on a FreeRTOS queue. Components post events into the queue rather than calling callbacks into the supervisor. The supervisor calls component transition methods (`setOFF`, `setIDLE`, `setSTREAMING`, `setERROR`) synchronously, and components post completion/failure back as events.

This replaces the callback/orchestration pattern (`TransitionInvoker`, `TransitionTimeoutHook`, `reportCompletion`, `beginOrchestration`, `requestTransition`, `finishTransition`, `PendingComponentTransition`, `OrchestrationContext`, `StateTransitionInfo`) with direct method calls + event posts.

## New Public API

```
Supervisor()                             // BOOTING, target SLEEP
SystemState state() const                 // observed state
void subscribe(StateObserver)             // state change callback
void setup()                              // idempotent boot entry
bool postEvent(event, reason)             // any core → queue
bool postEvent(event, reason, target)     // STATE_REQUESTED → queue
void setErrorEvent(reason, source)        // async error, first-writer-wins
bool registerComponent(id, required)      // register component
void run()                                // task loop, blocks on queue
```

Gone from current API: `setComponentTransitionHooks`, `reportCompletion`, `beginComponentTransition`, `beginOrchestration`, `requestTransition`, `finishTransition`, `isOrchestrationActive`, `componentsWaitingForCompletion`, `hasActiveTransition`, `activeTransitionId/From/Target`, `getComponentStatus`, `isComponentRequired`, `getPendingTimeout`, `postEventBuffered`, `triggerFatal`.

## State Model (unchanged)

States: FATAL(0), ERROR(10), SLEEP(20), BOOTING(30), CONNECTING(40), READY(50), LIVE(60).

States with rank <= 30 are transient and cannot be externally targeted via `STATE_REQUESTED`. Enums, X-macro pattern, and `stateRank()` helper are preserved from the current code.

## New Event

`SystemEvent::TRANSITION_COMPLETED` — carries `{ComponentID, TransitionStatus, DebugReason}`. Posted by components into the FreeRTOS queue via `postEvent()` when they finish or fail a transition method call. The supervisor dispatches these in `run()` to update per-component completion tracking.

## Transition Flow

```
stepTowardTarget(target)
  -> calls each registered, non-disabled component's transition method
     (setIDLE/setSTREAMING/setOFF/setERROR)
  -> each component starts async work
  -> each component posts TRANSITION_COMPLETED (Completed or Failed)
     into the queue via postEvent()
  -> run() drains queue, updates per-component completion status
  -> when all eligible components have reported:
     setObservedStateImmediate -> next state
  -> if observedState_ != targetMode_: stepTowardTarget again
```

Components post completions via `postEvent(TRANSITION_COMPLETED, reason)` (2-param overload with custom event data in a dedicated slot). The supervisor tracks which components are expected per transition in a `std::array<TransitionStatus, Count>`.

## Component Contract Update

`ISystemComponent` gains a reference to the supervisor (or the queue) at registration time, stored for posting completion events. The existing transition methods remain unchanged. The state matrix (`getStateMatrix()`/`getStateMatrixSize()`) and timeout hook remain (queried by the supervisor on demand during transition).

`registerComponent(id, required)` replaces the current combination of `registerComponent + setComponentTransitionHooks`. The supervisor reads the component's state matrix and timeout config directly from the `ISystemComponent` interface.

## Implementation Plan (7 layers, bottom-up)

### Layer 1 — New supervisor header
Write new `supervisor.h` from scratch (~55 lines). Enums, class skeleton, public API, private members. No implementation. Old files untouched — the new header lives alongside the old one (e.g., `supervisor_v2.h`) until integration.

### Layer 2 — State machine core (pure logic)
`stepTowardTarget()` implemented as a rank-based transition table with explicit switch/if branching (same pattern as current). `handleEvent()` as pure dispatch: `(current, event, payload) -> (next, action)`. No component awareness, no FreeRTOS. Fully unit-testable under native.

### Layer 3 — Component coordinator
Logic to register components, iterate eligible components during transitions, call their transition methods synchronously, and track completion status per transition. Testable with mock `ISystemComponent`.

### Layer 4 — Queue abstraction
Thin FreeRTOS queue wrapper (depth 1, overwrite) for system events. A separate completion payload slot holds `{ComponentID, TransitionStatus, DebugReason}` for `TRANSITION_COMPLETED` events — this avoids collision with `STATE_REQUESTED` in the shared queue slot. Testable on native with a simple ring buffer mock.

### Layer 5 — Supervisor task
`Supervisor::run()` FreeRTOS task loop: blocks on queue, dispatches to layers 2-3, checks timeouts, notifies observers. Wires the layers together. Testable on native with mock queue.

### Layer 6 — Component updates
Update `ISystemComponent` and all 4 concrete components (`BoardInfo`, `WiFi`, `AudioRuntime`, `CLI`) to match the new contract. Replace `reportCompletion()` calls with `postEvent(TRANSITION_COMPLETED, ...)`. Remove `setComponentTransitionHooks` calls from `registerWithController()`.

### Layer 7 — Integration
Wire `main.cpp` to create the supervisor task via `xTaskCreatePinnedToCore`. Replace `new` file paths. Run full test suite (`pio test -e native`). Verify on hardware. Delete old supervisor files.

## Key Design Decisions

- **FreeRTOS queue, depth 1, overwrite:** Preserves last-write-wins semantics from current mailbox. Same as current `Mailbox` struct but backed by `xQueueCreate(1, sizeof(EventSlot))`.
- **Completion events use a parallel slot** (not the shared mailbox) to avoid collision with `STATE_REQUESTED` events that arrive mid-transition.
- **Components hold `Supervisor&`** to post completion events via `postEvent()` — same pattern they already use for errors (`setErrorEvent`).
- **Timeouts:** Supervisor records start time when calling component transition methods. Checks elapsed time on each `run()` iteration. On timeout, calls `component.onTransitionTimeout()` directly — no separate hook callback.
- **Logging (`toString`):** Implementations live in `supervisor.cpp` (anonymous namespace) initially. Extracted to a shared utility header when other compilation units need them.
- **Step-by-step delivery:** Layer 1 produces a compilable header (no code yet). Each subsequent layer is testable independently before the next begins.
