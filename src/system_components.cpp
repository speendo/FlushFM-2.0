#include "system_components.h"

#include "audio_runtime.h"
#include "board_info.h"
#include "cli.h"
#include "config.h"
#include "wifi_manager.h"

const char* BoardInfoComponent::name() const {
    return "BoardInfo";
}

bool BoardInfoComponent::setup() {
    board_info::print();
    return true;
}

const char* WiFiComponent::name() const {
    return "WiFi";
}

bool WiFiComponent::setup() {
    wifi_manager::init();
    return true;
}

AudioRuntimeComponent::AudioRuntimeComponent(IAudioPlayer& audio) : audio_(audio) {}

const char* AudioRuntimeComponent::name() const {
    return "AudioRuntime";
}

bool AudioRuntimeComponent::setup() {
    return audio_runtime::start(audio_);
}

CliComponent::CliComponent(IAudioPlayer& audio) : audio_(audio) {}

const char* CliComponent::name() const {
    return "CLI";
}

bool CliComponent::setup() {
    cli::init(audio_, audio_runtime::taskHandlePtr());
    cli::printHelp();
    return true;
}

void CliComponent::loop() {
    static char cmdBuf[SERIAL_CMD_BUF_SIZE];
    if (cli::readLine(cmdBuf, sizeof(cmdBuf))) {
        cli::process(cmdBuf);
    }
}
