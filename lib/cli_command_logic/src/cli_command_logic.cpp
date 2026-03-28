#include "cli_command_logic.h"

#include <stdlib.h>
#include <string.h>

namespace cli_command_logic {

cli_output::CommandResult dispatchCommand(
    const char* cmd,
    const char* arg,
    IAudioPlayer& audio,
    IEnvironment& env,
    const uint8_t maxVolumeSteps) {
    using namespace cli_output;

    if (strcmp(cmd, "ssid") == 0) {
        if (!arg || *arg == '\0') return {MessageKey::USAGE_SSID};
        env.setSsid(arg);
        return {MessageKey::SSID_SET, arg};
    }

    if (strcmp(cmd, "pass") == 0) {
        if (!arg || *arg == '\0') return {MessageKey::USAGE_PASS};
        env.setPass(arg);
        return {MessageKey::PASSWORD_SET};
    }

    if (strcmp(cmd, "connect") == 0) {
        env.connectWiFi();
        return {MessageKey::NONE};
    }

    if (strcmp(cmd, "play") == 0) {
        if (!arg || *arg == '\0') return {MessageKey::USAGE_PLAY};
        if (env.wifiConnectivity() != WiFiConnectivity::CONNECTED) {
            return {MessageKey::WIFI_REQUIRED};
        }
        audio.connectToHost(arg);
        return {MessageKey::CONNECTING_STREAM, arg};
    }

    if (strcmp(cmd, "stop") == 0) {
        audio.stop();
        return {MessageKey::STREAM_STOPPED};
    }

    if (strcmp(cmd, "switch") == 0) {
        if (!arg || *arg == '\0') return {MessageKey::USAGE_SWITCH};
        if (env.wifiConnectivity() != WiFiConnectivity::CONNECTED) {
            return {MessageKey::WIFI_REQUIRED};
        }
        audio.stop();
        audio.connectToHost(arg);
        return {MessageKey::SWITCHING_STREAM, arg};
    }

    if (strcmp(cmd, "volume") == 0) {
        if (!arg || *arg == '\0') {
            return {MessageKey::VOLUME_CURRENT, nullptr, audio.getVolume()};
        }
        const int vol = atoi(arg);
        if (vol < 0 || vol > maxVolumeSteps) return {MessageKey::VOLUME_OUT_OF_RANGE};
        audio.setVolume(static_cast<uint8_t>(vol));
        return {MessageKey::VOLUME_SET, nullptr, vol};
    }

    if (strcmp(cmd, "balance") == 0) {
        if (!arg || *arg == '\0') return {MessageKey::USAGE_BALANCE};
        const int bal = atoi(arg);
        if (bal < -16 || bal > 16) return {MessageKey::BALANCE_OUT_OF_RANGE};
        audio.setBalance(static_cast<int8_t>(bal));
        return {MessageKey::BALANCE_SET, nullptr, bal};
    }

    if (strcmp(cmd, "help") == 0) {
        return {MessageKey::HELP};
    }

    return {MessageKey::NONE};
}

} // namespace cli_command_logic
