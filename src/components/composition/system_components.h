#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>

#include "component_types.h"

inline constexpr ComponentStateMatrix kWiFiStateMatrix[] = {
    {0, 0, 100, 100},
    {30, 30, 1000, 500},
    {20, 50, 1000, 500},
    {30, 40, 2000, 500},
    {40, 50, 8000, 1000},
    {50, TARGET_MODE, 5000, 500},
    {50, TARGET_MODE, 15000, 1000},
};

inline constexpr ComponentStateMatrix kAudioStateMatrix[] = {
    {0, 0, 100, 100},
    {30, 30, 1000, 500},
    {20, 50, 2000, 500},
    {30, 40, 2000, 500},
    {40, 50, 2000, 1000},
    {50, TARGET_MODE, 2000, 500},
    {50, TARGET_MODE, 5000, 1000},
};

inline constexpr ComponentStateMatrix kCliStateMatrix[] = {
    {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0},
    {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0},
};

inline constexpr ComponentStateMatrix kBoardInfoStateMatrix[] = {
    {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0},
    {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0},
};

class IAudioPlayer;
class Supervisor;

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
    virtual void registerWithController(Supervisor& controller) const = 0;
    virtual bool setup() = 0;
    virtual uint32_t setOFF(uint32_t transitionId) = 0;
    virtual uint32_t setIDLE(uint32_t transitionId) = 0;
    virtual uint32_t setSTREAMING(uint32_t transitionId) = 0;
    virtual uint32_t setERROR(uint32_t transitionId) = 0;
    virtual void onTransitionTimeout(uint32_t transitionId) = 0;
    virtual void loop() {}
    virtual const ComponentStateMatrix* getStateMatrix() const { return nullptr; }
    virtual size_t getStateMatrixSize() const { return 0; }

private:
    const ComponentID id_;
    const char* name_;
};

class BoardInfoComponent final : public ISystemComponent {
public:
    BoardInfoComponent();
    void registerWithController(Supervisor& controller) const override;
    bool setup() override;
    uint32_t setOFF(uint32_t transitionId) override;
    uint32_t setIDLE(uint32_t transitionId) override;
    uint32_t setSTREAMING(uint32_t transitionId) override;
    uint32_t setERROR(uint32_t transitionId) override;
    void onTransitionTimeout(uint32_t transitionId) override;
    const ComponentStateMatrix* getStateMatrix() const override { return kBoardInfoStateMatrix; }
    size_t getStateMatrixSize() const override { return std::size(kBoardInfoStateMatrix); }
};

class WiFiComponent final : public ISystemComponent {
public:
    explicit WiFiComponent(Supervisor& system);

    void registerWithController(Supervisor& controller) const override;
    bool setup() override;
    uint32_t setOFF(uint32_t transitionId) override;
    uint32_t setIDLE(uint32_t transitionId) override;
    uint32_t setSTREAMING(uint32_t transitionId) override;
    uint32_t setERROR(uint32_t transitionId) override;
    void onTransitionTimeout(uint32_t transitionId) override;
    void loop() override;
    bool bootAutoConnectSucceeded() const;
    const ComponentStateMatrix* getStateMatrix() const override { return kWiFiStateMatrix; }
    size_t getStateMatrixSize() const override { return std::size(kWiFiStateMatrix); }

private:
    static void onConnected(void* context);
    static void onDisconnected(void* context);
    void startPendingTransition(bool streamingTarget, uint32_t transitionId);
    void completePendingTransition(TransitionStatus status, const char* reason);

    Supervisor& system_;
    bool bootAutoConnectSucceeded_ = false;
    bool transitionPending_ = false;
    bool pendingStreamingTarget_ = false;
    uint32_t pendingTransitionId_ = 0;
};

class AudioRuntimeComponent final : public ISystemComponent {
public:
    AudioRuntimeComponent(IAudioPlayer& audio, Supervisor& system);

    void registerWithController(Supervisor& controller) const override;
    bool setup() override;
    uint32_t setOFF(uint32_t transitionId) override;
    uint32_t setIDLE(uint32_t transitionId) override;
    uint32_t setSTREAMING(uint32_t transitionId) override;
    uint32_t setERROR(uint32_t transitionId) override;
    void onTransitionTimeout(uint32_t transitionId) override;
    void loop() override;
    const ComponentStateMatrix* getStateMatrix() const override { return kAudioStateMatrix; }
    size_t getStateMatrixSize() const override { return std::size(kAudioStateMatrix); }

private:
    static void onAudioSignal(audio_runtime::Signal signal, void* context);
    void startPendingTransition(bool streamingTarget, uint32_t transitionId);
    void completePendingTransition(TransitionStatus status, const char* reason);

    IAudioPlayer& audio_;
    Supervisor& system_;
    bool transitionPending_ = false;
    bool pendingStreamingTarget_ = false;
    uint32_t pendingTransitionId_ = 0;
    bool pendingErrorTarget_ = false;
};

class CliComponent final : public ISystemComponent {
public:
    CliComponent(IAudioPlayer& audio, Supervisor& system);

    void registerWithController(Supervisor& controller) const override;
    bool setup() override;
    uint32_t setOFF(uint32_t transitionId) override;
    uint32_t setIDLE(uint32_t transitionId) override;
    uint32_t setSTREAMING(uint32_t transitionId) override;
    uint32_t setERROR(uint32_t transitionId) override;
    void onTransitionTimeout(uint32_t transitionId) override;
    void loop() override;
    const ComponentStateMatrix* getStateMatrix() const override { return kCliStateMatrix; }
    size_t getStateMatrixSize() const override { return std::size(kCliStateMatrix); }

private:
    IAudioPlayer& audio_;
    Supervisor& system_;
};
