#include "components/composition/system_components.h"

#include "components/audio/audio_runtime.h"
#include "components/board/board_info.h"
#include "components/cli/cli.h"
#include "core/config.h"
#include "core/debug.h"
#include "settings.h"
#include "components/network/wifi_manager.h"
#include "supervisor/supervisor_v2.h"

extern SupervisorV2 s_supervisorV2;

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

}  // namespace

BoardInfoComponent::BoardInfoComponent() : ISystemComponent(ComponentID::BoardInfo, kBoardInfoName) {}

bool BoardInfoComponent::setup() {
    board_info::print();
    s_supervisorV2.registerComponent(
        id(), &const_cast<BoardInfoComponent*>(this)->supervisorV2Mailbox, false);
    return true;
}

void BoardInfoComponent::loop() {
    SystemState target;
    if (!supervisorV2Mailbox.consumeNextState(target)) return;

    switch (target) {
        case SystemState::SLEEP:     setOFF(0); break;
        case SystemState::READY:     setIDLE(0); break;
        case SystemState::LIVE:      setSTREAMING(0); break;
        case SystemState::ERROR:
        case SystemState::FATAL:     setERROR(0); break;
        default: return;
    }
    s_supervisorV2.completeTransition(id(), TransitionStatus::Completed);
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
    (void)transitionId;
}

WiFiComponent::WiFiComponent()
    : ISystemComponent(ComponentID::WiFi, kWiFiName) {}

bool WiFiComponent::setup() {
    s_supervisorV2.registerComponent(
        id(), &const_cast<WiFiComponent*>(this)->supervisorV2Mailbox, true);

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
    completePendingTransition(TransitionStatus::Completed);
    return kWiFiTimeoutOffMs;
}

uint32_t WiFiComponent::setIDLE(uint32_t transitionId) {
    startPendingTransition(false, transitionId);
    completePendingTransition(TransitionStatus::Completed);
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
    completePendingTransition(TransitionStatus::Completed);
    return kWiFiTimeoutErrorMs;
}

void WiFiComponent::onTransitionTimeout(uint32_t transitionId) {
    (void)transitionId;
    if (transitionPending_ && pendingTransitionId_ == transitionId) {
        completePendingTransition(TransitionStatus::Failed);
    }
}

void WiFiComponent::loop() {
    SystemState target;
    if (supervisorV2Mailbox.consumeNextState(target)) {
        switch (target) {
            case SystemState::SLEEP:       setOFF(0); break;
            case SystemState::READY:       setIDLE(0); break;
            case SystemState::CONNECTING:
            case SystemState::LIVE:        setSTREAMING(0); break;
            case SystemState::ERROR:
            case SystemState::FATAL:       setERROR(0); break;
            default: break;
        }
    }

    if (!transitionPending_ || !pendingStreamingTarget_) {
        return;
    }

    if (wifi_manager::isConnected()) {
        completePendingTransition(TransitionStatus::Completed);
    }
}

bool WiFiComponent::bootAutoConnectSucceeded() const {
    return bootAutoConnectSucceeded_;
}

void WiFiComponent::onConnected(void* context) {
    auto* self = static_cast<WiFiComponent*>(context);
    if (!self) return;
    if (self->transitionPending_ && self->pendingStreamingTarget_) {
        self->completePendingTransition(TransitionStatus::Completed);
    }
}

void WiFiComponent::onDisconnected(void* context) {
    auto* self = static_cast<WiFiComponent*>(context);
    if (!self) return;
    if (self->transitionPending_ && self->pendingStreamingTarget_) {
        self->completePendingTransition(TransitionStatus::Failed);
    } else {
        s_supervisorV2.postErrorEvent("wifi disconnected", ComponentID::WiFi);
    }
}

void WiFiComponent::startPendingTransition(bool streamingTarget, uint32_t transitionId) {
    transitionPending_ = true;
    pendingStreamingTarget_ = streamingTarget;
    pendingTransitionId_ = transitionId;
}

void WiFiComponent::completePendingTransition(TransitionStatus status) {
    if (!transitionPending_) return;
    transitionPending_ = false;
    s_supervisorV2.completeTransition(id(), status);
}

AudioRuntimeComponent::AudioRuntimeComponent(IAudioPlayer& audio)
    : ISystemComponent(ComponentID::AudioRuntime, kAudioRuntimeName), audio_(audio) {}

