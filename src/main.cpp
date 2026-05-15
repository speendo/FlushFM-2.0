#include <Arduino.h>

#include "AudioPlayerESP32.h"
#include "IAudioPlayer.h"
#include "components/audio/audio_callbacks.h"
#include "core/config.h"
#include "core/debug.h"
#include "settings.h"
#include "supervisor/supervisor.h"
#include "supervisor/supervisor_v2.h"
#include "components/composition/system_components.h"

namespace {

constexpr const char* kLogSource = "Main";

}  // namespace

// ---------------------------------------------------------------------------
// Audio – concrete instance wired here; rest of code depends on interface only
// ---------------------------------------------------------------------------
static AudioPlayerESP32 s_playerImpl(I2S_BCK_PIN, I2S_WS_PIN, I2S_DOUT_PIN);
static IAudioPlayer& s_audio = s_playerImpl;
static Supervisor s_system;
static BoardInfoComponent s_boardInfo;
static WiFiComponent s_wifi(s_system);
static AudioRuntimeComponent s_audioRuntime(s_audio, s_system);
static CliComponent s_cli(s_audio, s_system);
static SupervisorV2 s_supervisorV2;

static ISystemComponent* s_components[] = {
    &s_boardInfo,
    &s_wifi,
    &s_audioRuntime,
    &s_cli,
};

// ---------------------------------------------------------------------------
// SupervisorV2 state machine task — pinned to Core 0
// ---------------------------------------------------------------------------

static void stateMachineTask(void* param) {
    auto* supervisorV2 = static_cast<SupervisorV2*>(param);
    supervisorV2->setup();
    for (;;) {
        supervisorV2->run();
    }
}

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

    PROD_LOG(kLogSource, "Hello FlushFM");
    registerAudioLibraryCallbacks();

    for (ISystemComponent* component : s_components) {
        component->registerWithController(s_system);
    }

    (void)s_system.setup();

    xTaskCreatePinnedToCore(
        stateMachineTask,
        "StateMachine",
        8192,
        &s_supervisorV2,
        2,
        nullptr,
        0
    );
}

void loop() {
    s_system.processMailbox();

    for (ISystemComponent* component : s_components) {
        component->loop();
    }
}

