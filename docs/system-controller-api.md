# SystemController API Boundary

**Status:** To Be Defined (US-0027a) | **Updated:** 2026-04-25

## Overview

This document specifies the SystemController as an **infrastructure orchestrator module**, not a runtime component. It defines:
- Public API responsibilities and bounds
- Internal helper methods excluded from public surface
- Component lifecycle registry ownership
- Hook wiring and initialization contract

---

## 1. SystemController as Module, Not Component

### Explicit Non-Component Status

SystemController is **not a component** in the component registry:
- Does not register itself via `registerComponent(...)`
- Does not participate in component lifecycle identity flow
- Does not require or use a `ComponentId` for program logic
- Maintains only a static debug label for diagnostics and logging

### Module Responsibilities

SystemController orchestrates:
- **Global system state transitions** (BOOTING → SLEEP → CONNECTING → READY → LIVE → ERROR)
- **Event queue processing** (queue drain, ordering, arbitration)
- **Component orchestration** (transition coordination, timeout watchdog, completion tracking)
- **Observer notification** (state change callbacks)

---

## 2. Public API Surface (Stable Module Contract)

### State Inspection

```cpp
SystemState getState() const;
```
- Return current global system state
- No preconditions; safe to call anytime

### Event Posting (Queue Interface)

```cpp
bool postEvent(SystemEvent event, SystemReason reason = nullptr);
```
- Post an event to the processing queue
- Returns: true if enqueued; false if queue full
- On failure: logs ERROR_LOG with context
- Thread-safe from any core via queue; enqueue may fail under backpressure

### Queue Processing (Core 0 Only)

```cpp
void processEventQueue();
```
- Drain and process events from the queue
- Called from Core 0 main loop or orchestration task
- Invokes `handleEvent(...)` for each dequeued event
- Handles timeouts and state transitions

### Observer Registration (Callback Hook)

```cpp
void registerStateObserver(StateObserver callback);
```
- Register a callback to be notified on state changes
- Callback signature: `void(SystemState oldState, SystemState newState)`
- Called on Core 0 after state commit

### Component Orchestration (Transition Coordination)

```cpp
bool beginComponentTransition(
    ComponentId componentId,
    SystemState targetState,
    uint32_t timeoutMs,
    TransitionInvoker invoker,
    TransitionTimeoutHook timeoutHook
);
```
- Initiate a transition for a component during orchestration
- Returns: success/failure (may fail if orchestration not active)
- Note: **Intended for internal orchestration flows; public use limited**

```cpp
void reportCompletion(
    ComponentId componentId,
    uint32_t transitionId,
    TransitionResult result
);
```
- Component reports completion of a transition (success or failure)
- Includes optional reason for logging
- Ignored if `transitionId` does not match active orchestration

### Component Registry (Identity-Based Lookup, US-0026 Onward)

```cpp
bool registerComponent(ComponentId id, bool isRequired);
bool getComponentStatus(ComponentId id, ComponentLifecycleStatus& status);
bool isComponentRequired(ComponentId id);
```
- Register components by static identifier (post-US-0026c)
- Query component status and required/optional classification
- Name-based lookup removed in favor of static identifiers

---

## 3. Internal-Only Methods (Not Public Surface)

The following methods are **internal orchestration helpers** and must not be exposed as public API:

### State Transition Commit Helper

```cpp
void transitionTo(SystemState target);  // Internal only
```
- Applies new global state, logs transition, notifies observers
- Called from `handleEvent(...)` only
- Does not own orchestration state (in-flight tracking, component coordination)
- No replay/deferred cleanup logic

### Orchestration Context Inspection

```cpp
// Internal inspect methods – not for public use
bool isTransitionActive() const;
uint32_t getActiveTransitionId() const;
SystemState getTargetState() const;
```
- Used internally by timeout watchdog and completion tracking
- Public exposure would create incorrect coupling

### Debug/Diagnostic Helpers

```cpp
void logTransitionDetails(...);      // Internal logging
const char* getDebugLabel() const;   // Static label for diagnostics
```

---

## 4. Component Lifecycle Registry

### Ownership

SystemController **owns** the component lifecycle registry:
- Registry tracks: {ComponentId, isRequired, lifeCycleStatus, lastFailureReason}
- Registry is limited to **runtime components** (WiFi, AudioRuntime, CLI, BoardInfo)
- SystemController itself is **not in the registry**

### Registration Flow

```cpp
// In component setup (e.g., WiFiComponent::setup())
controller.registerComponent(ComponentId::WIFI, true);  // required
controller.registerComponent(ComponentId::CLI, false);  // optional
```

### Query vs. Internal State

- Public methods: `getComponentStatus(...)`, `isComponentRequired(...)`
- Internal tracking: in-flight orchestration state, timeout values, completion flags

---

## 5. Hook Wiring and Initialization

### Deterministic Static Hook Setup

Component transition hooks are **statically wired at initialization**:
- Hook targets (transition invoker, timeout handler) are determined during `setup()`, not at runtime
- No dynamic hook configuration after initialization
- All required hooks are bound before any state transition can occur

**Decision (US-0027c):** Runtime guard branches that contradict static invariants (e.g., unknown component, missing callback) are removed from production paths where proven safe by:
- Deterministic initialization order
- Unit tests verifying hook setup
- Graceful degradation via explicit fallback paths

### Hook Binding Example

```cpp
// In SystemController::setup()
for (auto& comp : components) {
    if (comp.hasTransitionHook) {
        setComponentTransitionHooks(
            comp.id,
            comp.transitionInvoker,
            comp.timeoutHook
        );
    }
}
```

---

## 6. Module Boundaries Summary

| Artifact | Owner | Notes |
| --- | --- | --- |
| Global system state | SystemController | Single source of truth |
| Component lifecycle registry | SystemController | Limits to runtime components |
| Event queue | SystemController | Processes on Core 0 |
| In-flight orchestration state | SystemController | Transition ID, target state, timeout tracking |
| Component local state | Component | Mirrors global guarantees; component-owned lifecycle |
| Hook binding | Static at init | No dynamic reconfiguration; guarded by tests |

---

## 7. Graceful Degradation

SystemController remains robust if optional components are unavailable:
- Missing optional component: log warning; continue with degraded status
- Missing required component: log error; enter ERROR state or block transition
- Null callback during initialization: safe failure (verified by tests)
- Timeout with no recovery: component marked failed; policy applied

---

## 8. Related Stories

- US-0015: Component lifecycle registry (done)
- US-0016: Transition orchestration (done)
- US-0025c: Event contract freeze
- US-0026c–f: Static identifier migration (refactoring names-based APIs)
- US-0027a–c: Module boundary clarification and guard removal
