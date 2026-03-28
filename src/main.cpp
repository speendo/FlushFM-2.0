#include <Arduino.h>

#include "AudioPlayerESP32.h"
#include "IAudioPlayer.h"
#include "audio_runtime.h"
#include "board_info.h"
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
    board_info::print();

    wifi_manager::init();
    cli::init(s_audio, audio_runtime::taskHandlePtr());

    if (!audio_runtime::start(s_audio)) {
        ERROR_LOG("Audio runtime startup failed");
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

