#include "board_info.h"

#include <Arduino.h>

#include "debug.h"

namespace board_info {

void print() {
    DEBUG_LOG("--- Board Info ---");
    PROD_LOG("Chip model   : %s rev%d", ESP.getChipModel(), ESP.getChipRevision());
    PROD_LOG("CPU freq     : %u MHz", ESP.getCpuFreqMHz());
    PROD_LOG("Flash size   : %u KB", ESP.getFlashChipSize() / 1024);
    PROD_LOG("Free heap    : %u B", ESP.getFreeHeap());

#ifdef BOARD_HAS_PSRAM
    if (psramFound()) {
        PROD_LOG("PSRAM size   : %u KB", ESP.getPsramSize() / 1024);
        PROD_LOG("Free PSRAM   : %u B", ESP.getFreePsram());
    } else {
        ERROR_LOG("PSRAM not detected - check hardware configuration");
    }
#else
    DEBUG_LOG("BOARD_HAS_PSRAM not set - PSRAM check skipped");
#endif
}

} // namespace board_info
