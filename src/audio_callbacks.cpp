#include <Arduino.h>

#include "debug.h"

// ESP32-audioI2S requires these free callback functions.
void audio_info(const char* info) {
    PROD_LOG("[Audio] %s", info);
}

void audio_showstation(const char* info) {
    PROD_LOG("[Station] %s", info);
}

void audio_showstreamtitle(const char* info) {
    PROD_LOG("[Track]   %s", info);
}

void audio_bitrate(const char* info) {
    PROD_LOG("[Bitrate] %s", info);
}
