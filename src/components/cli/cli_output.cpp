#include "components/cli/cli_output.h"

#include <Arduino.h>

#include "core/config.h"
#include "core/debug.h"

namespace cli_output {

namespace {

constexpr const char* kLogSource = "CliOutput";

void printHelp(DebugHelpPrinter debugHelpPrinter) {
    Serial.println();
    Serial.println("FlushFM - Serial Commands:");
    Serial.println("  ssid <name>       Set WiFi SSID");
    Serial.println("  pass <password>   Set WiFi password");
    Serial.println("  connect           Connect to WiFi with stored credentials");
    Serial.println("  play [url]        Start streaming (URL optional: uses last saved if omitted)");
    Serial.println("  stop              Stop current stream");
    Serial.println("  switch <url>      Switch to a different stream URL");
    Serial.println("  forget            Clear persisted ssid/pass/station from NVS");
    Serial.println("  reset             Stop stream and reset runtime WiFi session");
    Serial.printf ("  volume [0-%d]     Get or set playback volume\r\n", AUDIO_VOLUME_STEPS);
    Serial.println("  balance <-16..16> Stereo balance (-16=L, 0=center, +16=R)");
    Serial.println("  status            Show WiFi, audio, and persisted settings");
    if (debugHelpPrinter) {
        debugHelpPrinter();
    }
    Serial.println("  help              Show this help");
    Serial.println();
    Serial.println("Test stations:");
    Serial.println("  http://ice1.somafm.com/groovesalad-256-mp3");
    Serial.println("  http://stream.srg-ssr.ch/m/drs3/mp3_128");
    Serial.println();
}

} // namespace

void render(const CommandResult& result, DebugHelpPrinter debugHelpPrinter) {
    switch (result.key) {
        case MessageKey::NONE:
            return;
        case MessageKey::USAGE_SSID:
            ERROR_LOG(kLogSource, "Usage: ssid <name>");
            return;
        case MessageKey::USAGE_PASS:
            ERROR_LOG(kLogSource, "Usage: pass <password>");
            return;
        case MessageKey::USAGE_PLAY:
            ERROR_LOG(kLogSource, "Usage: play <url>");
            return;
        case MessageKey::USAGE_SWITCH:
            ERROR_LOG(kLogSource, "Usage: switch <url>");
            return;
        case MessageKey::USAGE_BALANCE:
            ERROR_LOG(kLogSource, "Usage: balance <-16..16>  (-16=left, 0=center, +16=right)");
            return;
        case MessageKey::WIFI_REQUIRED:
            ERROR_LOG(kLogSource, "Not connected to WiFi - run 'connect' first");
            return;
        case MessageKey::SSID_SET:
            PROD_LOG(kLogSource, "SSID set to: %s", result.text ? result.text : "");
            return;
        case MessageKey::PASSWORD_SET:
            PROD_LOG(kLogSource, "Password set");
            return;
        case MessageKey::CONNECTING_STREAM:
            PROD_LOG(kLogSource, "Connecting to stream: %s", result.text ? result.text : "");
            return;
        case MessageKey::STREAM_STOPPED:
            PROD_LOG(kLogSource, "Stream stopped");
            return;
        case MessageKey::SWITCHING_STREAM:
            PROD_LOG(kLogSource, "Switching to stream: %s", result.text ? result.text : "");
            return;
        case MessageKey::VOLUME_CURRENT:
            PROD_LOG(kLogSource, "Current volume: %d (range 0-%d)", result.value, AUDIO_VOLUME_STEPS);
            return;
        case MessageKey::VOLUME_OUT_OF_RANGE:
            ERROR_LOG(kLogSource, "Volume must be 0-%d", AUDIO_VOLUME_STEPS);
            return;
        case MessageKey::VOLUME_SET:
            PROD_LOG(kLogSource, "Volume set to %d", result.value);
            return;
        case MessageKey::BALANCE_OUT_OF_RANGE:
            ERROR_LOG(kLogSource, "Balance must be -16..16");
            return;
        case MessageKey::BALANCE_SET:
            PROD_LOG(kLogSource, "Balance set to %d", result.value);
            return;
        case MessageKey::SETTINGS_FORGOTTEN:
            PROD_LOG(kLogSource, "Persisted settings cleared from NVS");
            return;
        case MessageKey::SESSION_RESET:
            PROD_LOG(kLogSource, "Runtime session reset (stream stopped, WiFi disconnected)");
            return;
        case MessageKey::STATUS:
            // Status will be rendered via aux field (packed audio + wifi state) and text (station)
            // Format: [WiFi: CONNECTED|DISCONNECTED] [Audio: IDLE|CONNECTING|STREAMING|ERROR] [Persisted: URL or empty]
            Serial.println();
            Serial.printf("WiFi:      %s\r\n", result.aux & 0x01 ? "CONNECTED" : "DISCONNECTED");
            Serial.printf("Audio:     ");
            {
                uint8_t audioState = (result.aux >> 1) & 0x03;
                switch (audioState) {
                    case 0: Serial.println("IDLE"); break;
                    case 1: Serial.println("CONNECTING"); break;
                    case 2: Serial.println("STREAMING"); break;
                    case 3: Serial.println("ERROR"); break;
                    default: Serial.println("UNKNOWN"); break;
                }
            }
            Serial.printf("Persisted: %s\r\n", result.text && *result.text ? result.text : "(none)");
            Serial.println();
            return;
        case MessageKey::HELP:
            printHelp(debugHelpPrinter);
            return;
        case MessageKey::UNKNOWN_COMMAND:
            ERROR_LOG(kLogSource, "Unknown command '%s' - type 'help' for available commands", result.text ? result.text : "");
            return;
    }
}

} // namespace cli_output
