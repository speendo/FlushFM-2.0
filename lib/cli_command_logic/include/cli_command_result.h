#pragma once

namespace cli_output {

enum class MessageKey {
    NONE,
    USAGE_SSID,
    USAGE_PASS,
    USAGE_PLAY,
    USAGE_SWITCH,
    USAGE_BALANCE,
    WIFI_REQUIRED,
    SSID_SET,
    PASSWORD_SET,
    CONNECTING_STREAM,
    STREAM_STOPPED,
    SWITCHING_STREAM,
    VOLUME_CURRENT,
    VOLUME_OUT_OF_RANGE,
    VOLUME_SET,
    BALANCE_OUT_OF_RANGE,
    BALANCE_SET,
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
