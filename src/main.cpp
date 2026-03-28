#include <Arduino.h>

#include "AudioPlayerESP32.h"
#include "IAudioPlayer.h"
#include "config.h"
#include "debug.h"
#include "system_components.h"

// ---------------------------------------------------------------------------
// Audio – concrete instance wired here; rest of code depends on interface only
// ---------------------------------------------------------------------------
static AudioPlayerESP32 s_playerImpl(I2S_BCK_PIN, I2S_WS_PIN, I2S_DOUT_PIN);
static IAudioPlayer& s_audio = s_playerImpl;
static BoardInfoComponent s_boardInfo;
static WiFiComponent s_wifi;
static AudioRuntimeComponent s_audioRuntime(s_audio);
static CliComponent s_cli(s_audio);

static ISystemComponent* s_components[] = {
    &s_boardInfo,
    &s_wifi,
    &s_audioRuntime,
    &s_cli,
};

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);

    // Wait for Serial to be ready (USB CDC on ESP32-S3 needs a moment)
    const uint32_t start = millis();
    while (!Serial && (millis() - start) < SERIAL_TIMEOUT_MS) {
        delay(10);
    }

    PROD_LOG("Hello FlushFM");

    for (ISystemComponent* component : s_components) {
        if (!component->setup()) {
            ERROR_LOG("Component setup failed: %s", component->name());
        }
    }
}

void loop() {
    for (ISystemComponent* component : s_components) {
        component->loop();
    }
}

