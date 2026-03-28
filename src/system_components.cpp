#include "system_components.h"

#include <string.h>

#include "audio_runtime.h"
#include "board_info.h"
#include "cli.h"
#include "config.h"
#include "debug.h"
#include "wifi_manager.h"

const char* BoardInfoComponent::name() const {
    return "BoardInfo";
}

bool BoardInfoComponent::setup() {
    board_info::print();
    return true;
}

WiFiComponent::WiFiComponent(SystemController& system) : system_(system) {}

const char* WiFiComponent::name() const {
    return "WiFi";
}

bool WiFiComponent::setup() {
    wifi_manager::setConnectedCallback(&WiFiComponent::onConnected, this);
    wifi_manager::setDisconnectedCallback(&WiFiComponent::onDisconnected, this);
    wifi_manager::init();

    if (wifi_manager::state() == wifi_manager::WiFiState::CONNECTED) {
        system_.postEvent(SystemEvent::WIFI_READY, SystemReason::WIFI_INITIALIZED);
    }

    return true;
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
    : audio_(audio), system_(system) {}

const char* AudioRuntimeComponent::name() const {
    return "AudioRuntime";
}

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
    : audio_(audio), system_(system) {}

const char* CliComponent::name() const {
    return "CLI";
}

bool CliComponent::setup() {
    cli::init(audio_, audio_runtime::taskHandlePtr(), &system_);
    system_.subscribe([](SystemState state) {
        PROD_LOG("Observed state change: %s", toString(state));
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
        }

        // Always process the command for output and validation (ssid/pass/connect/volume/balance/help)
        cli::process(cmdBuf);
    }
}
