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

}  // namespace

BoardInfoComponent::BoardInfoComponent() : ISystemComponent(kBoardInfoName) {}

void BoardInfoComponent::registerWithController(SystemController& controller) const {
    controller.registerComponent(name(), false);
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
    (void)transitionId;
    return kWiFiTimeoutOffMs;
}

uint32_t WiFiComponent::setIDLE(uint32_t transitionId) {
    (void)transitionId;
    return kWiFiTimeoutIdleMs;
}

uint32_t WiFiComponent::setSTREAMING(uint32_t transitionId) {
    (void)transitionId;
    return kWiFiTimeoutStreamingMs;
}

uint32_t WiFiComponent::setERROR(uint32_t transitionId) {
    (void)transitionId;
    return kWiFiTimeoutErrorMs;
}

void WiFiComponent::onTransitionTimeout(uint32_t transitionId) {
    DEBUG_LOG(kWiFiName, "Transition timeout for id=%lu", static_cast<unsigned long>(transitionId));
}

bool WiFiComponent::bootAutoConnectSucceeded() const {
    return bootAutoConnectSucceeded_;
}

void WiFiComponent::onConnected(void* context) {
    auto* self = static_cast<WiFiComponent*>(context);
    if (!self) {
        return;
    }
    self->system_.postEvent(SystemEvent::WIFI_READY, SystemReason::WIFI_INITIALIZED);
}

void WiFiComponent::onDisconnected(void* context) {
    auto* self = static_cast<WiFiComponent*>(context);
    if (!self) {
        return;
    }
    self->system_.postEvent(SystemEvent::WIFI_DISCONNECTED, SystemReason::NONE, EventPolicy::BOUNDED_BLOCKING);
}

AudioRuntimeComponent::AudioRuntimeComponent(IAudioPlayer& audio, SystemController& system)
    : ISystemComponent(kAudioRuntimeName), audio_(audio), system_(system) {}

void AudioRuntimeComponent::registerWithController(SystemController& controller) const {
    controller.registerComponent(name(), true);
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
    (void)transitionId;
    return kAudioTimeoutOffMs;
}

uint32_t AudioRuntimeComponent::setIDLE(uint32_t transitionId) {
    (void)transitionId;
    return kAudioTimeoutIdleMs;
}

uint32_t AudioRuntimeComponent::setSTREAMING(uint32_t transitionId) {
    (void)transitionId;
    return kAudioTimeoutStreamingMs;
}

uint32_t AudioRuntimeComponent::setERROR(uint32_t transitionId) {
    (void)transitionId;
    return kAudioTimeoutErrorMs;
}

void AudioRuntimeComponent::onTransitionTimeout(uint32_t transitionId) {
    DEBUG_LOG(kAudioRuntimeName, "Transition timeout for id=%lu", static_cast<unsigned long>(transitionId));
}

void AudioRuntimeComponent::onAudioSignal(audio_runtime::Signal signal, void* context) {
    auto* self = static_cast<AudioRuntimeComponent*>(context);
    if (!self) {
        return;
    }

    if (signal == audio_runtime::Signal::INIT_OK) {
        self->system_.postEvent(SystemEvent::AUDIO_INIT_OK, SystemReason::AUDIO_TASK_STARTED);
    } else if (signal == audio_runtime::Signal::STREAM_LOST) {
        self->system_.postEvent(SystemEvent::STREAM_LOST, SystemReason::NONE, EventPolicy::BOUNDED_BLOCKING);
    } else {
        self->system_.postEvent(SystemEvent::AUDIO_INIT_FAILED, SystemReason::AUDIO_TASK_INIT_FAILED);
    }
}

CliComponent::CliComponent(IAudioPlayer& audio, SystemController& system)
    : ISystemComponent(kCliName), audio_(audio), system_(system) {}

void CliComponent::registerWithController(SystemController& controller) const {
    controller.registerComponent(name(), false);
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
        // Extract command word before passing to cli::process
        char cmd[32] = {};
        const char* space = strchr(cmdBuf, ' ');
        if (space) {
            const size_t cmdLen = static_cast<size_t>(space - cmdBuf);
            memcpy(cmd, cmdBuf, cmdLen < sizeof(cmd) - 1 ? cmdLen : sizeof(cmd) - 1);
        } else {
            strncpy(cmd, cmdBuf, sizeof(cmd) - 1);
        }

        // Route state-relevant commands through SystemController
        if (strcmp(cmd, "play") == 0) {
            system_.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST, EventPolicy::BOUNDED_BLOCKING);
        } else if (strcmp(cmd, "stop") == 0) {
            system_.postEvent(SystemEvent::STOP_REQUESTED, SystemReason::USER_REQUEST, EventPolicy::BOUNDED_BLOCKING);
        } else if (strcmp(cmd, "reset") == 0) {
            system_.postEvent(SystemEvent::STOP_REQUESTED, SystemReason::USER_REQUEST, EventPolicy::BOUNDED_BLOCKING);
        }

        // Always process the command for output and validation.
        cli::process(cmdBuf);
    }
}
