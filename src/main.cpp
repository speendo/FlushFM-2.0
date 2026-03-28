#include <Arduino.h>

#include "AudioPlayerESP32.h"
#include "IAudioPlayer.h"
#include "config.h"
#include "debug.h"
#include "system_controller.h"
#include "system_components.h"

// ---------------------------------------------------------------------------
// Audio – concrete instance wired here; rest of code depends on interface only
// ---------------------------------------------------------------------------
static AudioPlayerESP32 s_playerImpl(I2S_BCK_PIN, I2S_WS_PIN, I2S_DOUT_PIN);
static IAudioPlayer& s_audio = s_playerImpl;
static SystemController s_system;
static BoardInfoComponent s_boardInfo;
static WiFiComponent s_wifi(s_system);
static AudioRuntimeComponent s_audioRuntime(s_audio, s_system);
static CliComponent s_cli(s_audio, s_system);

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
    s_system.postEvent(SystemEvent::BOOT, SystemReason::BOOT_SEQUENCE);

    for (ISystemComponent* component : s_components) {
        if (!component->setup()) {
            ERROR_LOG("Component setup failed: %s", component->name());
            s_system.postEvent(SystemEvent::COMPONENT_SETUP_FAILED, SystemReason::COMPONENT_SETUP);
        }
        s_system.dispatchPending();
    }
}

void loop() {
    s_system.dispatchPending();
    for (ISystemComponent* component : s_components) {
        component->loop();
    }
}

