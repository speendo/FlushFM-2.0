#pragma once

#include "IAudioPlayer.h"
#include "audio_runtime.h"
#include "system_controller.h"

class ISystemComponent {
public:
    virtual ~ISystemComponent() = default;
    virtual const char* name() const = 0;
    virtual bool setup() = 0;
    virtual void loop() {}
};

class BoardInfoComponent final : public ISystemComponent {
public:
    const char* name() const override;
    bool setup() override;
};

class WiFiComponent final : public ISystemComponent {
public:
    explicit WiFiComponent(SystemController& system);

    const char* name() const override;
    bool setup() override;

private:
    static void onConnected(void* context);
    static void onDisconnected(void* context);

    SystemController& system_;
};

class AudioRuntimeComponent final : public ISystemComponent {
public:
    AudioRuntimeComponent(IAudioPlayer& audio, SystemController& system);

    const char* name() const override;
    bool setup() override;

private:
    static void onAudioSignal(audio_runtime::Signal signal, void* context);

    IAudioPlayer& audio_;
    SystemController& system_;
};

class CliComponent final : public ISystemComponent {
public:
    CliComponent(IAudioPlayer& audio, SystemController& system);

    const char* name() const override;
    bool setup() override;
    void loop() override;

private:
    IAudioPlayer& audio_;
    SystemController& system_;
};