bool AudioRuntimeComponent::setup() {
    s_supervisorV2.registerComponent(
        id(), &const_cast<AudioRuntimeComponent*>(this)->supervisorV2Mailbox, true);

    audio_runtime::setSignalHandler(&AudioRuntimeComponent::onAudioSignal, this);
    const bool started = audio_runtime::start(audio_);
    if (!started) {
        s_supervisorV2.postErrorEvent("audio task init failed", ComponentID::AudioRuntime);
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
        completePendingTransition(TransitionStatus::Failed);
        return kAudioTimeoutStreamingMs;
    }

    if (!audio_.connectToHost(station)) {
        completePendingTransition(TransitionStatus::Failed);
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
    (void)transitionId;
    if (transitionPending_ && pendingTransitionId_ == transitionId) {
        audio_.stop();
        completePendingTransition(TransitionStatus::Failed);
    }
}

void AudioRuntimeComponent::loop() {
    SystemState target;
    if (supervisorV2Mailbox.consumeNextState(target)) {
        switch (target) {
            case SystemState::SLEEP:       setOFF(0); break;
            case SystemState::READY:       setIDLE(0); break;
            case SystemState::CONNECTING:
            case SystemState::LIVE:        setSTREAMING(0); break;
            case SystemState::ERROR:
            case SystemState::FATAL:       setERROR(0); break;
            default: break;
        }
    }

    if (!transitionPending_) return;

    const IAudioPlayer::RuntimeState runtimeState = audio_.runtimeState();
    if (pendingStreamingTarget_) {
        if (runtimeState == IAudioPlayer::RuntimeState::LIVE) {
            completePendingTransition(TransitionStatus::Completed);
        } else if (runtimeState == IAudioPlayer::RuntimeState::ERROR) {
            completePendingTransition(TransitionStatus::Failed);
        }
        return;
    }

    if (runtimeState == IAudioPlayer::RuntimeState::SLEEP) {
        completePendingTransition(TransitionStatus::Completed);
    } else if (runtimeState == IAudioPlayer::RuntimeState::ERROR && !pendingErrorTarget_) {
        completePendingTransition(TransitionStatus::Failed);
    }
}

void AudioRuntimeComponent::onAudioSignal(audio_runtime::Signal signal, void* context) {
    auto* self = static_cast<AudioRuntimeComponent*>(context);
    if (!self) return;

    if (signal == audio_runtime::Signal::INIT_OK) {
        if (self->transitionPending_ && self->pendingStreamingTarget_) {
            self->completePendingTransition(TransitionStatus::Completed);
        }
    } else if (signal == audio_runtime::Signal::STREAM_LOST) {
        if (self->transitionPending_ && self->pendingStreamingTarget_) {
            self->completePendingTransition(TransitionStatus::Failed);
        } else {
            s_supervisorV2.postErrorEvent("stream lost", ComponentID::AudioRuntime);
        }
    } else {
        if (self->transitionPending_ && self->pendingStreamingTarget_) {
            self->completePendingTransition(TransitionStatus::Failed);
        } else {
            s_supervisorV2.postErrorEvent("audio init failed", ComponentID::AudioRuntime);
        }
    }
}

void AudioRuntimeComponent::startPendingTransition(bool streamingTarget, uint32_t transitionId) {
    transitionPending_ = true;
    pendingStreamingTarget_ = streamingTarget;
    pendingTransitionId_ = transitionId;
}

void AudioRuntimeComponent::completePendingTransition(TransitionStatus status) {
    if (!transitionPending_) return;
    transitionPending_ = false;
    s_supervisorV2.completeTransition(id(), status);
}

CliComponent::CliComponent(IAudioPlayer& audio)
    : ISystemComponent(ComponentID::CLI, kCliName), audio_(audio) {}

bool CliComponent::setup() {
    s_supervisorV2.registerComponent(
        id(), &const_cast<CliComponent*>(this)->supervisorV2Mailbox, false);
    cli::init(audio_, audio_runtime::taskHandlePtr(), &s_supervisorV2);
    cli::printHelp();
    return true;
}

void CliComponent::loop() {
    SystemState target;
    if (supervisorV2Mailbox.consumeNextState(target)) {
        switch (target) {
            case SystemState::SLEEP:     setOFF(0); break;
            case SystemState::READY:     setIDLE(0); break;
            case SystemState::LIVE:      setSTREAMING(0); break;
            case SystemState::ERROR:
            case SystemState::FATAL:     setERROR(0); break;
            default: break;
        }
        s_supervisorV2.completeTransition(id(), TransitionStatus::Completed);
    }

    static char cmdBuf[SERIAL_CMD_BUF_SIZE];
    if (cli::readLine(cmdBuf, sizeof(cmdBuf))) {
        cli::process(cmdBuf);
    }
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
    (void)transitionId;
}
