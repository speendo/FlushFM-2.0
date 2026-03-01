# Guideline: Build System and Project Structure

> **Status:** Active  
> **Last updated:** 2026-03-01

---

## Purpose

Defines the PlatformIO project structure conventions, build configuration best practices, and environment organization for FlushFM 2.0.

---

## Rules

### Project Structure

1. **Follow PlatformIO conventions:** All code organization must adhere to PlatformIO's standard folder layout to ensure proper build behavior and IDE integration.

2. **Required folder structure:**
   ```
   FlushFM 2.0/
   ├── platformio.ini          # PlatformIO configuration
   ├── src/                    # Main source code
   │   ├── main.cpp           # Entry point (setup() and loop())
   │   ├── config.h           # Build-time configuration constants
   │   └── components/        # Application components
   ├── lib/                   # Project-specific libraries
   │   └── ComponentName/     # Each component as separate library
   │       ├── src/          # Component implementation
   │       ├── include/      # Component public headers
   │       └── library.json  # Component metadata
   ├── test/                  # Unit tests (Unity framework)
   │   ├── test_component/   # Per-component test suites
   │   └── test_main.cpp     # Test runner
   ├── data/                 # Filesystem content (LittleFS)
   │   └── www/             # Web UI files
   ├── boards/               # Custom board definitions (if needed)
   └── scripts/              # Build/deployment automation
   ```

3. **Component organization:** Each logical component (AudioPlayer, Display, LightSensor, etc.) is placed as a separate library in `lib/` with its own namespace and clear interface dependencies.

### PlatformIO Configuration

4. **Multiple environments defined in `platformio.ini`:**
   - `production` – Production build for target hardware (see hardware.md)
   - `debug` – Debug build with logging enabled
   - `native` – Unit tests running on host machine
   - `test_*` – Component test builds for hardware verification

5. **Environment-specific settings:**
   ```ini
   [env:production]
   platform = espressif32
   board = <see hardware.md>
   framework = arduino
   monitor_speed = 115200
   build_flags = 
     -DCORE_DEBUG_LEVEL=0
     -DPRODUCTION_BUILD
   
   [env:debug]
   extends = env:production
   build_flags = 
     -DCORE_DEBUG_LEVEL=4
     -DDEBUG_ENABLED
   
   [env:native]
   platform = native
   test_framework = unity
   build_flags = -std=c++17
   ```

6. **Library dependencies:** All external libraries declared in `platformio.ini` with explicit version pinning to ensure reproducible builds:
   ```ini
   lib_deps = 
     schreibfaul1/ESP32-audioI2S@^3.0.0
     bodmer/TFT_eSPI@^2.5.0
     esphome/AsyncTCP-esphome@^2.0.0
   ```

### Build Management

7. **No Arduino IDE compatibility required:** The project is PlatformIO-native and does not need to compile in Arduino IDE. This allows full use of PlatformIO's advanced features.

8. **Custom build scripts:** Complex build steps (web UI asset processing, version embedding) are implemented as Python scripts in `scripts/` directory and integrated via PlatformIO's `extra_scripts` feature.

9. **Firmware versioning:** Git commit hash and build timestamp are embedded in firmware during compilation using build script automation.

10. **Upload configurations:** Different upload methods configured for development convenience:
    - USB serial (default)
    - OTA updates for remote deployment
    - JTAG debugging support

### Testing Integration

11. **Test environments parallel main environments:** Each hardware component test target mirrors the main build configuration to ensure hardware/software compatibility.

12. **Automated testing:** CI/CD integration runs unit tests on every commit using PlatformIO's `pio test` command.

---

## Rationale

**PlatformIO over Arduino IDE:** PlatformIO provides superior dependency management, multiple environment support, professional debugging capabilities, and better integration with modern development tools.

**Component-based lib/ structure:** Keeps the codebase modular and testable while leveraging PlatformIO's library system for dependency management and build optimization.

**Explicit version pinning:** Prevents unexpected breakages from library updates and ensures reproducible builds across different development environments.

**Multiple build environments:** Enables different optimization levels, debug configurations, and testing targets without compromising the production build.

---

## Exceptions

- **Emergency Arduino IDE compatibility:** If PlatformIO becomes unavailable, critical files (`src/main.cpp`, essential libraries) should be structured to allow manual compilation in Arduino IDE as a last resort.

---

## Examples

```ini
; platformio.ini example
[platformio]
default_envs = debug

[common]
lib_deps = 
  schreibfaul1/ESP32-audioI2S@^3.0.0
  bodmer/TFT_eSPI@^2.5.0
  esphome/AsyncTCP-esphome@^2.0.0

[env:production]
platform = espressif32@^6.0.0
board = <see hardware.md>
framework = arduino
monitor_speed = 115200
lib_deps = ${common.lib_deps}
build_flags = 
  -DCORE_DEBUG_LEVEL=0
  -DPRODUCTION_BUILD
  <hardware-specific flags - see hardware.md>
upload_protocol = esptool

[env:debug]
extends = env:production
build_flags = 
  -DCORE_DEBUG_LEVEL=4
  -DDEBUG_ENABLED
  <hardware-specific flags - see hardware.md>

[env:native]
platform = native
test_framework = unity
build_flags = -std=c++17
lib_deps = 
  throwtheswitch/Unity@^2.5.2

[env:test_lightsensor]
extends = env:debug
src_filter = +<components/lightsensor/test/>
build_flags = 
  ${env:debug.build_flags}
  -DCOMPONENT_TEST_LIGHTSENSOR
```