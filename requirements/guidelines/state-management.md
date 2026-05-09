# Rule: State Management
[Status: Active | Updated: 2026-05-09]
**Context:** ESP32/C++, FreeRTOS | **Goal:** Define component lifecycle and system-wide state transitions via a **Supervisor Pattern** with autonomous **Components** and an **Event System**.

> **Lifecycle** is the operative concept: every component and the Supervisor itself moves through a defined sequence of states. The Supervisor orchestrates; Components execute and report.

---

## 1. Core Rules

### Supervisor
- **Core 0 only:** Run the Supervisor exclusively on Core 0. Cross-core signals must use FreeRTOS mechanisms (→ `concurrency.md`).
- **Two values:** Maintain a `TargetMode` (goal) and an `ObservedState` (current reality) as distinct values at all times.
- **State hierarchy:** States are organised in three levels, representing the full system lifecycle:
  - *Level 0 – inert:* `OFF`, `FATAL`. No active software; cannot trigger `BOOTING` without user intervention.
  - *Level 1 – boot triggers:* `INIT`, `SLEEP`, `ERROR`. All three trigger `BOOTING`.
  - *Level 2 – linear software sequence:* `BOOTING → CONNECTING → READY → LIVE`.
  ```
  [L0: inert]         OFF / FATAL
                            ↓
  [L1: boot triggers] INIT / SLEEP / ERROR
                            ↓
  [L2: linear]        BOOTING → CONNECTING → READY → LIVE
  ```
- **Pre-software states:** `OFF` (no power; software does not exist) and `INIT` (power applied; hardware event that triggers `BOOTING`) require no software implementation. `FATAL` halts software and effectively reduces the system to an inert state until user interaction triggers `INIT`.
- **No skipping:** Traverse the linear sequence strictly in order, including during recovery.
- **Transient states:** `BOOTING` and `CONNECTING` are transient — never a target, always passed through. `BOOTING` is the setup phase of the system: global `setup()` *initiates* `BOOTING` (→ `software-architecture.md`).
- **SLEEP:** A stable endpoint reached from `READY` after long inactivity. Implemented as ESP32 Deep Sleep. Exit always triggers a hardware reboot → `BOOTING`. Never a pass-through state. Sub-states (`PENDING` → `COMMITTED`) apply during the transition *into* `SLEEP`; once Deep Sleep is active, no software runs.
- **ERROR:** Reachable from any state in the linear sequence. Exit always via `BOOTING`. See `ERROR` recovery below.
- **FATAL:** Software halted. Reachable from `ERROR` when `retryPolicy.isExhausted()`. No automatic recovery. Requires user interaction to clear.
- **Sub-states:** Each Target Mode transition carries a sub-state: `PENDING` (in progress) → `COMMITTED` (all components confirmed) or `FAILED` (timeout or component failure).
- **Supervisor follows components:** Advance `ObservedState` only after all components have reported `COMMITTED`. The Supervisor is always the last to update.
- **Component tracking:** Track which individual components have reported `COMMITTED` per transition, not just a count.
- **Unresponsive components:** If a component does not report within the transition timeout, treat it as `FAILED`.
- **ERROR recovery:** On `ERROR`, set `TargetMode` to `ERROR` and initialise `RetryPolicy`. Determine recovery target by querying the current state of input components (i.e. components that signal external conditions) — the result is the highest `TargetMode` their states justify. Do not restore the previous `TargetMode`. Instruct all components to transition to `BOOTING`, then proceed sequentially toward the target. Increment `recoveryCounter` on each failed attempt. If `retryPolicy.isExhausted()`: set `TargetMode` to `FATAL`.
- **Mailbox:** Buffer incoming System Events in a single-slot Mailbox (last-write-wins). Process after current transition completes.

### Components
- **States:** Implement at least all software-layer states: `BOOTING`, `CONNECTING`, `READY`, `LIVE`, `SLEEP`, `ERROR`, `FATAL`. Pre-software states (`OFF`, `INIT`) are not software states and require no implementation. Additional intermediate states are permitted.
- **Required vs. optional:** Mark each component as `required` or `optional` in configuration. A `required` component that reports `FAILED` triggers `ERROR` recovery. An `optional` component that reports `FAILED` is marked `DEGRADED` and excluded from the `COMMITTED` quorum; the system continues with reduced functionality.
- **Absent components:** An `optional` component that is not present at boot is treated as permanently `DEGRADED`. A `required` component that is not present at boot triggers `ERROR` recovery immediately.
- **Push state:** Actively push state to the Supervisor.
- **Reporting:** Report `COMMITTED` or `FAILED` to the Supervisor upon completing or failing a transition.
- **Lifecycle contract:** Expose `setup()` and optionally `loop()` as per component lifecycle contract (→ `modularity.md`). Component `setup()` is called by the Supervisor once per component during `BOOTING`.
- **min/max matrix:** Define a min/max state matrix indexed by `systemState`. On forward transitions (moving to a higher state), enforce `minState` with timeout. On backward transitions (moving to a lower state), enforce `maxState` with timeout. Forward and backward timeouts are configured separately. Use `TARGET_MODE` as a dynamic reference to the current Target Mode in the `maxState` field. Include a `SLEEP` row — it defines component behaviour during the transition *into* `SLEEP`, not during Deep Sleep itself.
- **Persistence:** Save critical runtime data (e.g. last station, volume) to NVS.
- **SLEEP exception:** During `SLEEP` (ESP32 Deep Sleep), the main CPU is off. Components are not active; the push model does not apply. The Light Sensor is monitored by the ULP at hardware level. On wake, a hardware reboot triggers `BOOTING` and the full component lifecycle restarts.

