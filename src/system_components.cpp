#include "system_components.h"

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
    wifi_manager::init();

    if (wifi_manager::isConnected()) {
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
    cli::init(audio_, audio_runtime::taskHandlePtr());
    system_.subscribe([](SystemState state) {
        PROD_LOG("Observed state change: %s", toString(state));
    });
    cli::printHelp();
    return true;
}

void CliComponent::loop() {
    static char cmdBuf[SERIAL_CMD_BUF_SIZE];
    if (cli::readLine(cmdBuf, sizeof(cmdBuf))) {
        cli::process(cmdBuf);
    }
}
