# Guideline: Modularity and Component Independence

> **Status:** Active
> **Last updated:** 2026-03-28

---

## Purpose

FlushFM 2.0 consists of multiple largely independent components (e.g. streaming service,
audio output, display output, light sensor). This guideline ensures each component can be
developed, tested, and replaced in isolation, without requiring all other components to be
present and functional.

---

## Rules

1. **Each component is encapsulated in its own class** with a clearly defined, minimal public interface.

2. **Components do not directly instantiate their dependencies.** Dependencies are passed in
   from the outside (dependency injection), not created internally with `new` or by calling
   global state.

3. **Dependencies are expressed as abstract interfaces (pure virtual classes),** not as
   concrete implementations. A component only knows the interface it needs, not who implements it.

4. **Each component must be testable in isolation** by substituting its dependencies with simple
   stub or mock implementations.

5. **Components communicate through their interfaces only.** No component reaches into the
   internals of another component.

6. **Lifecycle contract for orchestration:** Components integrated by the orchestrator expose
  `setup()` and may expose `loop()`. `setup()` runs once; `loop()` is optional for
  event-driven components and must not be added only for state polling.

7. **Absence of a dependency must be handled gracefully.** If an optional dependency is not
   available (e.g. no display attached), the component must not crash – it should degrade
   silently or log a warning.

---

## Rationale

On an ESP32, it is common to wire components tightly together, leading to code that can only
be tested as a whole system. For FlushFM 2.0, this would make it hard to:

- Test audio output without a working Wi-Fi/stream
- Test the display without a running station list
- Test the light sensor logic without attached hardware

By enforcing loose coupling through interfaces and dependency injection, each component can
be verified independently – with a stub providing minimal fake input – before wiring
everything together.

This also makes it easier to swap implementations (e.g. replacing one display driver with
another) without touching dependent code.

---

## Exceptions

- Simple utility code (e.g. a helper for unit conversion, string formatting) does not need
  to be wrapped in an interface.
- The top-level application entry point (`main` / `setup` + `loop`) is explicitly responsible
  for wiring components together and is therefore allowed to reference concrete types.
- **Third-party libraries** may be used directly without wrapping them in a custom interface.
  A library is already a stable, versioned contract – abstracting it adds overhead without
  meaningful benefit in most cases.
- **Exception to the exception:** if a third-party library is hardware-specific and can only
  run on real hardware (e.g. a display driver or sensor library tied to a specific chip), a
  thin interface wrapper may be introduced to allow the component to be tested without that
  hardware present. This is a judgment call based on how central the component is and how
  painful hardware-only testing would be.

---

## Examples

```cpp
// Good: AudioOutput depends on an abstract stream interface
class IAudioStream {
public:
    virtual bool hasData() = 0;
    virtual size_t read(uint8_t* buffer, size_t length) = 0;
    virtual ~IAudioStream() = default;
};

class AudioOutput {
public:
    explicit AudioOutput(IAudioStream& stream) : stream_(stream) {}
private:
    IAudioStream& stream_;
};

// In tests: inject a stub
class StubStream : public IAudioStream {
public:
    bool hasData() override { return true; }
    size_t read(uint8_t* buf, size_t len) override { /* fill with silence */ return len; }
};
```

```cpp
// Bad: AudioOutput creates its own stream internally
class AudioOutput {
public:
    AudioOutput() {
        stream_ = new RadioStream("http://...");  // hard-coded dependency, untestable
    }
};
```
