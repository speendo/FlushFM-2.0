#ifndef UNIT_TEST

#include <Arduino.h>

#include "AudioPlayerESP32.h"
#include "IAudioPlayer.h"
#include "LightSensor.h"
#include "components/audio/audio_callbacks.h"
#include "core/config.h"
#include "core/debug.h"
#include "settings.h"
#include "supervisor/supervisor.h"
#include "components/composition/system_components.h"

namespace {

constexpr const char* kLogSource = "Main";

}  // namespace

static IAudioPlayer* s_audio = nullptr;
static LightSensor s_lightSensor(LIGHT_SENSOR_PIN);

Supervisor s_supervisor;
static BoardInfoComponent s_boardInfo;
static WiFiComponent s_wifi;
static AudioRuntimeComponent s_audioRuntime(&s_audio);
static CliComponent s_cli(&s_audio);
static LightSensorComponent s_lightSensorComponent(s_lightSensor);

static ISystemComponent* s_components[] = {
    &s_boardInfo,
    &s_wifi,
    &s_audioRuntime,
    &s_cli,
    &s_lightSensorComponent,
};

static void stateMachineTask(void* param) {
    auto* supervisorV2 = static_cast<Supervisor*>(param);
    supervisorV2->setup();
    for (;;) {
        supervisorV2->run();
    }
}

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    delay(SERIAL_USB_ENUMERATION_MS);

    PROD_LOG(kLogSource, "Hello FlushFM");
    registerAudioLibraryCallbacks();

    s_audio = new AudioPlayerESP32(I2S_BCK_PIN, I2S_WS_PIN, I2S_DOUT_PIN);

    for (ISystemComponent* component : s_components) {
        component->setup();
    }

    xTaskCreatePinnedToCore(
        stateMachineTask,
        "StateMachine",
        8192,
        &s_supervisor,
        2,
        nullptr,
        0
    );
}

void loop() {
    for (ISystemComponent* component : s_components) {
        component->loop();
    }
}

#endif  // UNIT_TEST
