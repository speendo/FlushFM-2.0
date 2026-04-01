#include "components/board/board_info.h"

#include <Arduino.h>

#include "core/debug.h"

namespace board_info {

namespace {

constexpr const char* kLogSource = "BoardInfo";

}  // namespace

void print() {
    DEBUG_LOG(kLogSource, "--- Board Info ---");
    PROD_LOG(kLogSource, "Chip model   : %s rev%d", ESP.getChipModel(), ESP.getChipRevision());
    PROD_LOG(kLogSource, "CPU freq     : %u MHz", ESP.getCpuFreqMHz());
    PROD_LOG(kLogSource, "Flash size   : %u KB", ESP.getFlashChipSize() / 1024);
    PROD_LOG(kLogSource, "Free heap    : %u B", ESP.getFreeHeap());

#ifdef BOARD_HAS_PSRAM
    if (psramFound()) {
        PROD_LOG(kLogSource, "PSRAM size   : %u KB", ESP.getPsramSize() / 1024);
        PROD_LOG(kLogSource, "Free PSRAM   : %u B", ESP.getFreePsram());
    } else {
        ERROR_LOG(kLogSource, "PSRAM not detected - check hardware configuration");
    }
#else
    DEBUG_LOG(kLogSource, "BOARD_HAS_PSRAM not set - PSRAM check skipped");
#endif
}

} // namespace board_info
