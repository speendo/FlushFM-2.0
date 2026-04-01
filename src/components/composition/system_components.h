#pragma once

#include "IAudioPlayer.h"
#include "components/audio/audio_runtime.h"
#include "state_machine/system_controller.h"

class ISystemComponent {
public:
    explicit ISystemComponent(const char* componentName) : name_(componentName) {}
    virtual ~ISystemComponent() = default;
    const char* name() const { return name_; }
    virtual bool setup() = 0;
    virtual void loop() {}

private:
    const char* name_;
};

class BoardInfoComponent final : public ISystemComponent {
public:
    BoardInfoComponent();
    bool setup() override;
};

class WiFiComponent final : public ISystemComponent {
public:
    explicit WiFiComponent(SystemController& system);

    bool setup() override;
    bool bootAutoConnectSucceeded() const;

private:
    static void onConnected(void* context);
    static void onDisconnected(void* context);

    SystemController& system_;
    bool bootAutoConnectSucceeded_ = false;
};

class AudioRuntimeComponent final : public ISystemComponent {
public:
    AudioRuntimeComponent(IAudioPlayer& audio, SystemController& system);

    bool setup() override;

private:
    static void onAudioSignal(audio_runtime::Signal signal, void* context);

    IAudioPlayer& audio_;
    SystemController& system_;
};

class CliComponent final : public ISystemComponent {
public:
    CliComponent(IAudioPlayer& audio, SystemController& system);

    bool setup() override;
    void loop() override;

private:
    IAudioPlayer& audio_;
    SystemController& system_;
};
