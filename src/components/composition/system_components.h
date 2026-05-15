#pragma once

#include <cstddef>
#include <cstdint>

#include "component_types.h"

class IAudioPlayer;

namespace audio_runtime {
enum class Signal;
}

class ISystemComponent {
public:
    explicit ISystemComponent(ComponentID componentId, const char* componentName)
        : id_(componentId), name_(componentName) {}
    virtual ~ISystemComponent() = default;
    ComponentID id() const { return id_; }
    const char* name() const { return name_; }
    virtual bool setup() = 0;
    virtual uint32_t setOFF(uint32_t transitionId) = 0;
    virtual uint32_t setIDLE(uint32_t transitionId) = 0;
    virtual uint32_t setSTREAMING(uint32_t transitionId) = 0;
    virtual uint32_t setERROR(uint32_t transitionId) = 0;
    virtual void onTransitionTimeout(uint32_t transitionId) = 0;
    virtual void loop() {}

private:
    const ComponentID id_;
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
    void loop() override;

    ComponentMailbox supervisorV2Mailbox;
};

class WiFiComponent final : public ISystemComponent {
public:
    WiFiComponent();

    bool setup() override;
    uint32_t setOFF(uint32_t transitionId) override;
    uint32_t setIDLE(uint32_t transitionId) override;
    uint32_t setSTREAMING(uint32_t transitionId) override;
    uint32_t setERROR(uint32_t transitionId) override;
    void onTransitionTimeout(uint32_t transitionId) override;
    void loop() override;
    bool bootAutoConnectSucceeded() const;

private:
    static void onConnected(void* context);
    static void onDisconnected(void* context);
    void startPendingTransition(bool streamingTarget, uint32_t transitionId);
    void completePendingTransition(TransitionStatus status, const char* reason);

    ComponentMailbox supervisorV2Mailbox;
    bool bootAutoConnectSucceeded_ = false;
    bool transitionPending_ = false;
    bool pendingStreamingTarget_ = false;
    uint32_t pendingTransitionId_ = 0;
};

class AudioRuntimeComponent final : public ISystemComponent {
public:
    AudioRuntimeComponent(IAudioPlayer& audio);

    bool setup() override;
    uint32_t setOFF(uint32_t transitionId) override;
    uint32_t setIDLE(uint32_t transitionId) override;
    uint32_t setSTREAMING(uint32_t transitionId) override;
    uint32_t setERROR(uint32_t transitionId) override;
    void onTransitionTimeout(uint32_t transitionId) override;
    void loop() override;

private:
    static void onAudioSignal(audio_runtime::Signal signal, void* context);
    void startPendingTransition(bool streamingTarget, uint32_t transitionId);
    void completePendingTransition(TransitionStatus status, const char* reason);

    IAudioPlayer& audio_;
    ComponentMailbox supervisorV2Mailbox;
    bool transitionPending_ = false;
    bool pendingStreamingTarget_ = false;
    uint32_t pendingTransitionId_ = 0;
    bool pendingErrorTarget_ = false;
};

class CliComponent final : public ISystemComponent {
public:
    CliComponent(IAudioPlayer& audio);

    bool setup() override;
    uint32_t setOFF(uint32_t transitionId) override;
    uint32_t setIDLE(uint32_t transitionId) override;
    uint32_t setSTREAMING(uint32_t transitionId) override;
    uint32_t setERROR(uint32_t transitionId) override;
    void onTransitionTimeout(uint32_t transitionId) override;
    void loop() override;

private:
    IAudioPlayer& audio_;
    ComponentMailbox supervisorV2Mailbox;
};
