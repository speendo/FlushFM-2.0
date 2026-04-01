// cli.h – Serial command dispatch (production commands)
// Debug commands are added automatically in debug builds via debug_cli.h.
#pragma once

#include <stddef.h>

#include "IAudioPlayer.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class SystemController;

namespace cli {

// Wire the CLI to the audio player, the audio task handle pointer, and the SystemController
// for state-machine event posting. controllerMay be nullptr to disable event-routed commands.
void init(IAudioPlayer& audio, TaskHandle_t* audioTaskHandle, SystemController* controller = nullptr);

// Non-blocking single-line reader over Serial.
// Returns true when a complete newline-terminated line is available in buf.
bool readLine(char* buf, size_t maxLen);

// Parse and dispatch one command line.
void process(const char* line);

// Print all available commands to Serial.
void printHelp();

} // namespace cli
