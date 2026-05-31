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

// -- ISystemComponent base class --------------------------------------------

void ISystemComponent::loop() {
    SystemState target;
    if (mailbox_.consumeNextState(target)) {
        dispatch(target);
    }
    poll();
}

void ISystemComponent::dispatch(SystemState target) {
    switch (target) {
        case SystemState::BOOTING:    handleBOOTING(); break;
        case SystemState::SLEEP:      handleSLEEP(); break;
        case SystemState::CONNECTING: handleCONNECTING(); break;
        case SystemState::READY:      handleREADY(); break;
        case SystemState::LIVE:       handleLIVE(); break;
        case SystemState::ERROR:      handleERROR(); break;
        case SystemState::FATAL:      handleFATAL(); break;
    }
}

void ISystemComponent::registerWithSupervisor(SupervisorV2& supervisor) {
    supervisor.registerComponent(id_, &mailbox_, isRequired_);
}

void ISystemComponent::completeTransition(TransitionStatus status) {
    s_supervisorV2.completeTransition(id_, status);
}

namespace {

constexpr const char* kAudioRuntimeName = "AudioRuntime";
constexpr const char* kCliName = "CLI";

constexpr const char* kAudioRuntimeName = "AudioRuntime";
constexpr uint32_t kAudioTimeoutIdleMs = 2000;
constexpr uint32_t kAudioTimeoutStreamingMs = 5000;
constexpr uint32_t kAudioTimeoutErrorMs = 1000;

constexpr uint32_t kCliTimeoutOffMs = 0;
constexpr uint32_t kCliTimeoutIdleMs = 0;
constexpr uint32_t kCliTimeoutStreamingMs = 0;
constexpr uint32_t kCliTimeoutErrorMs = 0;

}  // namespace

BoardInfoComponent::BoardInfoComponent()
    : ISystemComponent(ComponentID::BoardInfo, "BoardInfo", false) {}

bool BoardInfoComponent::setup() {
    board_info::print();
    registerWithSupervisor(s_supervisorV2);
    return true;
}

void BoardInfoComponent::handleBOOTING()    { completeTransition(TransitionStatus::Completed); }
void BoardInfoComponent::handleSLEEP()      { completeTransition(TransitionStatus::Completed); }
void BoardInfoComponent::handleCONNECTING() { completeTransition(TransitionStatus::Completed); }
void BoardInfoComponent::handleREADY()      { completeTransition(TransitionStatus::Completed); }
void BoardInfoComponent::handleLIVE()       { completeTransition(TransitionStatus::Completed); }
void BoardInfoComponent::handleERROR()      { completeTransition(TransitionStatus::Completed); }
void BoardInfoComponent::handleFATAL()      { completeTransition(TransitionStatus::Completed); }

WiFiComponent::WiFiComponent()
    : ISystemComponent(ComponentID::WiFi, "WiFi", true) {}

bool WiFiComponent::setup() {
    registerWithSupervisor(s_supervisorV2);

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
        PROD_LOG("WiFi", "Boot auto-connect requested from persisted settings");
    }

    // Opportunistic head-start: if credentials exist, start connecting early.
    // handleCONNECTING() will set transitionPending_ and call ensureConnected()
    // again -- if already connected, it returns instantly.
    ensureConnected();

    return true;
}

void WiFiComponent::handleBOOTING() {
    completeTransition(TransitionStatus::Completed);
}

void WiFiComponent::handleSLEEP() {
    transitionPending_ = false;
    wifi_manager::disconnect();
    completeTransition(TransitionStatus::Completed);
}

void WiFiComponent::handleCONNECTING() {
    transitionPending_ = true;
    pendingStreamingTarget_ = true;
    ensureConnected();
}

void WiFiComponent::handleREADY() {
    completeTransition(TransitionStatus::Completed);
}

void WiFiComponent::handleLIVE() {
    transitionPending_ = true;
    pendingStreamingTarget_ = true;
    ensureConnected();
}

void WiFiComponent::handleERROR() {
    transitionPending_ = false;
    completeTransition(TransitionStatus::Completed);
}

void WiFiComponent::handleFATAL() {
    completeTransition(TransitionStatus::Completed);
}

void WiFiComponent::ensureConnected() {
    if (wifi_manager::isConnected()) return;
    wifi_manager::connect();
}

void WiFiComponent::poll() {
    if (!transitionPending_) return;
    if (wifi_manager::isConnected()) {
        transitionPending_ = false;
        completeTransition(TransitionStatus::Completed);
    }
}

void WiFiComponent::onTransitionTimeout(uint32_t transitionId) {
    if (!transitionPending_ || pendingTransitionId_ != transitionId) return;
    transitionPending_ = false;
    completeTransition(TransitionStatus::Failed);
}

void WiFiComponent::onConnected(void* context) {
    auto* self = static_cast<WiFiComponent*>(context);
    if (!self) return;
    if (self->transitionPending_ && self->pendingStreamingTarget_) {
        self->transitionPending_ = false;
        self->completeTransition(TransitionStatus::Completed);
    }
}

void WiFiComponent::onDisconnected(void* context) {
    auto* self = static_cast<WiFiComponent*>(context);
    if (!self) return;
    if (self->transitionPending_ && self->pendingStreamingTarget_) {
        self->transitionPending_ = false;
        self->completeTransition(TransitionStatus::Failed);
    } else {
        s_supervisorV2.postErrorEvent("wifi disconnected", ComponentID::WiFi);
    }
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
    : ISystemComponent(ComponentID::CLI, "CLI", false), audio_(audio) {}

bool CliComponent::setup() {
    registerWithSupervisor(s_supervisorV2);
    cli::init(audio_, audio_runtime::taskHandlePtr(), &s_supervisorV2);
    cli::printHelp();
    return true;
}

void CliComponent::handleBOOTING()    { completeTransition(TransitionStatus::Completed); }
void CliComponent::handleSLEEP()      { completeTransition(TransitionStatus::Completed); }
void CliComponent::handleCONNECTING() { completeTransition(TransitionStatus::Completed); }
void CliComponent::handleREADY()      { completeTransition(TransitionStatus::Completed); }
void CliComponent::handleLIVE()       { completeTransition(TransitionStatus::Completed); }
void CliComponent::handleERROR()      { completeTransition(TransitionStatus::Completed); }
void CliComponent::handleFATAL()      { completeTransition(TransitionStatus::Completed); }

void CliComponent::poll() {
    static char cmdBuf[SERIAL_CMD_BUF_SIZE];
    if (cli::readLine(cmdBuf, sizeof(cmdBuf))) {
        cli::process(cmdBuf);
    }
}
