// debug_cli.cpp – Debug-only Serial commands
// Compiled only when DEBUG_ENABLED is defined.
#ifdef DEBUG_ENABLED

#include "components/cli/debug_cli.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdlib.h>
#include <string.h>

#include "ILightSensor.h"
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
static ILightSensor* s_lightSensor = nullptr;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static void printTaskList();
static void printTransitionStatus();
static void loadtestTask(void* param);
static void cmdLightThresh(const char* arg);
static void cmdLightAtten(const char* arg);
static void cmdLightInterval(const char* arg);
static void cmdLightEma(const char* arg);
static void cmdLightStatus();

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------
namespace debug_cli {

void init(TaskHandle_t* audioTaskHandle, Supervisor* supervisor, ILightSensor* lightSensor) {
    s_audioTaskHandle = audioTaskHandle;
    s_supervisor = supervisor;
    s_lightSensor = lightSensor;
}

void setLightSensor(ILightSensor* lightSensor) {
    s_lightSensor = lightSensor;
}

bool process(const char* cmd, const char* arg) {
    if (strcmp(cmd, "tasks") == 0) {
        printTaskList();
        return true;

    } else if (strcmp(cmd, "loadtest") == 0) {
        const BaseType_t r = xTaskCreatePinnedToCore(
            loadtestTask, "LoadTest", 2048, nullptr, 1, nullptr, 0);
        if (r == pdPASS) {
            PROD_LOG(kLogSource, "LoadTest task started on Core 0 for 5 seconds");
        } else {
            ERROR_LOG(kLogSource, "Failed to create LoadTest task");
        }
        return true;

    } else if (strcmp(cmd, "tstatus") == 0) {
        printTransitionStatus();
        return true;

    } else if (strcmp(cmd, "light") == 0) {
        if (!arg) return false;

        if (strncmp(arg, "thresh ", 7) == 0) {
            cmdLightThresh(arg + 7);
            return true;
        } else if (strncmp(arg, "atten ", 6) == 0) {
            cmdLightAtten(arg + 6);
            return true;
        } else if (strncmp(arg, "interval ", 9) == 0) {
            cmdLightInterval(arg + 9);
            return true;
        } else if (strncmp(arg, "ema ", 4) == 0) {
            cmdLightEma(arg + 4);
            return true;
        } else if (strcmp(arg, "status") == 0) {
            cmdLightStatus();
            return true;
        }
        return false;
    }

    return false;
}

void printHelp() {
    Serial.println("  tasks               Print FreeRTOS task list (core, state, stack HWM)");
    Serial.println("  loadtest            Run 5s busy-loop on Core 0, check audio stability");
    Serial.println("  tstatus             Show transition and component lifecycle status");
    Serial.println("  light thresh <n>     Set base threshold for flank detection (>= 0)");
    Serial.println("  light atten <V>      Set ADC attenuation (1.1, 1.5, 2.2, 3.3)");
    Serial.println("  light interval <ms>  Set raw reading interval in milliseconds");
    Serial.println("  light ema <fast> <trend>  Set EMA weights (0 < trend/slowDiv < fast/fastDiv)");
    Serial.println("  light status        Continuous light sensor readout (send x to exit)");
}

} // namespace debug_cli

// ---------------------------------------------------------------------------
// Light sensor commands (module-private)
// ---------------------------------------------------------------------------

static void cmdLightThresh(const char* arg) {
    if (!s_lightSensor) {
        ERROR_LOG(kLogSource, "Light sensor not available");
        return;
    }
    const long thresh = strtol(arg, nullptr, 10);
    if (thresh < 0) {
        ERROR_LOG(kLogSource, "Base threshold must be >= 0");
        return;
    }
    if (s_lightSensor->setBaseThreshold(static_cast<int32_t>(thresh))) {
        PROD_LOG(kLogSource, "Base threshold set: %ld", thresh);
    } else {
        ERROR_LOG(kLogSource, "Invalid base threshold: %ld", thresh);
    }
}

