# Guideline: Testing Strategy

> **Status:** Active  
> **Last updated:** 2026-02-28

---

## Purpose

Defines testing approaches for FlushFM 2.0 that balance test-driven development benefits with microcontroller constraints. Ensures components can be verified without requiring ESP32 hardware for every test, while still validating hardware integration properly.

---

## Rules

### Unit Testing (Off-Target)

1. **All business logic must have unit tests** that run on the development machine using PlatformIO's `native` environment and Unity test framework.

2. **Tests compile for the host platform** (Linux/Mac/Windows), not for ESP32. No test code ever deploys to the microcontroller.

3. **Hardware dependencies are replaced with stubs/mocks** during unit testing. Use the abstract interfaces from the modularity guideline to inject test doubles.

4. **Each component is tested in isolation.** A component's unit tests should not require any other component to be functional.

5. **Unit tests focus on logic, algorithms, and state changes** – not hardware I/O or timing-sensitive behavior.

### Component Testing (On-Target)

6. **Individual hardware components are tested in isolation** on the ESP32 with minimal test programs that verify only that component's functionality.

7. **Component tests are separate PlatformIO build targets** (e.g. `test_lightsensor`, `test_display`) that compile standalone programs focused on one hardware component.

8. **Component tests provide simple feedback** via built-in LED, Serial monitor output, or basic display output to verify the component works as expected.

9. **Component tests do not depend on other components** being present or functional – they test hardware integration for one component at a time.

### Integration Testing (On-Target)

10. **Integration tests verify end-to-end workflows** on the full assembled system (e.g. "light sensor triggers radio on, station plays through speaker").

11. **Document integration test procedures** as checklists in the user story acceptance criteria, not as automated code.

12. **Hardware abstraction layer (HAL) components** may skip unit tests if they are thin wrappers around vendor libraries – test them via component testing instead.

### Test-Driven Development

13. **Write unit tests before implementing business logic** where practical. Hardware setup and component testing may be exploratory and tested manually first.

14. **Red-Green-Refactor cycle:** Write failing test → implement minimal code to pass → refactor → repeat.

15. **Component tests can be written after basic hardware exploration** to lock in verified behavior before building higher-level functionality.**

---

## Rationale

Microcontroller development faces unique testing challenges:
- Limited flash/RAM means no test code on production devices
- Hardware dependencies make automated testing complex
- Real-time constraints are difficult to test deterministically

The three-tier approach (unit → component → integration) addresses these by:
- Testing logic cheaply and repeatably on the development machine
- Verifying individual hardware components work correctly in isolation on ESP32
- Using the modular architecture to isolate testable vs. hardware-bound code  
- Validating full system behavior where it matters most (on real hardware)
- Keeping test execution fast for rapid feedback during development

Unity framework is lightweight, well-integrated with PlatformIO, and designed for embedded systems – making it the natural choice for ESP32 projects.

---

## Exceptions

- **Proof-of-concept code** exploring hardware capabilities may be tested manually first, then refactored to be unit-testable once the approach is proven.
- **Thin wrapper classes** around third-party libraries (e.g. display drivers) may skip unit tests if they contain no business logic.
- **Time-critical real-time code** may be difficult to unit test meaningfully due to timing dependencies.

---

## Examples

```cpp
// Good: Testable component with injected dependencies
class AudioPlayer {
public:
    explicit AudioPlayer(IAudioStream& stream, IAudioOutput& output) 
        : stream_(stream), output_(output) {}
    
    void playNext() {
        if (stream_.hasData()) {
            uint8_t buffer[1024];
            size_t bytes = stream_.read(buffer, sizeof(buffer));
            output_.write(buffer, bytes);
        }
    }
private:
    IAudioStream& stream_;
    IAudioOutput& output_;
};

// Unit test (runs on development machine)
TEST(AudioPlayer, PlaysDataWhenAvailable) {
    MockAudioStream stream;
    MockAudioOutput output;
    AudioPlayer player(stream, output);
    
    stream.setHasData(true);
    stream.setDataToReturn("test audio");
    
    player.playNext();
    
    ASSERT_EQ("test audio", output.getLastWrittenData());
}
```

```cpp
// Component test example (separate PlatformIO target: test_lightsensor)
#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  // Initialize light sensor on analog pin A0
}

void loop() {
  int lightLevel = analogRead(A0);
  Serial.printf("Light level: %d\n", lightLevel);
  
  // LED on when dark (< 100), off when bright
  digitalWrite(LED_BUILTIN, lightLevel < 100 ? HIGH : LOW);
  
  delay(500);
}
```

```cpp
// Bad: Untestable hardware-dependent code
class AudioPlayer {
public:
    void playNext() {
        // Direct hardware access - can't test without ESP32
        if (digitalRead(PIN_STREAM_READY)) {
            // Read from hardcoded I2S peripheral
            // Write to hardcoded DAC
        }
    }
};
```