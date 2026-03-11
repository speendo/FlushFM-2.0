# Rule: Testing Strategy
[Status: Active | Updated: 2026-03-11]
**Context:** ESP32, PlatformIO, Unity | **Goal:** Verify logic off-target and hardware on-target without test code in production

---

## 1. Core Rules

### Unit Tests (Off-Target)
- **Framework:** Use Unity via PlatformIO `[env:native]` (→ `build-system.md`); all unit tests compile and run on the host machine only
- **Coverage:** Every component with business logic must have unit tests; hardware-bound I/O is excluded
- **Isolation:** Each component is tested independently; inject mocks/stubs via interfaces instead of real hardware (→ `modularity.md`)
- **TDD:** Write failing test first → implement minimal passing code → refactor

### Component Tests (On-Target)
- **Scope:** One hardware component per test binary; verify physical integration only (wiring, driver init, signal)
- **Build Targets:** Each component test is a separate PlatformIO `[env:test_<component>]` target (→ `build-system.md`)
- **Feedback:** Use Serial output or built-in LED; no dependence on other components

### Integration Tests (On-Target)
- **Scope:** End-to-end workflows on the fully assembled system (e.g. LDR triggers radio playback)
- **Format:** Document as acceptance-criteria checklists in user stories; not automated code

## 2. Constraints & Exceptions
- **Never:** Deploy test code to a production build; `[env:production]` must contain no Unity or test symbols
- **Never:** Write unit tests that `#include` hardware headers (e.g. `Arduino.h`, `driver/i2s.h`) directly
- **Exception:** Thin wrapper classes around vendor libraries (e.g. TFT driver) may skip unit tests if they contain no business logic; cover via component test instead
- **Exception:** Proof-of-concept hardware exploration may be manual first; add unit tests before merging to main flow

## 3. Reference Pattern

```cpp
// IAudioStream interface (defined in component header – testable via DI)
class IAudioStream {
public:
    virtual bool hasData() = 0;
    virtual size_t read(uint8_t* buf, size_t len) = 0;
    virtual ~IAudioStream() = default;
};

// Unit test (native env – no ESP32 required)
#include <unity.h>
#include "AudioPlayer.h"
#include "MockAudioStream.h"
#include "MockAudioOutput.h"

void test_plays_data_when_available() {
    MockAudioStream stream;
    MockAudioOutput output;
    AudioPlayer player(stream, output);

    stream.setHasData(true);
    stream.setData("audio");

    player.playNext();

    TEST_ASSERT_EQUAL_STRING("audio", output.lastWritten());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_plays_data_when_available);
    return UNITY_END();
}
```
