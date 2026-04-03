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
        const char* url = arg;
        if (!url || *url == '\0') {
            url = env.loadStation();
            if (!url || *url == '\0') {
                return {MessageKey::USAGE_PLAY};
            }
        }
        if (env.wifiConnectivity() != WiFiConnectivity::CONNECTED) {
            return {MessageKey::WIFI_REQUIRED};
        }
        env.saveStation(url);
        audio.connectToHost(url);
        return {MessageKey::CONNECTING_STREAM, url};
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
        env.saveStation(arg);
        audio.stop();
        audio.connectToHost(arg);
        return {MessageKey::SWITCHING_STREAM, arg};
    }

    if (strcmp(cmd, "forget") == 0) {
        env.forgetSettings();
        return {MessageKey::SETTINGS_FORGOTTEN};
    }

    if (strcmp(cmd, "reset") == 0) {
        audio.stop();
        env.resetSession();
        return {MessageKey::SESSION_RESET};
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

    if (strcmp(cmd, "mute") == 0) {
        if (!arg || *arg == '\0') {
            return {MessageKey::MUTE_CURRENT, nullptr, audio.getMute() ? 1 : 0};
        }

        if (strcmp(arg, "on") == 0) {
            audio.setMute(true);
            return {MessageKey::MUTE_SET, "on"};
        }

        if (strcmp(arg, "off") == 0) {
            audio.setMute(false);
            return {MessageKey::MUTE_SET, "off"};
        }

        return {MessageKey::USAGE_MUTE};
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

    if (strcmp(cmd, "status") == 0) {
        uint8_t statusBits = 0;
        if (env.wifiConnectivity() == WiFiConnectivity::CONNECTED) {
            statusBits |= 0x01;
        }
        const AudioState aState = env.audioState();
        statusBits |= (static_cast<uint8_t>(aState) & 0x03) << 1;

        cli_output::CommandResult result;
        result.key = MessageKey::STATUS;
        result.text = env.getPersistedStation();
        result.aux = statusBits;
        return result;
    }

    return {MessageKey::NONE};
}

} // namespace cli_command_logic
