#include "system_controller.h"

#include "debug.h"

SystemController::SystemController() {
    queue_ = xQueueCreate(16, sizeof(QueuedEvent));
}

SystemState SystemController::state() const {
    return state_;
}

void SystemController::subscribe(StateObserver observer) {
    observers_.push_back(observer);
}

bool SystemController::postEvent(SystemEvent event, SystemReason reason) {
    if (!queue_) {
        return false;
    }
    const QueuedEvent queued{event, reason};
    return xQueueSend(queue_, &queued, 0) == pdTRUE;
}

void SystemController::dispatchPending() {
    if (!queue_) {
        return;
    }

    QueuedEvent queued{};
    while (xQueueReceive(queue_, &queued, 0) == pdTRUE) {
        handleEvent(queued.event, queued.reason);
    }
}

void SystemController::handleEvent(SystemEvent event, SystemReason reason) {
    switch (state_) {
        case SystemState::OFF:
            if (event == SystemEvent::BOOT) {
                transitionTo(SystemState::STARTING, event, reason);
            }
            break;

        case SystemState::STARTING:
            if (event == SystemEvent::WIFI_READY) {
                transitionTo(SystemState::IDLE, event, reason);
            } else if (event == SystemEvent::COMPONENT_SETUP_FAILED) {
                transitionTo(SystemState::ERROR, event, reason);
            } else if (event == SystemEvent::AUDIO_INIT_FAILED) {
                transitionTo(SystemState::ERROR, event, reason);
            } else if (event == SystemEvent::ENTER_OFF) {
                transitionTo(SystemState::OFF, event, reason);
            }
            break;

        case SystemState::IDLE:
            if (event == SystemEvent::PLAY_REQUESTED) {
                transitionTo(SystemState::STREAMING, event, reason);
            } else if (event == SystemEvent::COMPONENT_SETUP_FAILED) {
                transitionTo(SystemState::ERROR, event, reason);
            } else if (event == SystemEvent::AUDIO_INIT_FAILED) {
                transitionTo(SystemState::ERROR, event, reason);
            } else if (event == SystemEvent::ENTER_OFF) {
                transitionTo(SystemState::OFF, event, reason);
            }
            break;

        case SystemState::STREAMING:
            if (event == SystemEvent::STOP_REQUESTED) {
                transitionTo(SystemState::IDLE, event, reason);
            } else if (event == SystemEvent::AUDIO_INIT_FAILED) {
                transitionTo(SystemState::ERROR, event, reason);
            } else if (event == SystemEvent::ENTER_OFF) {
                transitionTo(SystemState::OFF, event, reason);
            }
            break;

        case SystemState::ERROR:
            if (event == SystemEvent::RECOVER) {
                transitionTo(SystemState::IDLE, event, reason);
            } else if (event == SystemEvent::ENTER_OFF) {
                transitionTo(SystemState::OFF, event, reason);
            }
            break;
    }
}

void SystemController::transitionTo(SystemState next, SystemEvent trigger, SystemReason reason) {
    if (next == state_) {
        return;
    }

    const SystemState previous = state_;
    state_ = next;

    if (next == SystemState::ERROR) {
        transientError_ = true;
    }
    if (next == SystemState::OFF || next == SystemState::STARTING || next == SystemState::IDLE) {
        transientError_ = false;
    }

    PROD_LOG("State transition: %s -> %s (event=%s reason=%s)",
             toString(previous), toString(next), toString(trigger), toString(reason));

    for (const auto& observer : observers_) {
        observer(next);
    }
}

const char* toString(SystemState state) {
    switch (state) {
        case SystemState::OFF:
            return "OFF";
        case SystemState::STARTING:
            return "STARTING";
        case SystemState::IDLE:
            return "IDLE";
        case SystemState::STREAMING:
            return "STREAMING";
        case SystemState::ERROR:
            return "ERROR";
    }
    return "UNKNOWN";
}

const char* toString(SystemEvent event) {
    switch (event) {
        case SystemEvent::BOOT:
            return "BOOT";
        case SystemEvent::COMPONENT_SETUP_FAILED:
            return "COMPONENT_SETUP_FAILED";
        case SystemEvent::WIFI_READY:
            return "WIFI_READY";
        case SystemEvent::AUDIO_INIT_OK:
            return "AUDIO_INIT_OK";
        case SystemEvent::AUDIO_INIT_FAILED:
            return "AUDIO_INIT_FAILED";
        case SystemEvent::PLAY_REQUESTED:
            return "PLAY_REQUESTED";
        case SystemEvent::STOP_REQUESTED:
            return "STOP_REQUESTED";
        case SystemEvent::RECOVER:
            return "RECOVER";
        case SystemEvent::ENTER_OFF:
            return "ENTER_OFF";
    }
    return "UNKNOWN";
}

const char* toString(SystemReason reason) {
    switch (reason) {
        case SystemReason::NONE:
            return "NONE";
        case SystemReason::BOOT_SEQUENCE:
            return "BOOT_SEQUENCE";
        case SystemReason::COMPONENT_SETUP:
            return "COMPONENT_SETUP";
        case SystemReason::WIFI_INITIALIZED:
            return "WIFI_INITIALIZED";
        case SystemReason::AUDIO_TASK_STARTED:
            return "AUDIO_TASK_STARTED";
        case SystemReason::AUDIO_TASK_INIT_FAILED:
            return "AUDIO_TASK_INIT_FAILED";
        case SystemReason::USER_REQUEST:
            return "USER_REQUEST";
        case SystemReason::RECOVERY:
            return "RECOVERY";
        case SystemReason::POWER_POLICY:
            return "POWER_POLICY";
    }
    return "UNKNOWN";
}
