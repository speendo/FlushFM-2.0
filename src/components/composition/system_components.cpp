#include "components/composition/system_components.h"

#include <string.h>

#include "components/audio/audio_runtime.h"
#include "components/board/board_info.h"
#include "components/cli/cli.h"
#include "core/config.h"
#include "core/debug.h"
#include "settings.h"
#include "components/network/wifi_manager.h"

namespace {

constexpr const char* kBoardInfoName = "BoardInfo";
constexpr const char* kWiFiName = "WiFi";
constexpr const char* kAudioRuntimeName = "AudioRuntime";
constexpr const char* kCliName = "CLI";

}  // namespace

BoardInfoComponent::BoardInfoComponent() : ISystemComponent(kBoardInfoName) {}

bool BoardInfoComponent::setup() {
    board_info::print();
    return true;
}

WiFiComponent::WiFiComponent(SystemController& system)
    : ISystemComponent(kWiFiName), system_(system) {}

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

bool AudioRuntimeComponent::setup() {
    audio_runtime::setSignalHandler(&AudioRuntimeComponent::onAudioSignal, this);
    const bool started = audio_runtime::start(audio_);
    if (!started) {
        system_.postEvent(SystemEvent::AUDIO_INIT_FAILED, SystemReason::AUDIO_TASK_INIT_FAILED);
    }
    return started;
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

bool CliComponent::setup() {
    cli::init(audio_, audio_runtime::taskHandlePtr(), &system_);
    system_.subscribe([this](SystemState state) {
        PROD_LOG(kCliName, "Observed state change: %s", toString(state));
    });
    cli::printHelp();
    return true;
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
        } else if (strcmp(cmd, "switch") == 0) {
            system_.postEvent(SystemEvent::STOP_REQUESTED, SystemReason::USER_REQUEST, EventPolicy::BOUNDED_BLOCKING);
            system_.postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST, EventPolicy::BOUNDED_BLOCKING);
        } else if (strcmp(cmd, "reset") == 0) {
            system_.postEvent(SystemEvent::STOP_REQUESTED, SystemReason::USER_REQUEST, EventPolicy::BOUNDED_BLOCKING);
        }

        // Always process the command for output and validation.
        cli::process(cmdBuf);
    }
}