static void cmdLightAtten(const char* arg) {
    if (!s_lightSensor) {
        ERROR_LOG(kLogSource, "Light sensor not available");
        return;
    }
    float volts = atof(arg);
    if (s_lightSensor->setAttenuation(volts)) {
        PROD_LOG(kLogSource, "Attenuation set: %.1f V", (double)s_lightSensor->getAttenuation());
    } else {
        ERROR_LOG(kLogSource, "Invalid attenuation: %.1f (valid: 1.1, 1.5, 2.2, 3.3)", (double)volts);
    }
}

static void cmdLightInterval(const char* arg) {
    if (!s_lightSensor) {
        ERROR_LOG(kLogSource, "Light sensor not available");
        return;
    }
    const long ms = strtol(arg, nullptr, 10);
    if (ms < 1 || ms > UINT16_MAX) {
        ERROR_LOG(kLogSource, "Raw reading interval must be 1-65535 ms");
        return;
    }
    s_lightSensor->setRawReadingIntervalMs(static_cast<uint16_t>(ms));
    PROD_LOG(kLogSource, "Raw reading interval set: %ld ms", ms);
}

static void cmdLightEma(const char* arg) {
    if (!s_lightSensor) {
        ERROR_LOG(kLogSource, "Light sensor not available");
        return;
    }
    const char* space = strchr(arg, ' ');
    if (!space) {
        ERROR_LOG(kLogSource, "Usage: light ema <fastWeight> <trendWeight>");
        return;
    }
    const long fast = strtol(arg, nullptr, 10);
    const long trend = strtol(space + 1, nullptr, 10);
    if (fast < 1 || trend < 1) {
        ERROR_LOG(kLogSource, "EMA weights must be positive");
        return;
    }
    if (s_lightSensor->setEmaWeights(static_cast<int32_t>(fast), static_cast<int32_t>(trend))) {
        PROD_LOG(kLogSource, "EMA weights set: fast=%ld trend=%ld", fast, trend);
    } else {
        ERROR_LOG(kLogSource,
                  "Invalid EMA weights: need 0 < trend/slowDiv < fast/fastDiv");
    }
}

static void cmdLightStatus() {
    if (!s_lightSensor) {
        ERROR_LOG(kLogSource, "Light sensor not available");
        return;
    }

    Serial.println("Light sensor status (send 'x' or 'exit' to stop)");
    Serial.println("  raw | fastEMA | trendEMA | flank | state | thresh | atten | intv | fastW | trendW");

    for (;;) {
        s_lightSensor->poll();

        const uint16_t raw = s_lightSensor->readRaw();
        const int32_t fastEma = s_lightSensor->getFastEma();
        const int32_t trendEma = s_lightSensor->getTrendEma();
        const int32_t flank = s_lightSensor->getFlank();
        const bool lightOn = s_lightSensor->isLightOn();

        Serial.printf("  %4u | %7ld | %8ld | %5ld | %-5s | %6ld | %.1f  | %4u | %5ld | %6ld\r\n",
                      raw,
                      (long)fastEma,
                      (long)trendEma,
                      (long)flank,
                      lightOn ? "ON" : "OFF",
                      (long)s_lightSensor->getBaseThreshold(),
                      (double)s_lightSensor->getAttenuation(),
                      (unsigned)s_lightSensor->getRawReadingIntervalMs(),
                      (long)s_lightSensor->getEmaFastWeight(),
                      (long)s_lightSensor->getEmaTrendWeight());

        while (Serial.available()) {
            String line = Serial.readStringUntil('\n');
            line.trim();
            if (line == "x" || line == "exit") {
                Serial.println("Exiting light status mode.");
                return;
            }
        }

        delay(200);
    }
}

// ---------------------------------------------------------------------------
// Existing debug commands
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

static void loadtestTask(void* /*param*/) {
    const uint32_t end = millis() + 5000;
    uint32_t lastYield = millis();
    while (millis() < end) {
        if (millis() - lastYield >= 100) {
            vTaskDelay(1);
            lastYield = millis();
        }
    }
    PROD_LOG(kLogSource, "LoadTest finished -- Core 0 saturation ended");
    vTaskDelete(nullptr);
}

#endif // DEBUG_ENABLED
