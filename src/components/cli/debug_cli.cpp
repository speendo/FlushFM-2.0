// debug_cli.cpp – Debug-only Serial commands
// Compiled only when DEBUG_ENABLED is defined.
#ifdef DEBUG_ENABLED

#include "components/cli/debug_cli.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "core/config.h"
#include "core/debug.h"
#include "state_machine/system_controller.h"

// ---------------------------------------------------------------------------
// Module-private state
// ---------------------------------------------------------------------------
static constexpr const char* kLogSource = "DebugCLI";
static TaskHandle_t* s_audioTaskHandle = nullptr;
static SystemController* s_controller = nullptr;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static void printTaskList();
static void loadtestTask(void* param);

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------
namespace debug_cli {

static bool postManualTransition(const char* targetState) {
    if (!s_controller) {
        ERROR_LOG(kLogSource, "SystemController not available for transition command");
        return true;
    }

    if (!targetState || targetState[0] == '\0') {
        ERROR_LOG(kLogSource, "Usage: transition <idle|streaming|off|error>");
        return true;
    }

    if (strcmp(targetState, "idle") == 0) {
        (void)s_controller->postEvent(SystemEvent::STOP_REQUESTED, SystemReason::USER_REQUEST, EventPolicy::BOUNDED_BLOCKING);
        PROD_LOG(kLogSource, "Transition request posted: idle");
        return true;
    }

    if (strcmp(targetState, "streaming") == 0) {
        (void)s_controller->postEvent(SystemEvent::PLAY_REQUESTED, SystemReason::USER_REQUEST, EventPolicy::BOUNDED_BLOCKING);
        PROD_LOG(kLogSource, "Transition request posted: streaming");
        return true;
    }

    if (strcmp(targetState, "off") == 0) {
        (void)s_controller->postEvent(SystemEvent::ENTER_OFF, SystemReason::USER_REQUEST, EventPolicy::BOUNDED_BLOCKING);
        PROD_LOG(kLogSource, "Transition request posted: off");
        return true;
    }

    if (strcmp(targetState, "error") == 0) {
        (void)s_controller->postEvent(SystemEvent::AUDIO_INIT_FAILED, SystemReason::USER_REQUEST, EventPolicy::BOUNDED_BLOCKING);
        PROD_LOG(kLogSource, "Transition request posted: error");
        return true;
    }

    ERROR_LOG(kLogSource, "Unknown transition target: %s", targetState);
    ERROR_LOG(kLogSource, "Usage: transition <idle|streaming|off|error>");
    return true;
}

void init(TaskHandle_t* audioTaskHandle, SystemController* controller) {
    s_audioTaskHandle = audioTaskHandle;
    s_controller = controller;
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

    } else if (strcmp(cmd, "suspend") == 0) {
        if (!s_audioTaskHandle || !*s_audioTaskHandle) {
            ERROR_LOG(kLogSource, "Audio task handle not available");
            return true;
        }
        PROD_LOG(kLogSource, "Suspending AudioTask – audio will stop");
        vTaskSuspend(*s_audioTaskHandle);
        return true;

    } else if (strcmp(cmd, "resume") == 0) {
        if (!s_audioTaskHandle || !*s_audioTaskHandle) {
            ERROR_LOG(kLogSource, "Audio task handle not available");
            return true;
        }
        vTaskResume(*s_audioTaskHandle);
        PROD_LOG(kLogSource, "AudioTask resumed");
        return true;
    } else if (strcmp(cmd, "transition") == 0) {
        return postManualTransition(arg);
    }

    return false;
}

void printHelp() {
    Serial.println("  tasks            Print FreeRTOS task list (core, state, stack HWM)");
    Serial.println("  loadtest         Run 5s busy-loop on Core 0, check audio stability");
    Serial.println("  suspend          Suspend AudioTask");
    Serial.println("  resume           Resume AudioTask");
    Serial.println("  transition <s>   Request state transition: idle|streaming|off|error");
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
