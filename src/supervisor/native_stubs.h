#pragma once

#include <cstring>

using EventGroupHandle_t = void*;
struct StaticEventGroup_t { uint8_t data[32]; };
using TickType_t = uint32_t;
using EventBits_t = uint32_t;
using TaskHandle_t = void*;

inline constexpr TickType_t pdMS_TO_TICKS(TickType_t ms) { return ms; }
inline constexpr TickType_t pdTICKS_TO_MS(TickType_t ticks) { return ticks; }
inline void vTaskDelay(TickType_t) {}
inline constexpr int pdTRUE = 1;
inline constexpr TickType_t portMAX_DELAY = 0xffffffffUL;

inline EventGroupHandle_t xEventGroupCreateStatic(StaticEventGroup_t* buffer) {
    std::memset(buffer, 0, sizeof(StaticEventGroup_t));
    return buffer;
}
inline EventBits_t xEventGroupClearBits(EventGroupHandle_t handle, EventBits_t bitsToClear) {
    if (handle == nullptr) return 0;
    auto* bits = reinterpret_cast<uint32_t*>(handle);
    *bits &= ~bitsToClear;
    return *bits;
}
inline EventBits_t xEventGroupSetBits(EventGroupHandle_t handle, EventBits_t bitsToSet) {
    if (handle == nullptr) return 0;
    auto* bits = reinterpret_cast<uint32_t*>(handle);
    *bits |= bitsToSet;
    return *bits;
}
inline EventBits_t xEventGroupGetBits(EventGroupHandle_t handle) {
    if (handle == nullptr) return 0;
    return *reinterpret_cast<uint32_t*>(handle);
}
inline EventBits_t xEventGroupWaitBits(EventGroupHandle_t handle, EventBits_t bitsToWaitFor,
                                        bool clearOnExit, bool waitForAll, TickType_t) {
    if (handle == nullptr) return 0;
    auto* bits = reinterpret_cast<uint32_t*>(handle);
    EventBits_t result = *bits & bitsToWaitFor;
    if (waitForAll && result != bitsToWaitFor) return 0;
    if (clearOnExit) *bits &= ~bitsToWaitFor;
    return result;
}
inline void xTaskCreatePinnedToCore(void (*task)(void*), const char*, uint32_t,
                                     void* param, uint32_t, TaskHandle_t*, int) {}
inline TickType_t xTaskGetTickCount() { return 0; }
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return nullptr; }
inline void xTaskNotifyGive(TaskHandle_t) {}
inline uint32_t ulTaskNotifyTake(bool, TickType_t) { return 0; }