### Event System
- **System Events:** Route to the Supervisor via the Mailbox. Only the most recent is relevant.
- **Error Events:** Emitted by a component entering an error state. Store as a single flag + payload. Accept only the first per transition. See Data Structures for payload definition.
- **Component Commands:** Route directly to the target component. Do not pass through the Supervisor.

### Data Structures
- **Mailbox** — single-slot buffer, last-write-wins; holds most recent System Event while Supervisor is busy. Scope: Supervisor.
- **Error Event flag** — single flag + payload `{ reason, source }`; set on first Error Event per transition; cleared when Supervisor takes it up. Scope: Supervisor. — TODO: verify payload structure against existing project definition.
- **RetryPolicy** — `{ maxRetries: int, recoveryCounter: int }`; exposes `isExhausted()`. Scope: Supervisor; instantiated per recovery attempt.
- **Component status map** — tracks `COMMITTED`, `FAILED`, or `DEGRADED` status per component; `DEGRADED` is assigned by the Supervisor when an `optional` component reports `FAILED`; `DEGRADED` components are excluded from the quorum. Scope: Supervisor; non-`DEGRADED` entries are reset on each new transition.
- **Component config** — per-component configuration including `ComponentID id` (compile-time identity), `const char* name` (human-readable label for diagnostics), and `required: bool`. Scope: Supervisor configuration.
- **Transition timeout config** — per-transition; separate values for forward and backward transitions. Scope: Supervisor configuration.
- **min/max state matrix** — per-component table indexed by `systemState`. Scope: each Component individually.

---

## 2. Constraints & Exceptions

- **Never:** Skip states — the linear sequence `BOOTING → CONNECTING → READY → LIVE` must be traversed in order, including during recovery.
- **Never:** Use queues within the Supervisor event and transition flow — single-slot Mailbox and single Error Event flag are sufficient. This does not affect cross-core FreeRTOS queues (→ `concurrency.md`).
- **Never:** Poll component states — components push to the Supervisor.
- **Never:** Allow a component to directly read or modify another component's state.
- **Never:** Write `TargetMode` or `ObservedState` from Core 1.
- **Limit:** NVS writes to user-critical settings only; avoid frequent writes.
- **Exception:** The boot sequence may bypass formal state transitions during hardware initialisation before the FreeRTOS scheduler starts.
- **Exception:** `SLEEP`, `ERROR` and `FATAL` rows in the min/max matrix may deviate from the sequential pattern to allow graceful degradation per component.
- **Exception:** No automatic recovery from `FATAL` — requires user interaction to clear.

---

## 3. Reference Pattern

```cpp
// OFF and INIT are pre-software states; not represented in code.
// FATAL is a software-halted state; Supervisor stops processing on entry.
enum class TargetMode {
    // Linear sequence (software layer)
    BOOTING,    // transient; entry point from INIT, SLEEP wake, ERROR recovery
    CONNECTING, // transient; always targets READY
    READY,      // stable; software standby
    LIVE,       // stable; active playback
    // Special endpoints
    SLEEP,      // endpoint; ESP32 Deep Sleep; exit via hardware reboot → BOOTING
    ERROR,      // endpoint; exit via BOOTING (recovery)
    FATAL       // endpoint; requires user interaction to clear
};
enum class SubState        { PENDING, COMMITTED, FAILED };
enum class ComponentStatus { COMMITTED, FAILED, DEGRADED };

// Supervisor holds one SubState per active transition.
struct ActiveTransition {
    TargetMode target;
    SubState   state = SubState::PENDING;
};

// Component identity: compile-time enum, not runtime strings.
// Human-readable names are kept for diagnostics only.
// Component entries are added as the project grows; Count provides array sizing.
enum class ComponentID : uint8_t { Count };
// Maintained by Supervisor; updated on every push from a Component.
// Last known status persists even if a Component stops reporting.
using ComponentStatusMap = std::array<ComponentStatus, static_cast<size_t>(ComponentID::Count)>;

struct RetryPolicy {
    int maxRetries;
    int recoveryCounter = 0;
    bool isExhausted() const { return recoveryCounter >= maxRetries; }
};

struct StateRequirement {
    TargetMode minState; // enforced with timeout on forward transitions
    TargetMode maxState; // enforced with timeout on backward transitions; TARGET_MODE = dynamic ref
};

class ComponentBase {
public:
    const ComponentID id;             // compile-time identity; array index
    const char* name = nullptr;       // human-readable label for diagnostics only
    bool required = true;

    explicit ComponentBase(ComponentID id, const char* name) : id(id), name(name) {}

    virtual bool setup() = 0;
    virtual void loop() {}
    virtual void transitionTo(TargetMode target) = 0;
    virtual void reportCommitted() = 0;
    virtual void reportFailed()    = 0;
};
```
