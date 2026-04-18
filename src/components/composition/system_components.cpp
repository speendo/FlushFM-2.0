#include "components/composition/system_components.h"

#include <string.h>

#include "components/audio/audio_runtime.h"
#include "components/board/board_info.h"
#include "components/cli/cli.h"
#include "core/config.h"
#include "core/debug.h"
#include "settings.h"
#include "components/network/wifi_manager.h"
#include "state_machine/system_controller.h"

namespace {

constexpr const char* kBoardInfoName = "BoardInfo";
constexpr const char* kWiFiName = "WiFi";
constexpr const char* kAudioRuntimeName = "AudioRuntime";
constexpr const char* kCliName = "CLI";

constexpr uint32_t kBoardInfoTimeoutOffMs = 0;
constexpr uint32_t kBoardInfoTimeoutIdleMs = 0;
constexpr uint32_t kBoardInfoTimeoutStreamingMs = 0;
constexpr uint32_t kBoardInfoTimeoutErrorMs = 0;

constexpr uint32_t kWiFiTimeoutOffMs = 1000;
constexpr uint32_t kWiFiTimeoutIdleMs = 8000;
constexpr uint32_t kWiFiTimeoutStreamingMs = 15000;
constexpr uint32_t kWiFiTimeoutErrorMs = 1000;

constexpr uint32_t kAudioTimeoutOffMs = 2000;
constexpr uint32_t kAudioTimeoutIdleMs = 2000;
constexpr uint32_t kAudioTimeoutStreamingMs = 5000;
constexpr uint32_t kAudioTimeoutErrorMs = 1000;

constexpr uint32_t kCliTimeoutOffMs = 0;
constexpr uint32_t kCliTimeoutIdleMs = 0;
constexpr uint32_t kCliTimeoutStreamingMs = 0;
constexpr uint32_t kCliTimeoutErrorMs = 0;

uint32_t invokeComponentTransition(ISystemComponent& component,
                                   SystemState target,
                                   uint32_t transitionId) {
    switch (target) {
        case SystemState::OFF:
            return component.setOFF(transitionId);
        case SystemState::STARTING:
            return component.setIDLE(transitionId);
        case SystemState::READY:
            return component.setIDLE(transitionId);
        case SystemState::LIVE:
            return component.setSTREAMING(transitionId);
        case SystemState::ERROR:
            return component.setERROR(transitionId);
    }

    return component.setERROR(transitionId);
}

}  // namespace

BoardInfoComponent::BoardInfoComponent() : ISystemComponent(kBoardInfoName) {}

void BoardInfoComponent::registerWithController(SystemController& controller) const {
    controller.registerComponent(name(), false);
    controller.setComponentTransitionHooks(
        name(),
        [component = const_cast<BoardInfoComponent*>(this), &controller](SystemState target, uint32_t transitionId) {
            const uint32_t timeoutMs = invokeComponentTransition(*component, target, transitionId);
            (void)controller.reportCompletion(component->name(), transitionId, TransitionStatus::Completed, nullptr);
            return timeoutMs;
        },
        [component = const_cast<BoardInfoComponent*>(this), &controller](uint32_t transitionId) {
            component->onTransitionTimeout(transitionId);
            (void)controller.reportCompletion(component->name(), transitionId, TransitionStatus::Failed, "timeout");
        });
}

bool BoardInfoComponent::setup() {
    board_info::print();
    return true;
}

uint32_t BoardInfoComponent::setOFF(uint32_t transitionId) {
    (void)transitionId;
    return kBoardInfoTimeoutOffMs;
}

uint32_t BoardInfoComponent::setIDLE(uint32_t transitionId) {
    (void)transitionId;
    return kBoardInfoTimeoutIdleMs;
}

uint32_t BoardInfoComponent::setSTREAMING(uint32_t transitionId) {
    (void)transitionId;
    return kBoardInfoTimeoutStreamingMs;
}

uint32_t BoardInfoComponent::setERROR(uint32_t transitionId) {
    (void)transitionId;
    return kBoardInfoTimeoutErrorMs;
}

void BoardInfoComponent::onTransitionTimeout(uint32_t transitionId) {
    DEBUG_LOG(kBoardInfoName, "Transition timeout for id=%lu", static_cast<unsigned long>(transitionId));
}

