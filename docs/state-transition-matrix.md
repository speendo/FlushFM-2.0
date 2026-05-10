# State Transition Matrix

> **Scope:** Supervisor (`src/state_machine/supervisor.cpp`)

Every `(SystemState, SystemEvent)` pair has exactly one normalized outcome:

- **direct → STATE**: transition directly via `setObservedStateImmediate()` (no orchestration)
- **request → STATE**: orchestration to target state via `requestStateTransition()`
- **state unchanged**: no state change (event ignored or only registry updated)
- **defer**: event is recorded but action is deferred until a later state
- **cancel**: orchestration request is discarded

## State-Independent Events (User Intents)

These events are checked before the state switch and can fire from any state.

| Event | Outcome |
|-------|---------|
| `ENTER_SLEEP` | `targetState_` = SLEEP; request → SLEEP |
| `STOP_REQUESTED` | `targetState_` = SLEEP; if state == CONNECTING → **cancel**; else request → READY |
| `PLAY_REQUESTED` (state == CONNECTING) | `targetState_` = LIVE; **defer** |
| `PLAY_REQUESTED` (state == SLEEP) | `targetState_` = LIVE; direct → CONNECTING; if both WiFi+Audio Ready → request → READY |
| `PLAY_REQUESTED` (state == LIVE) | `targetState_` = LIVE; request → READY (replay) |
| `PLAY_REQUESTED` (other states) | request → LIVE |

## State-Dependent Events

### BOOTING

| Event | Outcome |
|-------|---------|
| `BOOT` | direct → SLEEP |

### SLEEP

| Event | Outcome |
|-------|---------|
| `WIFI_READY` | registry WiFi = Ready; state unchanged |
| `AUDIO_INIT_OK` | registry AudioRuntime = Ready; state unchanged |
| `WIFI_DISCONNECTED` | registry WiFi = Unknown; state unchanged |
| `AUDIO_INIT_FAILED` | registry AudioRuntime = Failed; state unchanged |

### CONNECTING

| Event | Outcome |
|-------|---------|
| `AUDIO_INIT_OK` | registry AudioRuntime = Ready; if WiFi Ready → request → READY |
| `WIFI_READY` | registry WiFi = Ready; if AudioRuntime Ready → request → READY |
| `COMPONENT_SETUP_FAILED` | direct → ERROR |
| `AUDIO_INIT_FAILED` | direct → ERROR |

### READY

| Event | Outcome |
|-------|---------|
| `WIFI_DISCONNECTED` | direct → ERROR |
| `COMPONENT_SETUP_FAILED` | direct → ERROR |
| `AUDIO_INIT_FAILED` | direct → ERROR |

### LIVE

| Event | Outcome |
|-------|---------|
| `WIFI_DISCONNECTED` | direct → ERROR |
| `STREAM_LOST` | direct → ERROR |
| `AUDIO_INIT_FAILED` | direct → ERROR |
| `PLAY_REQUESTED` | see state-independent table above (replay) |

### ERROR

| Event | Outcome |
|-------|---------|
| `RECOVER` | direct → READY |

## Orchestration-Driven Transitions

The orchestrator commits the final state after all required components report completion. Transitions triggered by `requestStateTransition()` are:

| Transition | Trigger Event | Target |
|------------|---------------|--------|
| SLEEP → CONNECTING | PLAY_REQUESTED | CONNECTING (direct) |
| CONNECTING → READY | WIFI_READY + AUDIO_INIT_OK | READY (orchestrated) |
| READY → LIVE | PLAY_REQUESTED (deferred by targetState_) | LIVE (orchestrated) |
| LIVE → READY | STOP_REQUESTED or PLAY_REQUESTED (replay) | READY (orchestrated) |
| LIVE → SLEEP | ENTER_SLEEP | SLEEP (orchestrated) |
| Any → ERROR | Required component failure, WIFI_DISCONNECTED, etc. | ERROR (direct) |
| ERROR → READY | RECOVER | READY (direct) |
| ERROR → SLEEP | ENTER_SLEEP | SLEEP (orchestrated) |
