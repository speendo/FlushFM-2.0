#include <Arduino.h>

#include "AudioPlayerESP32.h"
#include "IAudioPlayer.h"
#include "cli.h"
#include "config.h"
#include "debug.h"
#include "wifi_manager.h"

// ---------------------------------------------------------------------------
// Audio – concrete instance wired here; rest of code depends on interface only
// ---------------------------------------------------------------------------
static AudioPlayerESP32 s_playerImpl(I2S_BCK_PIN, I2S_WS_PIN, I2S_DOUT_PIN);
static IAudioPlayer& s_audio = s_playerImpl;

// ---------------------------------------------------------------------------
// Runtime state
// ---------------------------------------------------------------------------
static TaskHandle_t s_audioTaskHandle = nullptr;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static void printBoardInfo();

// Audio library callbacks – must be free functions (required by ESP32-audioI2S)
void audio_info(const char* info);
void audio_showstation(const char* info);
void audio_showstreamtitle(const char* info);
void audio_bitrate(const char* info);

// ---------------------------------------------------------------------------
// Audio task – runs at priority 2 to prevent buffer underruns while allowing
// the watchdog and WiFi/TCP stack to get CPU time.
// begin() is called here (not in setup()) so the Audio object is
// initialized on the same core that calls loop() – required for thread safety.
// ---------------------------------------------------------------------------
static void audioTask(void* /*param*/) {
    if (!s_audio.begin()) {
        ERROR_LOG("Audio init failed inside task – check I2S wiring (BCK=%d WS=%d DOUT=%d)",
                  I2S_BCK_PIN, I2S_WS_PIN, I2S_DOUT_PIN);
    } else {
        s_audio.setVolumeSteps(AUDIO_VOLUME_STEPS);
        s_audio.setVolume(AUDIO_VOLUME_DEFAULT);
        PROD_LOG("Audio initialized in task (BCK=%d WS=%d DOUT=%d volume=%d/%d)",
                 I2S_BCK_PIN, I2S_WS_PIN, I2S_DOUT_PIN, AUDIO_VOLUME_DEFAULT, AUDIO_VOLUME_STEPS);
    }
    for (;;) {
        s_audio.loop();
        vTaskDelay(pdMS_TO_TICKS(1));  // Feed watchdog; 1 ms is short enough for audio continuity
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

    PROD_LOG("Hello FlushFM");
    printBoardInfo();

    wifi_manager::init();
    cli::init(s_audio, &s_audioTaskHandle);

    // Audio is fully initialized inside audioTask (thread-safety: same core as loop()).
    // Spawn dedicated audio task on Core 0
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
        ERROR_LOG("Failed to create audio task – falling back to loop()");
    } else {
        PROD_LOG("Audio task started (core=%d priority=%d)",
                 AUDIO_TASK_CORE, AUDIO_TASK_PRIORITY);
    }

    cli::printHelp();
}

void loop() {
    // Audio is handled by audioTask; loop() is Serial-only.
    static char cmdBuf[SERIAL_CMD_BUF_SIZE];
    if (cli::readLine(cmdBuf, sizeof(cmdBuf))) {
        cli::process(cmdBuf);
    }
}

// ---------------------------------------------------------------------------
// Board info
// ---------------------------------------------------------------------------

static void printBoardInfo() {
    DEBUG_LOG("--- Board Info ---");
    PROD_LOG("Chip model   : %s rev%d", ESP.getChipModel(), ESP.getChipRevision());
    PROD_LOG("CPU freq     : %u MHz", ESP.getCpuFreqMHz());
    PROD_LOG("Flash size   : %u KB", ESP.getFlashChipSize() / 1024);
    PROD_LOG("Free heap    : %u B",  ESP.getFreeHeap());

#ifdef BOARD_HAS_PSRAM
    if (psramFound()) {
        PROD_LOG("PSRAM size   : %u KB", ESP.getPsramSize() / 1024);
        PROD_LOG("Free PSRAM   : %u B",  ESP.getFreePsram());
    } else {
        ERROR_LOG("PSRAM not detected – check hardware configuration");
    }
#else
    DEBUG_LOG("BOARD_HAS_PSRAM not set – PSRAM check skipped");
#endif
}

// ---------------------------------------------------------------------------
// ESP32-audioI2S callbacks
// ---------------------------------------------------------------------------

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