WiFiComponent::WiFiComponent(SystemController& system)
    : ISystemComponent(kWiFiName), system_(system) {}

void WiFiComponent::registerWithController(SystemController& controller) const {
    controller.registerComponent(name(), true);
    controller.setComponentTransitionHooks(
        name(),
        [component = const_cast<WiFiComponent*>(this)](SystemState target, uint32_t transitionId) {
            return invokeComponentTransition(*component, target, transitionId);
        },
        [component = const_cast<WiFiComponent*>(this)](uint32_t transitionId) {
            component->onTransitionTimeout(transitionId);
        });
}

bool WiFiComponent::setup() {
    wifi_manager::setConnectedCallback(&WiFiComponent::onConnected, this);
    wifi_manager::setDisconnectedCallback(&WiFiComponent::onDisconnected, this);
    wifi_manager::init();

    char ssid[settings::kSsidMaxLen] = {};
    char pass[settings::kPassMaxLen] = {};

    if (settings::loadSsid(ssid, sizeof(ssid))) {
        wifi_manager::setSsid(ssid);
        settings::loadPass(pass, sizeof(pass));
        if (pass[0] != '\0') {
            wifi_manager::setPass(pass);
        }

        PROD_LOG(kWiFiName, "Boot auto-connect requested from persisted settings");
        wifi_manager::connect();
        bootAutoConnectSucceeded_ = (wifi_manager::state() == wifi_manager::WiFiState::CONNECTED);
    }

    return true;
}

uint32_t WiFiComponent::setOFF(uint32_t transitionId) {
    startPendingTransition(false, transitionId);
    wifi_manager::disconnect();
    completePendingTransition(TransitionStatus::Completed, nullptr);
    return kWiFiTimeoutOffMs;
}

uint32_t WiFiComponent::setIDLE(uint32_t transitionId) {
    startPendingTransition(false, transitionId);
    completePendingTransition(TransitionStatus::Completed, nullptr);
    return kWiFiTimeoutIdleMs;
}

uint32_t WiFiComponent::setSTREAMING(uint32_t transitionId) {
    startPendingTransition(true, transitionId);
    if (!wifi_manager::isConnected()) {
        wifi_manager::connect();
    }

    return kWiFiTimeoutStreamingMs;
}

uint32_t WiFiComponent::setERROR(uint32_t transitionId) {
    startPendingTransition(false, transitionId);
    completePendingTransition(TransitionStatus::Completed, nullptr);
    return kWiFiTimeoutErrorMs;
}

void WiFiComponent::onTransitionTimeout(uint32_t transitionId) {
    DEBUG_LOG(kWiFiName, "Transition timeout for id=%lu", static_cast<unsigned long>(transitionId));
    if (transitionPending_ && pendingTransitionId_ == transitionId) {
        completePendingTransition(TransitionStatus::Failed, "timeout");
    }
}

void WiFiComponent::loop() {
    if (!transitionPending_ || !pendingStreamingTarget_) {
        return;
    }

    if (wifi_manager::isConnected()) {
        completePendingTransition(TransitionStatus::Completed, nullptr);
    }
}

bool WiFiComponent::bootAutoConnectSucceeded() const {
    return bootAutoConnectSucceeded_;
}

void WiFiComponent::onConnected(void* context) {
    auto* self = static_cast<WiFiComponent*>(context);
    if (!self) {
        return;
    }

    if (self->transitionPending_ && self->pendingStreamingTarget_) {
        self->completePendingTransition(TransitionStatus::Completed, nullptr);
    }

    self->system_.postEvent(SystemEvent::WIFI_READY, SystemReason::WIFI_INITIALIZED);
}

void WiFiComponent::onDisconnected(void* context) {
    auto* self = static_cast<WiFiComponent*>(context);
    if (!self) {
        return;
    }

    if (self->transitionPending_ && self->pendingStreamingTarget_) {
        self->completePendingTransition(TransitionStatus::Failed, "wifi disconnected");
    }

    self->system_.postEvent(SystemEvent::WIFI_DISCONNECTED, SystemReason::NONE, EventPolicy::BOUNDED_BLOCKING);
}

void WiFiComponent::startPendingTransition(bool streamingTarget, uint32_t transitionId) {
    transitionPending_ = true;
    pendingStreamingTarget_ = streamingTarget;
    pendingTransitionId_ = transitionId;
}

