// debug_cli.h – Debug-only Serial commands (tasks, loadtest, light sensor)
#pragma once

#ifdef DEBUG_ENABLED

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class Supervisor;
class ILightSensor;

namespace debug_cli {

void init(TaskHandle_t* audioTaskHandle, Supervisor* supervisorV2, ILightSensor* lightSensor = nullptr);

void setLightSensor(ILightSensor* lightSensor);

bool process(const char* cmd, const char* arg);

void printHelp();

} // namespace debug_cli

#endif // DEBUG_ENABLED
