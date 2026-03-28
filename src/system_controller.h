#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <functional>
#include <vector>

enum class SystemState {
    OFF,
    STARTING,
    IDLE,
    STREAMING,
    ERROR,
};

enum class SystemEvent {
    BOOT,
    COMPONENT_SETUP_FAILED,
    WIFI_READY,
    AUDIO_INIT_OK,
    AUDIO_INIT_FAILED,
    PLAY_REQUESTED,
    STOP_REQUESTED,
    RECOVER,
    ENTER_OFF,
};

enum class SystemReason {
    // Event origin/context used for transition telemetry and diagnostics.
    // This is not a causal rule engine input.
    NONE,
    BOOT_SEQUENCE,
    COMPONENT_SETUP,
    WIFI_INITIALIZED,
    AUDIO_TASK_STARTED,
    AUDIO_TASK_INIT_FAILED,
    USER_REQUEST,
    RECOVERY,
    POWER_POLICY,
};

class SystemController {
public:
    using StateObserver = std::function<void(SystemState)>;

    SystemController();

    SystemState state() const;
    void subscribe(StateObserver observer);

    // Thread-safe event enqueue for any core/task.
    // reason carries origin/context metadata for logging and debugging.
    bool postEvent(SystemEvent event, SystemReason reason);

    // Core 0 only: process pending events and run transition logic.
    void dispatchPending();

private:
    struct QueuedEvent {
        SystemEvent event;
        SystemReason reason;
    };

    void handleEvent(SystemEvent event, SystemReason reason);
    void transitionTo(SystemState next, SystemEvent trigger, SystemReason reason);

    SystemState state_ = SystemState::OFF;
    bool transientError_ = false;
    QueueHandle_t queue_ = nullptr;
    std::vector<StateObserver> observers_;
};

const char* toString(SystemState state);
const char* toString(SystemEvent event);
const char* toString(SystemReason reason);
