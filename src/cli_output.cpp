#include "cli_output.h"

#include <Arduino.h>

#include "config.h"
#include "debug.h"

namespace cli_output {

namespace {

void printHelp(DebugHelpPrinter debugHelpPrinter) {
    Serial.println();
    Serial.println("FlushFM - Serial Commands:");
    Serial.println("  ssid <name>       Set WiFi SSID");
    Serial.println("  pass <password>   Set WiFi password");
    Serial.println("  connect           Connect to WiFi with stored credentials");
    Serial.println("  play <url>        Start streaming from URL");
    Serial.println("  stop              Stop current stream");
    Serial.println("  switch <url>      Switch to a different stream URL");
    Serial.printf ("  volume [0-%d]     Get or set playback volume\r\n", AUDIO_VOLUME_STEPS);
    Serial.println("  balance <-16..16> Stereo balance (-16=L, 0=center, +16=R)");
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
            ERROR_LOG("Usage: ssid <name>");
            return;
        case MessageKey::USAGE_PASS:
            ERROR_LOG("Usage: pass <password>");
            return;
        case MessageKey::USAGE_PLAY:
            ERROR_LOG("Usage: play <url>");
            return;
        case MessageKey::USAGE_SWITCH:
            ERROR_LOG("Usage: switch <url>");
            return;
        case MessageKey::USAGE_BALANCE:
            ERROR_LOG("Usage: balance <-16..16>  (-16=left, 0=center, +16=right)");
            return;
        case MessageKey::WIFI_REQUIRED:
            ERROR_LOG("Not connected to WiFi - run 'connect' first");
            return;
        case MessageKey::SSID_SET:
            PROD_LOG("SSID set to: %s", result.text ? result.text : "");
            return;
        case MessageKey::PASSWORD_SET:
            PROD_LOG("Password set");
            return;
        case MessageKey::CONNECTING_STREAM:
            PROD_LOG("Connecting to stream: %s", result.text ? result.text : "");
            return;
        case MessageKey::STREAM_STOPPED:
            PROD_LOG("Stream stopped");
            return;
        case MessageKey::SWITCHING_STREAM:
            PROD_LOG("Switching to stream: %s", result.text ? result.text : "");
            return;
        case MessageKey::VOLUME_CURRENT:
            PROD_LOG("Current volume: %d (range 0-%d)", result.value, AUDIO_VOLUME_STEPS);
            return;
        case MessageKey::VOLUME_OUT_OF_RANGE:
            ERROR_LOG("Volume must be 0-%d", AUDIO_VOLUME_STEPS);
            return;
        case MessageKey::VOLUME_SET:
            PROD_LOG("Volume set to %d", result.value);
            return;
        case MessageKey::BALANCE_OUT_OF_RANGE:
            ERROR_LOG("Balance must be -16..16");
            return;
        case MessageKey::BALANCE_SET:
            PROD_LOG("Balance set to %d", result.value);
            return;
        case MessageKey::HELP:
            printHelp(debugHelpPrinter);
            return;
        case MessageKey::UNKNOWN_COMMAND:
            ERROR_LOG("Unknown command '%s' - type 'help' for available commands", result.text ? result.text : "");
            return;
    }
}

} // namespace cli_output
