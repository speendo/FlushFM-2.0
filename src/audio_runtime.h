#pragma once

#include "IAudioPlayer.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace audio_runtime {

enum class Signal {
	INIT_OK,
	INIT_FAILED,
};

using SignalHandler = void (*)(Signal signal, void* context);

void setSignalHandler(SignalHandler handler, void* context);

// Returns a stable pointer to the internal audio task handle for diagnostics.
TaskHandle_t* taskHandlePtr();

// Starts the dedicated audio task on the configured core.
// Returns true on success.
bool start(IAudioPlayer& audio);

} // namespace audio_runtime
