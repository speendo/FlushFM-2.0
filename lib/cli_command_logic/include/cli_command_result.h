#pragma once

namespace cli_output {

enum class MessageKey {
    NONE,
    USAGE_SSID,
    USAGE_PASS,
    USAGE_PLAY,
    USAGE_MUTE,
    USAGE_BALANCE,
    WIFI_REQUIRED,
    SSID_SET,
    PASSWORD_SET,
    CONNECTING_STREAM,
    STREAM_STOPPED,
    VOLUME_CURRENT,
    VOLUME_OUT_OF_RANGE,
    VOLUME_SET,
    MUTE_CURRENT,
    MUTE_SET,
    BALANCE_OUT_OF_RANGE,
    BALANCE_SET,
    SETTINGS_FORGOTTEN,
    SESSION_RESET,
    STATUS,
    HELP,
    UNKNOWN_COMMAND,
};

struct CommandResult {
    MessageKey key = MessageKey::NONE;
    const char* text = nullptr;
    int value = 0;
    int aux = 0;
};

} // namespace cli_output
