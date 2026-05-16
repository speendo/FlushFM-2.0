#include "supervisor/supervisor_v2.h"
#include "core/debug.h"

namespace fatal {

constexpr const char* kLogSource = "Fatal";
constexpr TickType_t kDwellMs = 60000;

}  // namespace fatal

void fatalTask(SupervisorV2* supervisor) {
    PROD_LOG(fatal::kLogSource, "FATAL — system entering fatal state");

    // TODO: LED signalling placeholder
    // TODO: component cleanup placeholder

    vTaskDelay(pdMS_TO_TICKS(fatal::kDwellMs));

    TickType_t elapsed = xTaskGetTickCount() - supervisor->fatalEnteredTicks_;
    if (elapsed >= pdMS_TO_TICKS(fatal::kDwellMs)) {
        supervisor->fatalDeadlineElapsed_ = true;
    }
#if defined(ARDUINO)
    esp_deep_sleep_start();
#endif
}
