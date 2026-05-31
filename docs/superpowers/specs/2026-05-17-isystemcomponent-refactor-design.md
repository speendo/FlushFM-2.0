# ISystemComponent Refactor — Base Class with Full Contract

> **Created:** 2026-05-17 | **Author:** Design session

## 1. Motivation

Currently every concrete component (`BoardInfoComponent`, `WiFiComponent`, `AudioRuntimeComponent`, `CliComponent`) duplicates the same infrastructure:

- Private `ComponentMailbox supervisorV2Mailbox` field
- `switch (target)` dispatch in `loop()` mapping `SystemState` -> setter method
- `registerComponent()` call in `setup()` with a `const_cast` hack to expose the mailbox
- Boilerplate `(void)transitionId; return 0;` in no-op setter bodies

The supervisor's surface on each component is narrow -- it needs `id()`, `name()`, `isRequired`, and the `mailbox` pointer. These belong in the base class.

---

## 2. Design

### 2.1 `ISystemComponent` — Abstract Base Class

```
ISystemComponent(ComponentID id, const char* name, bool isRequired)

// -- Public -----------------------------------------------

id()              -> ComponentID           (existing, unchanged)
name()            -> const char*           (existing, unchanged)
isRequired()      -> bool                  (new)
mailbox()         -> ComponentMailbox&     (new)
setup()           -> bool                  (pure virtual, unchanged)
loop()            -> void                  (non-virtual: reads mailbox -> dispatch -> poll)

// -- Protected -- component contract (pure virtual) -------

handleBOOTING()   -> void
handleSLEEP()     -> void
handleCONNECTING() -> void
handleREADY()     -> void
handleLIVE()      -> void
handleERROR()     -> void
handleFATAL()     -> void
poll()            -> void
onTransitionTimeout(uint32_t transitionId) -> void

// -- Protected -- helpers (non-virtual) -------------------

registerWithSupervisor(Supervisor& supervisor)
completeTransition(TransitionStatus status)

// -- Private ----------------------------------------------

dispatch(SystemState target)
mailbox_           ComponentMailbox
id_                ComponentID
name_              const char*
isRequired_        bool
```

### 2.2 `loop()` — Non-Virtual, Owned by Base Class

```cpp
void ISystemComponent::loop() {
    SystemState target;
    if (mailbox_.consumeNextState(target)) {
        dispatch(target);
    }
    poll();
}
```

Components cannot override `loop()`. They hook into dispatch via `handleX()` overrides and into the periodic tick via `poll()`.

### 2.3 `dispatch()` — Private Switch

```cpp
void ISystemComponent::dispatch(SystemState target) {
    switch (target) {
        case SystemState::BOOTING:    handleBOOTING(); break;
        case SystemState::SLEEP:      handleSLEEP(); break;
        case SystemState::CONNECTING: handleCONNECTING(); break;
        case SystemState::READY:      handleREADY(); break;
        case SystemState::LIVE:       handleLIVE(); break;
        case SystemState::ERROR:      handleERROR(); break;
        case SystemState::FATAL:      handleFATAL(); break;
    }
}
```

### 2.4 Protected Helpers

**`registerWithSupervisor(Supervisor& supervisor)`** replaces the duplicated `registerComponent` + `const_cast` pattern:

```cpp
void ISystemComponent::registerWithSupervisor(Supervisor& supervisor) {
    supervisor.registerComponent(id_, &mailbox_, isRequired_);
}
```

**`completeTransition(TransitionStatus status)`** wraps the global supervisor call:

```cpp
void ISystemComponent::completeTransition(TransitionStatus status) {
    s_supervisor.completeTransition(id_, status);
}
```

### 2.5 Concrete Component Example — `WiFiComponent`

```cpp
class WiFiComponent final : public ISystemComponent {
public:
    WiFiComponent();
    bool setup() override;
    void handleLIVE() override;
    void handleSLEEP() override;
    void handleERROR() override;
    void poll() override;
    void onTransitionTimeout(uint32_t transitionId) override;
    bool bootAutoConnectSucceeded() const;

private:
    bool bootAutoConnectSucceeded_ = false;
    uint32_t pendingTransitionId_ = 0;
};
```

All nine pure virtual methods must be declared -- the seven `handleX()` state handlers plus `poll()` and `onTransitionTimeout()`. Empty bodies are written as `{}` (e.g., `BoardInfoComponent` for states it doesn't handle).

### 2.6 Replaces Old Lifecycle Methods

The old `setOFF`, `setIDLE`, `setSTREAMING`, and `setERROR` methods are removed. They didn't map 1:1 to system states and the new `handleX()` contract is cleaner.

### 2.7 All Nine Are Pure Virtual

Every component overrides all nine. No default bodies -- the compiler enforces completeness. A component like `BoardInfoComponent` that has no work for a state writes `void handleCONNECTING() override {}`.

---

## 3. What Disappears

| Removed | From |
|---|---|
| `ComponentMailbox supervisorV2Mailbox` field | All 4 components |
| 4x `switch (target) { ... }` in `loop()` | All 4 components |
| `loop()` override | All 4 components |
| `registerComponent(id, &const_cast<Self*>(this)->supervisorV2Mailbox, isRequired)` | All 4 `setup()` methods |
| `setOFF/setIDLE/setSTREAMING/setERROR` methods | All 4 components |
| `(void)transitionId; return ...;` boilerplate | BoardInfo, CLI |
| `completePendingTransition()` / `startPendingTransition()` | WiFi, AudioRuntime |
| `extern Supervisor s_supervisor` include | `system_components.cpp` (moves to base) |

---

## 4. Non-Blocking

All `handleX()` methods return `void` and must not block. Synchronous components call `completeTransition()` immediately from `handleX()`. Async components (WiFi, AudioRuntime) kick off work in `handleX()` and check progress in `poll()`, calling `completeTransition()` when ready.

---

## 5. In-Between States

Component-private substates (e.g., `INTERNAL_CONNECTING`, `INTERNAL_ASSOCIATING`) are per-component implementation details. They live in the concrete component, invisible to the supervisor. The supervisor only sees the mailbox writes and `completeTransition` / `postErrorEvent` calls.

---

## 6. Access Control Rationale

| Member | Visibility | Reason |
|---|---|---|
| `id_`, `name_`, `isRequired_` | Private + accessor | Immutable after constructor, read-only externally |
| `mailbox_` | Private + accessor | Only the base `loop()` should consume state from it |
| `dispatch()` | Private | Implementation detail of `loop()`, never called directly |
| `handleX()` | Protected pure virtual | Component contract -- must be overridable |
| `poll()` | Protected pure virtual | Async hook -- must be overridable |
| `onTransitionTimeout()` | Protected pure virtual | Supervisor-facing contract |
| `registerWithSupervisor()` | Protected | Called from `setup()`, needs supervisor reference |
| `completeTransition()` | Protected | Called from `handleX()` and `poll()` |

---

## 7. Impact

- **`component_types.h`** — no change
- **`system_components.h`** — rewritten (new `ISystemComponent`, slimmer concrete classes)
- **`system_components.cpp`** — significantly shortened; only `setup()` and `handleX()` bodies remain
- **`main.cpp`** — no change (still calls `component->setup()` and `component->loop()`)
- **`supervisor_v2.h` / `.cpp`** — no change (frozen API)
- **Tests** — component tests need updates (mailbox moves, `loop()` becomes non-virtual)