void WiFiComponent::completePendingTransition(TransitionStatus status, const char* reason) {
    if (!transitionPending_) {
        return;
    }

    const uint32_t transitionId = pendingTransitionId_;
    transitionPending_ = false;
    pendingTransitionId_ = 0;
    pendingStreamingTarget_ = false;

    (void)system_.reportCompletion(name(), transitionId, status, reason);
}

AudioRuntimeComponent::AudioRuntimeComponent(IAudioPlayer& audio, SystemController& system)
    : ISystemComponent(kAudioRuntimeName), audio_(audio), system_(system) {}

void AudioRuntimeComponent::registerWithController(SystemController& controller) const {
    controller.registerComponent(name(), true);
    controller.setComponentTransitionHooks(
        name(),
        [component = const_cast<AudioRuntimeComponent*>(this)](SystemState target, uint32_t transitionId) {
            return invokeComponentTransition(*component, target, transitionId);
        },
        [component = const_cast<AudioRuntimeComponent*>(this)](uint32_t transitionId) {
            component->onTransitionTimeout(transitionId);
        });
}

bool AudioRuntimeComponent::setup() {
    audio_runtime::setSignalHandler(&AudioRuntimeComponent::onAudioSignal, this);
    const bool started = audio_runtime::start(audio_);
    if (!started) {
        system_.postEvent(SystemEvent::AUDIO_INIT_FAILED, SystemReason::AUDIO_TASK_INIT_FAILED);
    }
    return started;
}

uint32_t AudioRuntimeComponent::setOFF(uint32_t transitionId) {
    startPendingTransition(false, transitionId);
    pendingErrorTarget_ = false;
    audio_.stop();
    return kAudioTimeoutOffMs;
}

uint32_t AudioRuntimeComponent::setIDLE(uint32_t transitionId) {
    startPendingTransition(false, transitionId);
    pendingErrorTarget_ = false;
    audio_.stop();
    return kAudioTimeoutIdleMs;
}

uint32_t AudioRuntimeComponent::setSTREAMING(uint32_t transitionId) {
    startPendingTransition(true, transitionId);
    pendingErrorTarget_ = false;

    char station[settings::kStationMaxLen] = {};
    if (!settings::loadStation(station, sizeof(station)) || station[0] == '\0') {
        completePendingTransition(TransitionStatus::Failed, "no station configured");
        return kAudioTimeoutStreamingMs;
    }

    if (!audio_.connectToHost(station)) {
        completePendingTransition(TransitionStatus::Failed, "audio connect failed");
    }

    return kAudioTimeoutStreamingMs;
}

uint32_t AudioRuntimeComponent::setERROR(uint32_t transitionId) {
    startPendingTransition(false, transitionId);
    pendingErrorTarget_ = true;
    audio_.stop();
    return kAudioTimeoutErrorMs;
}

void AudioRuntimeComponent::onTransitionTimeout(uint32_t transitionId) {
    DEBUG_LOG(kAudioRuntimeName, "Transition timeout for id=%lu", static_cast<unsigned long>(transitionId));
    if (transitionPending_ && pendingTransitionId_ == transitionId) {
        audio_.stop();
        completePendingTransition(TransitionStatus::Failed, "timeout");
    }
}

void AudioRuntimeComponent::loop() {
    if (!transitionPending_) {
        return;
    }

    const IAudioPlayer::RuntimeState runtimeState = audio_.runtimeState();
    if (pendingStreamingTarget_) {
        if (runtimeState == IAudioPlayer::RuntimeState::STREAMING) {
            completePendingTransition(TransitionStatus::Completed, nullptr);
        } else if (runtimeState == IAudioPlayer::RuntimeState::ERROR) {
            completePendingTransition(TransitionStatus::Failed, "audio runtime error");
        }
        return;
    }

    // For stop-like targets (OFF/IDLE/ERROR), completion means stream teardown is observable.
    if (runtimeState == IAudioPlayer::RuntimeState::IDLE) {
        completePendingTransition(TransitionStatus::Completed, nullptr);
    } else if (runtimeState == IAudioPlayer::RuntimeState::ERROR && !pendingErrorTarget_) {
        completePendingTransition(TransitionStatus::Failed, "audio stop failed");
    }
}

