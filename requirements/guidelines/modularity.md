# Rule: Modularity and Component Independence
[Status: Active | Updated: 2026-03-28]
**Context:** C++, OOP, Dependency Injection | **Goal:** Enable isolated testing and hardware abstraction

---

## 1. Core Rules
- **Encapsulation:** Wrap each functional domain (Audio, Display, Sensors) in a dedicated class with a minimal public interface; each domain is a standalone library under `lib/` (→ `build-system.md`)
- **Dependency Injection (DI):** Pass dependencies into constructors; never instantiate them internally via `new` or global state
- **Abstractions:** Use pure virtual classes (Interfaces) to define what a component needs, not who provides it
- **Isolation:** Ensure each component is testable by substituting real hardware with stubs/mocks
- **Interaction:** Communicate exclusively through public interfaces; use FreeRTOS queues for cross-core messages, callbacks for same-core observation (→ `concurrency.md`, `state-management.md`); never reach into another component's internals
- **Lifecycle Contract:** Components integrated by orchestration must expose `setup()` and may expose `loop()`; `setup()` is called once by the orchestrator, `loop()` is optional for event-driven components and must not be forced for state polling
- **Graceful Degradation:** Handle optional or missing dependencies (e.g., disconnected display) without crashing

## 2. Constraints & Exceptions
- **Limit:** Wrap third-party hardware libraries in thin interfaces if they block unit testing on a host machine (e.g., TFT drivers)
- **Never:** Hard-code specific implementations or use Singletons within core logic components
- **Never:** Share internal state across components; use getters or event-callbacks/queues
- **Exception:** The top-level `setup()` and `main.cpp` are allowed to reference concrete types to wire the system together
- **Exception:** Simple utility functions (math, formatting) do not require interfaces
- **Exception:** Standard libraries or stable, non-hardware-locked 3rd party tools (e.g., `ArduinoJson`) can be used directly

## 3. Reference Pattern
```cpp
// 1. Interface (Contract)
class IAudioSource {
public:
    virtual size_t read(uint8_t* buffer, size_t len) = 0;
    virtual ~IAudioSource() = default;
};

// 2. Component using DI
class AudioPlayer {
public:
    explicit AudioPlayer(IAudioSource& source) : _source(source) {}
    void process() { _source.read(buf, 512); }
private:
    IAudioSource& _source; // Depends on abstraction
};

// 3. Concrete Implementation (Wiring in main.cpp)
class WebStream : public IAudioSource { ... };
WebStream radio;
AudioPlayer player(radio);