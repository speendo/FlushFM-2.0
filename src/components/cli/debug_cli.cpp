// debug_cli.cpp – Debug-only Serial commands
// Compiled only when DEBUG_ENABLED is defined.
#ifdef DEBUG_ENABLED

#include "components/cli/debug_cli.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "component_types.h"
#include "core/config.h"
#include "core/debug.h"
#include "supervisor/supervisor.h"

// ---------------------------------------------------------------------------
// Module-private state
// ---------------------------------------------------------------------------
static constexpr const char* kLogSource = "DebugCLI";
static TaskHandle_t* s_audioTaskHandle = nullptr;
static Supervisor* s_supervisor = nullptr;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static void printTaskList();
static void printTransitionStatus();
static void loadtestTask(void* param);

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------
namespace debug_cli {

void init(TaskHandle_t* audioTaskHandle, Supervisor* supervisor) {
    s_audioTaskHandle = audioTaskHandle;
    s_supervisor = supervisor;
}

bool process(const char* cmd, const char* arg) {
    if (strcmp(cmd, "tasks") == 0) {
        printTaskList();
        return true;

    } else if (strcmp(cmd, "loadtest") == 0) {
        const BaseType_t r = xTaskCreatePinnedToCore(
            loadtestTask, "LoadTest", 2048, nullptr, 1, nullptr, 0);
        if (r == pdPASS) {
            PROD_LOG(kLogSource, "LoadTest task started on Core 0 for 5 seconds – listen for audio dropouts");
        } else {
            ERROR_LOG(kLogSource, "Failed to create LoadTest task");
        }
        return true;

    } else if (strcmp(cmd, "tstatus") == 0) {
        printTransitionStatus();
        return true;
    }

    return false;
}

void printHelp() {
    Serial.println("  tasks               Print FreeRTOS task list (core, state, stack HWM)");
    Serial.println("  loadtest            Run 5s busy-loop on Core 0, check audio stability");
    Serial.println("  tstatus             Show transition and component lifecycle status");
}

} // namespace debug_cli

// ---------------------------------------------------------------------------
// Implementations (module-private)
// ---------------------------------------------------------------------------

static void printTaskList() {
    Serial.println();
    Serial.println("Task / Memory Report:");
    Serial.println("---------------------------------------------");

    if (s_audioTaskHandle && *s_audioTaskHandle) {
        const UBaseType_t hwm = uxTaskGetStackHighWaterMark(*s_audioTaskHandle);
        Serial.printf("  AudioTask   core=%d  prio=%d  stackHWM=%u DW\r\n",
                      AUDIO_TASK_CORE, AUDIO_TASK_PRIORITY, (unsigned)hwm);
    } else {
        Serial.println("  AudioTask   handle not available");
    }

    Serial.printf("  loopTask    core=1  (Arduino default)\r\n");
    Serial.println("  [WiFi/TCP/IDLE tasks on Core 0 are framework-internal, not application tasks]");
    Serial.println();
    Serial.printf("  Free heap    : %u B\r\n",  (unsigned)ESP.getFreeHeap());
    if (psramFound()) {
        Serial.printf("  Free PSRAM   : %u B\r\n", (unsigned)ESP.getFreePsram());
    }
    Serial.println();
}

static void printTransitionStatus() {
    if (!s_supervisor) {
        ERROR_LOG(kLogSource, "Supervisor not available");
        return;
    }

    Serial.println();
    Serial.printf("SM state:        %s\r\n", stateToString(s_supervisor->getObservedState()));
    Serial.printf("Target:          %s\r\n", stateToString(s_supervisor->getTargetState()));
    Serial.println();
}

// Busy-loop task on Core 0 for 5 seconds to stress-test audio stability.
// A brief yield every 100 ms lets IDLE0 reset its own WDT subscription while
// keeping Core 0 saturated >99% of the time.
static void loadtestTask(void* /*param*/) {
    const uint32_t end = millis() + 5000;
    uint32_t lastYield = millis();
    while (millis() < end) {
        if (millis() - lastYield >= 100) {
            vTaskDelay(1);
            lastYield = millis();
        }
    }
    PROD_LOG(kLogSource, "LoadTest finished – Core 0 saturation ended");
    vTaskDelete(nullptr);
}

#endif // DEBUG_ENABLED
