#include "audio_runtime.h"

#include <Arduino.h>

#include "config.h"
#include "debug.h"

namespace {

static IAudioPlayer* s_audio = nullptr;
static TaskHandle_t s_audioTaskHandle = nullptr;
static audio_runtime::SignalHandler s_signalHandler = nullptr;
static void* s_signalContext = nullptr;

static void audioTask(void* /*param*/) {
    if (!s_audio || !s_audio->begin()) {
        ERROR_LOG("Audio init failed inside task - check I2S wiring (BCK=%d WS=%d DOUT=%d)",
                  I2S_BCK_PIN, I2S_WS_PIN, I2S_DOUT_PIN);
        if (s_signalHandler) {
            s_signalHandler(audio_runtime::Signal::INIT_FAILED, s_signalContext);
        }
    } else {
        s_audio->setVolumeSteps(AUDIO_VOLUME_STEPS);
        s_audio->setVolume(AUDIO_VOLUME_DEFAULT);
        PROD_LOG("Audio initialized in task (BCK=%d WS=%d DOUT=%d volume=%d/%d)",
                 I2S_BCK_PIN, I2S_WS_PIN, I2S_DOUT_PIN, AUDIO_VOLUME_DEFAULT, AUDIO_VOLUME_STEPS);
        if (s_signalHandler) {
            s_signalHandler(audio_runtime::Signal::INIT_OK, s_signalContext);
        }
    }

    for (;;) {
        if (s_audio) {
            s_audio->loop();
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

} // namespace

namespace audio_runtime {

void setSignalHandler(SignalHandler handler, void* context) {
    s_signalHandler = handler;
    s_signalContext = context;
}

TaskHandle_t* taskHandlePtr() {
    return &s_audioTaskHandle;
}

bool start(IAudioPlayer& audio) {
    s_audio = &audio;

    const BaseType_t res = xTaskCreatePinnedToCore(
        audioTask,
        "AudioTask",
        AUDIO_TASK_STACK_SIZE,
        nullptr,
        AUDIO_TASK_PRIORITY,
        &s_audioTaskHandle,
        AUDIO_TASK_CORE
    );

    if (res != pdPASS) {
        ERROR_LOG("Failed to create audio task");
        return false;
    }

    PROD_LOG("Audio task started (core=%d priority=%d)", AUDIO_TASK_CORE, AUDIO_TASK_PRIORITY);
    return true;
}

} // namespace audio_runtime
