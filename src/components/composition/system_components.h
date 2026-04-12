#pragma once

#include <cstdint>

class IAudioPlayer;
class SystemController;

namespace audio_runtime {
enum class Signal;
}

class ISystemComponent {
public:
    explicit ISystemComponent(const char* componentName) : name_(componentName) {}
    virtual ~ISystemComponent() = default;
    const char* name() const { return name_; }
    virtual bool setup() = 0;
    virtual uint32_t setOFF(uint32_t transitionId) = 0;
    virtual uint32_t setIDLE(uint32_t transitionId) = 0;
    virtual uint32_t setSTREAMING(uint32_t transitionId) = 0;
    virtual uint32_t setERROR(uint32_t transitionId) = 0;
    virtual void onTransitionTimeout(uint32_t transitionId) = 0;
    virtual void loop() {}

private:
    const char* name_;
};

class BoardInfoComponent final : public ISystemComponent {
public:
    BoardInfoComponent();
    bool setup() override;
    uint32_t setOFF(uint32_t transitionId) override;
    uint32_t setIDLE(uint32_t transitionId) override;
    uint32_t setSTREAMING(uint32_t transitionId) override;
    uint32_t setERROR(uint32_t transitionId) override;
    void onTransitionTimeout(uint32_t transitionId) override;
};

class WiFiComponent final : public ISystemComponent {
public:
    explicit WiFiComponent(SystemController& system);

    bool setup() override;
    uint32_t setOFF(uint32_t transitionId) override;
    uint32_t setIDLE(uint32_t transitionId) override;
    uint32_t setSTREAMING(uint32_t transitionId) override;
    uint32_t setERROR(uint32_t transitionId) override;
    void onTransitionTimeout(uint32_t transitionId) override;
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
    uint32_t setOFF(uint32_t transitionId) override;
    uint32_t setIDLE(uint32_t transitionId) override;
    uint32_t setSTREAMING(uint32_t transitionId) override;
    uint32_t setERROR(uint32_t transitionId) override;
    void onTransitionTimeout(uint32_t transitionId) override;

private:
    static void onAudioSignal(audio_runtime::Signal signal, void* context);

    IAudioPlayer& audio_;
    SystemController& system_;
};

class CliComponent final : public ISystemComponent {
public:
    CliComponent(IAudioPlayer& audio, SystemController& system);

    bool setup() override;
    uint32_t setOFF(uint32_t transitionId) override;
    uint32_t setIDLE(uint32_t transitionId) override;
    uint32_t setSTREAMING(uint32_t transitionId) override;
    uint32_t setERROR(uint32_t transitionId) override;
    void onTransitionTimeout(uint32_t transitionId) override;
    void loop() override;

private:
    IAudioPlayer& audio_;
    SystemController& system_;
};
