// cli.h – Serial command dispatch (production commands)
// Debug commands are added automatically in debug builds via debug_cli.h.
#pragma once

#include <stddef.h>

#include "IAudioPlayer.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace cli {

// Wire the CLI to the audio player and the audio task handle pointer.
// audioTaskHandle may be nullptr initially; pass the address of the variable
// set by xTaskCreatePinnedToCore so debug_cli can see the populated value.
void init(IAudioPlayer& audio, TaskHandle_t* audioTaskHandle);

// Non-blocking single-line reader over Serial.
// Returns true when a complete newline-terminated line is available in buf.
bool readLine(char* buf, size_t maxLen);

// Parse and dispatch one command line.
void process(const char* line);

// Print all available commands to Serial.
void printHelp();

} // namespace cli
