// cli.cpp – Serial command dispatch
#include "cli.h"

#include <Arduino.h>
#include <string.h>

#include "config.h"
#include "debug.h"
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

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------
namespace cli {

void init(IAudioPlayer& audio, TaskHandle_t* audioTaskHandle) {
    s_audio = &audio;
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

    if (strcmp(cmd, "ssid") == 0) {
        if (!arg || *arg == '\0') { ERROR_LOG("Usage: ssid <name>"); return; }
        wifi_manager::setSsid(arg);
        PROD_LOG("SSID set to: %s", arg);

    } else if (strcmp(cmd, "pass") == 0) {
        if (!arg || *arg == '\0') { ERROR_LOG("Usage: pass <password>"); return; }
        wifi_manager::setPass(arg);
        PROD_LOG("Password set");

    } else if (strcmp(cmd, "connect") == 0) {
        wifi_manager::connect();

    } else if (strcmp(cmd, "play") == 0) {
        if (!arg || *arg == '\0') { ERROR_LOG("Usage: play <url>"); return; }
        if (!wifi_manager::isConnected()) { ERROR_LOG("Not connected to WiFi – run 'connect' first"); return; }
        PROD_LOG("Connecting to stream: %s", arg);
        s_audio->connectToHost(arg);

    } else if (strcmp(cmd, "stop") == 0) {
        s_audio->stop();
        PROD_LOG("Stream stopped");

    } else if (strcmp(cmd, "switch") == 0) {
        if (!arg || *arg == '\0') { ERROR_LOG("Usage: switch <url>"); return; }
        if (!wifi_manager::isConnected()) { ERROR_LOG("Not connected to WiFi – run 'connect' first"); return; }
        s_audio->stop();
        PROD_LOG("Switching to stream: %s", arg);
        s_audio->connectToHost(arg);

    } else if (strcmp(cmd, "volume") == 0) {
        if (!arg || *arg == '\0') {
            PROD_LOG("Current volume: %d (range 0-%d)", s_audio->getVolume(), AUDIO_VOLUME_STEPS);
            return;
        }
        const int vol = atoi(arg);
        if (vol < 0 || vol > AUDIO_VOLUME_STEPS) { ERROR_LOG("Volume must be 0-%d", AUDIO_VOLUME_STEPS); return; }
        s_audio->setVolume(static_cast<uint8_t>(vol));
        PROD_LOG("Volume set to %d", vol);

    } else if (strcmp(cmd, "balance") == 0) {
        if (!arg || *arg == '\0') { ERROR_LOG("Usage: balance <-16..16>  (-16=left, 0=center, +16=right)"); return; }
        const int bal = atoi(arg);
        if (bal < -16 || bal > 16) { ERROR_LOG("Balance must be -16..16"); return; }
        s_audio->setBalance(static_cast<int8_t>(bal));
        PROD_LOG("Balance set to %d", bal);

    } else if (strcmp(cmd, "help") == 0) {
        printHelp();

    } else if (!TRY_DEBUG_COMMAND(cmd, arg)) {
        ERROR_LOG("Unknown command '%s' – type 'help' for available commands", cmd);
    }
}

void printHelp() {
    Serial.println();
    Serial.println("FlushFM – Serial Commands:");
    Serial.println("  ssid <name>       Set WiFi SSID");
    Serial.println("  pass <password>   Set WiFi password");
    Serial.println("  connect           Connect to WiFi with stored credentials");
    Serial.println("  play <url>        Start streaming from URL");
    Serial.println("  stop              Stop current stream");
    Serial.println("  switch <url>      Switch to a different stream URL");
    Serial.printf ("  volume [0-%d]     Get or set playback volume\r\n", AUDIO_VOLUME_STEPS);
    Serial.println("  balance <-16..16> Stereo balance (-16=L, 0=center, +16=R)");
    PRINT_DEBUG_HELP();
    Serial.println("  help              Show this help");
    Serial.println();
    Serial.println("Test stations:");
    Serial.println("  http://ice1.somafm.com/groovesalad-256-mp3");
    Serial.println("  http://stream.srg-ssr.ch/m/drs3/mp3_128");
    Serial.println();
}

} // namespace cli
