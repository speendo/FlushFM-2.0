# Component Signal-to-Event Mapping

Each component emits local signals that are translated into `SystemEvent` messages posted to the Supervisor Mailbox.

## WiFi Component (`system_components.cpp`)

| Local Signal | Handler | SystemEvent | Expected Supervisor Effect |
|-------------|---------|-------------|---------------------------|
| WiFi connected | `onConnected` | `WIFI_READY` | Registry:WiFi = Ready; in CONNECTING → triggers READY orchestration |
| WiFi disconnected | `onDisconnected` | `WIFI_DISCONNECTED` | Registry:WiFi = Unknown; in READY/LIVE → direct → ERROR |

## Audio Runtime Component (`system_components.cpp`)

| Local Signal | Handler | SystemEvent | Expected Supervisor Effect |
|-------------|---------|-------------|---------------------------|
| `INIT_OK` | audio signal callback | `AUDIO_INIT_OK` | Registry:AudioRuntime = Ready; in CONNECTING → triggers READY orchestration |
| `STREAM_LOST` | audio signal callback | `STREAM_LOST` | in LIVE → direct → ERROR |
| `INIT_FAILED` or any other | audio signal callback | `AUDIO_INIT_FAILED` | Registry:AudioRuntime = Failed; in CONNECTING/LIVE → direct → ERROR |

## CLI Component (`cli.cpp`)

| User Command | SystemEvent | Expected Supervisor Effect |
|-------------|-------------|---------------------------|
| `play` | `PLAY_REQUESTED` | State-dependent: CONNECTING→defer, SLEEP→CONNECTING→READY, LIVE→replay, other→LIVE |
| `stop` | `STOP_REQUESTED` | State-dependent: CONNECTING→cancel, other→READY |
| `reset` | `STOP_REQUESTED` | Same as stop |
| `sleep` | `ENTER_SLEEP` | State-independent: request → SLEEP |

## `main.cpp` (Boot Path)

| Context | SystemEvent | Expected Supervisor Effect |
|---------|-------------|---------------------------|
| Initial boot | `BOOT` | direct → SLEEP |
| Component setup failure | `COMPONENT_SETUP_FAILED` | direct → ERROR (in CONNECTING/READY) |
| Auto-play (boot) | `PLAY_REQUESTED` | State-dependent: SLEEP → CONNECTING → READY → LIVE |
