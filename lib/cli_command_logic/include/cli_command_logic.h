#pragma once

#include <stdint.h>

#include "IAudioPlayer.h"
#include "cli_command_result.h"

namespace cli_command_logic {

enum class WiFiConnectivity {
    DISCONNECTED,
    CONNECTED,
};

enum class AudioState {
    IDLE,
    CONNECTING,
    STREAMING,
    ERROR,
};

class IEnvironment {
public:
    virtual ~IEnvironment() = default;

    virtual void setSsid(const char* ssid) = 0;
    virtual void setPass(const char* pass) = 0;
    virtual void connectWiFi() = 0;
    virtual void saveStation(const char* stationUrl) = 0;
    virtual const char* loadStation() = 0;
    virtual void forgetSettings() = 0;
    virtual void resetSession() = 0;
    virtual WiFiConnectivity wifiConnectivity() const = 0;
    virtual AudioState audioState() const = 0;
    virtual const char* getPersistedStation() const = 0;
};

cli_output::CommandResult dispatchCommand(
    const char* cmd,
    const char* arg,
    IAudioPlayer& audio,
    IEnvironment& env,
    uint8_t maxVolumeSteps);

} // namespace cli_command_logic
