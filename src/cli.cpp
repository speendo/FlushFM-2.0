// cli.cpp – Serial command dispatch
#include "cli.h"

#include <Arduino.h>
#include <string.h>

#include "cli_command_logic.h"
#include "cli_output.h"
#include "config.h"
#include "system_controller.h"
#include "wifi_manager.h"

// ---------------------------------------------------------------------------
// Debug command integration – single guard at file level; no scattered #ifdefs
// ---------------------------------------------------------------------------
#include "debug_cli.h"
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
    }

    void setPass(const char* pass) override {
        wifi_manager::setPass(pass);
    }

    void connectWiFi() override {
        wifi_manager::connect();
    }

    cli_command_logic::WiFiConnectivity wifiConnectivity() const override {
        return wifi_manager::state() == wifi_manager::WiFiState::CONNECTED
            ? cli_command_logic::WiFiConnectivity::CONNECTED
            : cli_command_logic::WiFiConnectivity::DISCONNECTED;
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
    debug_cli::init(audioTaskHandle);
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
