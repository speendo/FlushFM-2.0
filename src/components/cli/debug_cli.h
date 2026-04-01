// debug_cli.h – Debug-only Serial commands (tasks, loadtest, suspend, resume)
// The entire module is compiled only when DEBUG_ENABLED is defined.
#pragma once

#ifdef DEBUG_ENABLED

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace debug_cli {

// Store the audio task handle used by suspend/resume commands.
// Pass a pointer so the value is picked up after the task is created.
void init(TaskHandle_t* audioTaskHandle);

// Attempt to handle cmd+arg as a debug command.
// Returns true if the command was handled; false if unknown (caller should continue).
bool process(const char* cmd, const char* arg);

void printHelp();

} // namespace debug_cli

#endif // DEBUG_ENABLED
