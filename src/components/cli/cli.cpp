// cli.cpp – Serial command dispatch
#include "components/cli/cli.h"

#include <Arduino.h>
#include <string.h>

#include "cli_command_logic.h"
#include "components/cli/cli_output.h"
#include "core/config.h"
#include "settings.h"
#include "state_machine/system_controller.h"
#include "components/network/wifi_manager.h"

// ---------------------------------------------------------------------------
// Debug command integration – single guard at file level; no scattered #ifdefs
// ---------------------------------------------------------------------------
#include "components/cli/debug_cli.h"
#ifdef DEBUG_ENABLED
#  define TRY_DEBUG_COMMAND(cmd, arg)  debug_cli::process(cmd, arg)
#  define PRINT_DEBUG_HELP()           debug_cli::printHelp()
#else
#  define TRY_DEBUG_COMMAND(cmd, arg)  false
#  define PRINT_DEBUG_HELP()           do {} while (0)
#endif

// ---------------------------------------------------------------------------
// Module-private state
// ---------------------------------------------------------------------------
static IAudioPlayer* s_audio = nullptr;
static SystemController* s_controller = nullptr;

namespace {

class CliEnvironment final : public cli_command_logic::IEnvironment {
public:
    void setSsid(const char* ssid) override {
        wifi_manager::setSsid(ssid);
        settings::saveSsid(ssid);
    }

    void setPass(const char* pass) override {
        wifi_manager::setPass(pass);
        settings::savePass(pass);
    }

    void connectWiFi() override {
        wifi_manager::connect();
    }

    void saveStation(const char* stationUrl) override {
        settings::saveStation(stationUrl);
    }

    const char* loadStation() override {
        static char stationBuf[settings::kStationMaxLen];
        if (settings::loadStation(stationBuf, sizeof(stationBuf))) {
            return stationBuf;
        }
        return "";
    }

    void forgetSettings() override {
        settings::clearAll();
    }

    void resetSession() override {
        wifi_manager::resetSession();
    }

    cli_command_logic::WiFiConnectivity wifiConnectivity() const override {
        return wifi_manager::state() == wifi_manager::WiFiState::CONNECTED
            ? cli_command_logic::WiFiConnectivity::CONNECTED
            : cli_command_logic::WiFiConnectivity::DISCONNECTED;
    }

    cli_command_logic::AudioState audioState() const override {
        switch (s_audio->runtimeState()) {
            case IAudioPlayer::RuntimeState::IDLE:
                return cli_command_logic::AudioState::IDLE;
            case IAudioPlayer::RuntimeState::CONNECTING:
                return cli_command_logic::AudioState::CONNECTING;
            case IAudioPlayer::RuntimeState::STREAMING:
                return cli_command_logic::AudioState::STREAMING;
            case IAudioPlayer::RuntimeState::ERROR:
                return cli_command_logic::AudioState::ERROR;
        }
        return cli_command_logic::AudioState::IDLE;
    }

    const char* getPersistedStation() const override {
        static char stationBuf[settings::kStationMaxLen];
        if (settings::loadStation(stationBuf, sizeof(stationBuf))) {
            return stationBuf;
        }
        return "";
    }
};

CliEnvironment s_env;

} // namespace

static void printDebugHelp() {
    PRINT_DEBUG_HELP();
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------
namespace cli {

void init(IAudioPlayer& audio, TaskHandle_t* audioTaskHandle, SystemController* controller) {
    s_audio = &audio;
    s_controller = controller;
#ifdef DEBUG_ENABLED
    debug_cli::init(audioTaskHandle, controller);
#else
    (void)audioTaskHandle;
#endif
}

bool readLine(char* buf, size_t maxLen) {
    static size_t pos = 0;
    while (Serial.available()) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\n' || c == '\r') {
            if (pos > 0) {
                buf[pos] = '\0';
                pos = 0;
                return true;
            }
        } else if (c >= 0x20 && c <= 0x7E && pos < maxLen - 1) {
            // Accept printable ASCII only – discard control/escape sequences
            buf[pos++] = c;
        }
    }
    return false;
}

void process(const char* line) {
    if (!s_audio) return;

    // Split into command word and optional argument
    char cmd[32] = {};
    const char* arg = nullptr;
    const char* space = strchr(line, ' ');
    if (space) {
        const size_t cmdLen = static_cast<size_t>(space - line);
        memcpy(cmd, line, cmdLen < sizeof(cmd) - 1 ? cmdLen : sizeof(cmd) - 1);
        arg = space + 1;
    } else {
        strncpy(cmd, line, sizeof(cmd) - 1);
    }

    cli_output::CommandResult result = cli_command_logic::dispatchCommand(
        cmd,
        arg,
        *s_audio,
        s_env,
        AUDIO_VOLUME_STEPS);

    if (s_controller) {
        if (strcmp(cmd, "play") == 0 && result.key == cli_output::MessageKey::CONNECTING_STREAM) {
            (void)s_controller->postEvent(SystemEvent::PLAY_REQUESTED,
                                          SystemReason::USER_REQUEST,
                                          EventPolicy::BOUNDED_BLOCKING);
        } else if (strcmp(cmd, "stop") == 0 && result.key == cli_output::MessageKey::STREAM_STOPPED) {
            (void)s_controller->postEvent(SystemEvent::STOP_REQUESTED,
                                          SystemReason::USER_REQUEST,
                                          EventPolicy::BOUNDED_BLOCKING);
        } else if (strcmp(cmd, "reset") == 0 && result.key == cli_output::MessageKey::SESSION_RESET) {
            (void)s_controller->postEvent(SystemEvent::STOP_REQUESTED,
                                          SystemReason::USER_REQUEST,
                                          EventPolicy::BOUNDED_BLOCKING);
        }
    }

    if (result.key == cli_output::MessageKey::NONE) {
        if (!TRY_DEBUG_COMMAND(cmd, arg)) {
            result = {cli_output::MessageKey::UNKNOWN_COMMAND, cmd};
        }
    }

    cli_output::render(result, &printDebugHelp);
}

void printHelp() {
    cli_output::render({cli_output::MessageKey::HELP}, &printDebugHelp);
}

} // namespace cli
