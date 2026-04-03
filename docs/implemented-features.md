# Implemented Features

## Internet Radio Streaming (US-0001)

- WiFi connection via runtime Serial CLI (credentials never stored in code)
- HTTP audio stream playback via ESP32-audioI2S (MP3, AAC)
- Stream metadata display: station name, track title, bitrate
- Stop / restart / switch streams without device reset
- Graceful WiFi reconnection after brief network interruption

## Audio Output via PCM5102A I2S DAC (US-0002)

- `IAudioPlayer` interface + `AudioPlayerESP32` wrapper (`lib/audio/`)
- I2S output to PCM5102A DAC (BCK=4, WS=5, DOUT=6); line-level output for headphones
- Runtime volume control via `volume [0-N]` Serial command; step count configurable via `AUDIO_VOLUME_STEPS` in `config.h` (default 21)
- Runtime mute control via `IAudioPlayer::setMute()` / native `Audio::setMute()`
- Runtime stereo balance control via `balance <-16..16>` Serial command
- Clean stream switching and stop verified; stereo L/R separation verified
- Audio task pinned to Core 0, priority 2, with `vTaskDelay(pdMS_TO_TICKS(1))` to keep IDLE fed

## Core Pinning Verification (US-0003)

- `audioTask` pinned to Core 1 (`AUDIO_TASK_CORE = 1`, priority 2)
- `tasks` Serial command reports core, priority and stack HWM of `audioTask` (observed: 5892 DW free)
- `loadtest` Serial command runs 5s busy-loop on Core 0 – no audio dropouts observed
- `suspend` / `resume` Serial commands verified: clean recovery, no reboot
- Framework tasks (WiFi/TCP/IDLE) on Core 0 are expected and accepted per `concurrency.md`
- DMA drain behavior documented: audio continues for several seconds after `vTaskSuspend()` – tracked in US-0006
- Serial CLI hardened: `readLine` now discards non-printable characters (fixes spurious "Unknown command" errors from escape sequences)

## Persistent Settings via NVS (US-0006)

- Added dedicated `Settings` module under `lib/settings/` using `Preferences` namespace `flushfm`
- Persist `ssid` / `pass` on CLI commands and restore them on boot for automatic WiFi connect
- Persist last station URL on `play` / `switch` and auto-start playback after successful boot auto-connect
- `play` command works without URL argument – loads last station from NVS; falls back to usage error if no station saved
- Added `forget` command to clear persisted `ssid` / `pass` / `station` from NVS
- Added `reset` command to clear only runtime session state (stop stream, disconnect WiFi, clear volatile credentials)
- Password values are never logged in plaintext

## State-Machine Hardening (US-0004a to US-0004f)

- Event-driven state ownership centralized in `SystemController`
- Clear transition handling for OFF, STARTING, IDLE, STREAMING, and ERROR
- Observer-based architecture prepared for additional components (for example display and LED)
- Runtime behavior aligned with resilience goals for reconnect and restart scenarios

## Runtime Logging Cleanup (US-0010)

- Reduced repetitive callback log noise for better debug readability
- Kept relevant state and error information visible for diagnostics

## Library Callback Log Tier Alignment (US-0005)

- ESP32-audioI2S informational callbacks are now routed through `DEBUG_LOG` only
- Callback message mapping is centralized in a small hardware-free helper (`lib/audio_callback_logging/`) to support native unit tests
- Production builds remain silent for callback diagnostics, while debug builds keep full callback visibility

## Planned Features

- Station and track info on ILI9341 display
- Automatic on/off via TEMT6000 light sensor
- LED state visualization mapped to system state (US-0008)
- Web configuration UI (ESPAsyncWebServer + LittleFS)
