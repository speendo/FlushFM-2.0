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

## Planned

- Station and track info on ILI9341 display
- Automatic on/off via TEMT6000 light sensor
- Web configuration UI (ESPAsyncWebServer + LittleFS)
