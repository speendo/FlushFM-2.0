# Supervisor API Boundary

**Status:** Approved (US-0027a) | **Updated:** 2026-05-09

## Overview

The `Supervisor` is an **infrastructure orchestrator module**, not a runtime component. It coordinates global state transitions, processes system events, manages component lifecycle registration, and notifies observers. It does not participate in the component registry.

---

## 1. Supervisor as Module, Not Component

### Explicit Non-Component Status

The `Supervisor` is **not a component** in the component registry:
- Does not register itself via `registerComponent(...)`
- Does not require or use a `ComponentID` for program logic
- Maintains only a static debug label (`"Supervisor"`) for diagnostics and logging

### Module Responsibilities

The `Supervisor` orchestrates:
- **Global system state transitions** (BOOTING → CONNECTING → READY → LIVE, with SLEEP and ERROR as endpoints)
- **Event queue processing** (FreeRTOS queue drain, ordering, arbitration)
- **Component orchestration** (transition coordination, timeout watchdog, completion tracking)
- **Observer notification** (state change callbacks)

---

## 2. Public API Surface (Stable Module Contract)

### State Inspection

```cpp
SystemState state() const;
```
- Returns current global system state
- No preconditions; safe to call anytime

### Event Posting

```cpp
bool postEvent(SystemEvent event, SystemReason reason,
               EventPolicy policy = EventPolicy::BestEffort);
```
- Posts an event to the processing queue
- `EventPolicy::BestEffort`: non-blocking; may drop if queue is full
- `EventPolicy::Critical`: waits briefly (10ms); falls back to sticky pending flags
- On failure: logs `ERROR_LOG` with event context
- Thread-safe from any core

### Queue Processing (Core 0 Only)

```cpp
void processEventQueue();
```
- Drains and processes events from the queue
- Called from Core 0 main loop
- Handles timeouts and state transitions

### Observer Registration

```cpp
void subscribe(StateObserver observer);
// StateObserver = std::function<void(SystemState)>
```
- Registers a callback notified on every state change
- Callback receives the new system state
- Called on Core 0 after state commit

### Component Registry

```cpp
bool registerComponent(ComponentID id, bool isRequired);
ComponentLifecycleStatus getComponentStatus(ComponentID id) const;
bool isComponentRequired(ComponentID id) const;
```
- Register components by `ComponentID` (compile-time enum)
- Query component lifecycle status and required/optional classification
- Defaults: unregistered components return `Unknown` status and `false` for required

### Component Orchestration

```cpp
bool setComponentTransitionHooks(ComponentID id,
                                 TransitionInvoker invoker,
                                 TransitionTimeoutHook timeoutHook);
// TransitionInvoker = std::function<uint32_t(SystemState, uint32_t)>
// TransitionTimeoutHook = std::function<void(uint32_t)>

bool reportCompletion(ComponentID id, uint32_t transitionId,
                      TransitionStatus status, DebugReason reason);

bool beginComponentTransition(ComponentID id, uint32_t transitionId);
```
- `setComponentTransitionHooks`: statically wires transition callbacks per component at boot
- `reportCompletion`: component signals transition completion (`Completed` or `Failed`)
- `beginComponentTransition`: arms a pending transition for a component (internal use)

### Transition Inspection

```cpp
TransitionRequestDecision requestTransition(SystemState from, SystemState target, uint32_t transitionId);
bool finishTransition(uint32_t transitionId);
bool beginOrchestration(SystemState target, SystemEvent trigger,
                        SystemReason reason, uint32_t transitionId);
bool isOrchestrationActive() const;
size_t componentsWaitingForCompletion() const;
bool hasActiveTransition() const;
bool hasQueuedTransition() const;
uint32_t activeTransitionId() const;
SystemState activeTransitionFrom() const;
SystemState activeTransitionTarget() const;
uint32_t queuedTransitionId() const;
SystemState queuedTransitionFrom() const;
SystemState queuedTransitionTarget() const;
```
- Read-only inspection of active and queued state transitions
- Used by diagnostics (CLI/debug output) and internal timeout watchdog

---

## 3. Internal-Only Methods (Not Public API)

The following methods are **internal orchestration helpers**:

```cpp
void handleEvent(SystemEvent event, SystemReason reason);       // Core event dispatch
void transitionTo(SystemState next, SystemEvent trigger,
                  SystemReason reason, uint32_t transitionId);  // State commit + observers
void checkTransitionTimeouts();                                  // Timeout watchdog
```

---

## 4. Component Lifecycle Registry

### Ownership

The `Supervisor` **owns** the component lifecycle registry:
- Registry: `std::array<ComponentRegistryEntry, ComponentID::Count>`
- Tracks: `{ isRegistered, isRequired, isDisabled, lifeCycleStatus, lastFailureReason }`
- Limited to **runtime components**: `BoardInfo`, `WiFi`, `AudioRuntime`, `CLI`

### Registration Flow

```cpp
// In component setup (e.g., BoardInfoComponent::registerWithController())
controller.registerComponent(id(), false);
controller.setComponentTransitionHooks(id(), invoker, timeoutHook);
```

Components call `registerComponent(ComponentID, bool)` and `setComponentTransitionHooks(ComponentID, ...)` during `registerWithController()`. The `Supervisor` itself is never registered.

---

## 5. Hook Wiring and Initialization

Component transition hooks are **statically wired at boot** via `registerWithController()`:
- Each component registers its hooks before any transition begins
- Hook targets are determined during `setup()`, not at runtime
- The hook contract is: invoker returns a timeout in milliseconds, timeout hook handles expiry

---

## 6. Module Boundaries Summary

| Artifact | Owner | Notes |
| --- | --- | --- |
| Global system state | Supervisor | Single source of truth |
| Component lifecycle registry | Supervisor | Fixed-size array indexed by `ComponentID` |
| Event queue | Supervisor | FreeRTOS queue, processed on Core 0 |
| In-flight orchestration state | Supervisor | Transition ID, target state, timeout tracking |
| Component local state | Component | Mirrors global guarantees; component-owned lifecycle |
| Hook binding | Component at init | Wired in `registerWithController()`, immutable after boot |

---

## 7. Graceful Degradation

The `Supervisor` remains robust if optional components are unavailable:
- Missing optional component: system continues with degraded status
- Missing required component: `ERROR` state triggered
- Timeout with no recovery: component marked `Failed`; policy applied

## 8. Related Stories

- US-0025c: Event contract freeze
- US-0026c-g: Static identifier migration (completed)
- US-0027a: This document
- US-0027b-c: Boundary enforcement and guard removal
