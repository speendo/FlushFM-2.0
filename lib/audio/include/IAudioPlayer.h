#pragma once

#include <stdint.h>

/// Abstract interface for an internet radio / audio streaming player.
///
/// Concrete implementations (e.g. AudioPlayerESP32) are wired in main.cpp.
/// All higher-level code must depend only on this interface, never on a
/// concrete type (→ requirements/guidelines/modularity.md).
class IAudioPlayer {
public:
    enum class RuntimeState {
        SLEEP,
        CONNECTING,
        LIVE,
        ERROR,
    };

    virtual ~IAudioPlayer() = default;

    /// Initialize hardware (I2S pinout, default volume).
    /// Must be called once before any other method.
    /// Returns false if initialization failed.
    virtual bool begin() = 0;

    /// Pump the internal decode/stream loop.
    /// Must be called continuously and with high frequency (Core 1).
    virtual void loop() = 0;

    /// Start streaming from an HTTP(S) URL.
    /// Replaces the current stream if one is already active.
    /// Returns false if the connection could not be initiated.
    virtual bool connectToHost(const char* url) = 0;

    /// Stop the current stream cleanly (flushes internal buffers).
    virtual void stop() = 0;

    /// Mute or unmute audio output.
    virtual void setMute(bool mute) = 0;

    /// Return whether audio output is currently muted.
    virtual bool getMute() = 0;

    /// Set playback volume (0 = mute, N = maximum where N is the configured step count).
    /// Default step count is 21 (ESP32-audioI2S default); can be changed via setVolumeSteps().
    virtual void setVolume(uint8_t volume) = 0;

    /// Configure the number of volume steps (default 21; valid range 21–255).
    /// Call once after begin(), before setVolume().
    virtual void setVolumeSteps(uint8_t steps) = 0;

    /// Return the current volume setting.
    virtual uint8_t getVolume() const = 0;

    /// Set stereo balance (-16 = full left, 0 = center, +16 = full right).
    virtual void setBalance(int8_t balance) = 0;

    /// Return current runtime state for orchestration/transition decisions.
    virtual RuntimeState runtimeState() const = 0;
};
