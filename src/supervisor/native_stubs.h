#pragma once

#include <cstring>

using EventGroupHandle_t = void*;
struct StaticEventGroup_t { uint8_t data[32]; };
using TickType_t = uint32_t;
using EventBits_t = uint32_t;
using TaskHandle_t = void*;

inline constexpr TickType_t pdMS_TO_TICKS(TickType_t ms) { return ms; }
inline void vTaskDelay(TickType_t) {}

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
inline void xTaskCreatePinnedToCore(void (*task)(void*), const char*, uint32_t,
                                     void* param, uint32_t, TaskHandle_t*, int) {}
inline TickType_t xTaskGetTickCount() { return 0; }
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return nullptr; }
inline void xTaskNotifyGive(TaskHandle_t) {}
inline uint32_t ulTaskNotifyTake(bool, TickType_t) { return 0; }
