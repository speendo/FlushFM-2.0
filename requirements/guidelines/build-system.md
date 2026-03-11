# Rule: [Build System and Project Structure]
[Status: Active | Updated: 2026-03-08]
**Context:** ESP32, PlatformIO, LittleFS | **Goal:** Ensure modularity, reproducible builds and environment separation

---

## 1. Core Rules
- **Folder Layout:** Follow PlatformIO standards; `src/main.cpp` is entry point only
- **Component Isolation:** Each logic unit (e.g. AudioPlayer, Display, LightSensor) **must** be a standalone library in `lib/<ComponentName>/` (→ `modularity.md`)
- **Building Environments & Flags:** Extend `[common]` in `platformio.ini`:
    - `[env:production]`: `-DCORE_DEBUG_LEVEL=0`, `-DPRODUCTION_BUILD`
    - `[env:debug]`: `-DDEBUG_ENABLED` (→ `debug.md`)
    - `[env:native]`: Host-side unit tests only (→ `testing.md`)
    - `[env:test_*]`: Component-specific hardware verification (→ `testing.md`)
- **Dependency Pinning:** Use explicit version tags or commit hashes; never use `latest`
- **Web UI:** Assets reside in `data/www/` for LittleFS image generation
- **PlatformIO-native:** Arduino IDE compatibility not required
- **Firmware Versioning:** Git commit hash and build timestamp are embedded in firmware during compilation using build script automation
- **Automation:** Use Python in `scripts/` via `extra_scripts` for versioning (Git hash/timestamp) and asset processing
- **CI/CD:** Run `pio test` on every commit; test environments must mirror main build configs

## 2. Constraints & Exceptions
- **Limit:** Use arduino framework for ESP32
- **Never:** Place component logic in `src/` (`src/` only for orchestration/setup)

## 3. Reference Pattern

- **Project Structure:**
     ```
     FlushFM 2.0/
     ├── platformio.ini
     ├── src/
     │     ├── main.cpp                    <-- Minimal orchestration
     │     └── config.h
     ├── lib/
     │     └── LightSensor/            <-- Component as Library
     │             ├── src/
     │             ├── include/
     │             └── library.json
     ├── test/
     │     ├── test_component/
     │     └── test_main.cpp
     ├── data/www/                        <-- LittleFS assets
     └── scripts/                            <-- Python automation
     ```