void AudioRuntimeComponent::onAudioSignal(audio_runtime::Signal signal, void* context) {
    auto* self = static_cast<AudioRuntimeComponent*>(context);
    if (!self) {
        return;
    }

    if (signal == audio_runtime::Signal::INIT_OK) {
        if (self->transitionPending_ && self->pendingStreamingTarget_) {
            self->completePendingTransition(TransitionStatus::Completed, nullptr);
        }
        self->system_.postEvent(SystemEvent::AUDIO_INIT_OK, SystemReason::AUDIO_TASK_STARTED);
    } else if (signal == audio_runtime::Signal::STREAM_LOST) {
        if (self->transitionPending_ && self->pendingStreamingTarget_) {
            self->completePendingTransition(TransitionStatus::Failed, "stream lost");
        }
        self->system_.postEvent(SystemEvent::STREAM_LOST, SystemReason::NONE, EventPolicy::BOUNDED_BLOCKING);
    } else {
        if (self->transitionPending_ && self->pendingStreamingTarget_) {
            self->completePendingTransition(TransitionStatus::Failed, "audio init failed");
        }
        self->system_.postEvent(SystemEvent::AUDIO_INIT_FAILED, SystemReason::AUDIO_TASK_INIT_FAILED);
    }
}

void AudioRuntimeComponent::startPendingTransition(bool streamingTarget, uint32_t transitionId) {
    transitionPending_ = true;
    pendingStreamingTarget_ = streamingTarget;
    pendingTransitionId_ = transitionId;
}

void AudioRuntimeComponent::completePendingTransition(TransitionStatus status, const char* reason) {
    if (!transitionPending_) {
        return;
    }

    const uint32_t transitionId = pendingTransitionId_;
    transitionPending_ = false;
    pendingTransitionId_ = 0;
    pendingStreamingTarget_ = false;
    pendingErrorTarget_ = false;

    (void)system_.reportCompletion(name(), transitionId, status, reason);
}

CliComponent::CliComponent(IAudioPlayer& audio, SystemController& system)
    : ISystemComponent(kCliName), audio_(audio), system_(system) {}

void CliComponent::registerWithController(SystemController& controller) const {
    controller.registerComponent(name(), false);
    controller.setComponentTransitionHooks(
        name(),
        [component = const_cast<CliComponent*>(this), &controller](SystemState target, uint32_t transitionId) {
            const uint32_t timeoutMs = invokeComponentTransition(*component, target, transitionId);
            (void)controller.reportCompletion(component->name(), transitionId, TransitionStatus::Completed, nullptr);
            return timeoutMs;
        },
        [component = const_cast<CliComponent*>(this), &controller](uint32_t transitionId) {
            component->onTransitionTimeout(transitionId);
            (void)controller.reportCompletion(component->name(), transitionId, TransitionStatus::Failed, "timeout");
        });
}

bool CliComponent::setup() {
    cli::init(audio_, audio_runtime::taskHandlePtr(), &system_);
    system_.subscribe([this](SystemState state) {
        PROD_LOG(kCliName, "Observed state change: %s", toString(state));
    });
    cli::printHelp();
    return true;
}

uint32_t CliComponent::setOFF(uint32_t transitionId) {
    (void)transitionId;
    return kCliTimeoutOffMs;
}

uint32_t CliComponent::setIDLE(uint32_t transitionId) {
    (void)transitionId;
    return kCliTimeoutIdleMs;
}

uint32_t CliComponent::setSTREAMING(uint32_t transitionId) {
    (void)transitionId;
    return kCliTimeoutStreamingMs;
}

uint32_t CliComponent::setERROR(uint32_t transitionId) {
    (void)transitionId;
    return kCliTimeoutErrorMs;
}

void CliComponent::onTransitionTimeout(uint32_t transitionId) {
    DEBUG_LOG(kCliName, "Transition timeout for id=%lu", static_cast<unsigned long>(transitionId));
}

void CliComponent::loop() {
    static char cmdBuf[SERIAL_CMD_BUF_SIZE];
    if (cli::readLine(cmdBuf, sizeof(cmdBuf))) {
        // Process command first so transitions are posted only for valid user input.
        cli::process(cmdBuf);
    }
}
