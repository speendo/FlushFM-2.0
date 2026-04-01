#include <Audio.h>

#include "components/audio/audio_callbacks.h"
#include "core/debug.h"

namespace {

constexpr const char* kLogSource = "AudioCb";

const char* safeText(const char* text) {
    return text ? text : "";
}

// evt_info: Generic audio library diagnostic messages
// Contains: "Buffer status OK", "Codec detected: MP3", stream readiness, etc.
// Use: Debug diagnostics, performance monitoring
void handleInfoEvent(const char* msg) {
    DEBUG_LOG(kLogSource, "Info: %s", msg);
}

// evt_bitrate: Bitrate detected in stream (ICY or metadata)
// Contains: Bitrate value in b/s (e.g., "256000")
// Use: Quality assessment, stream stability monitoring
void handleBitrateEvent(const char* msg) {
    DEBUG_LOG(kLogSource, "Bitrate: %s b/s", msg);
}

// evt_icyurl: URL embedded in ICY metadata
// Contains: Link to station website or related resource
// Use: Display station info, hyperlinks on UI
void handleIcyUrlEvent(const char* msg) {
    DEBUG_LOG(kLogSource, "IcyUrl: %s", msg);
}

// evt_name: Station/source name from ICY metadata or playlist
// Contains: "SomaFM", etc.
// Use: Display on UI, visible to user (PROD_LOG)
void handleNameEvent(const char* msg) {
    PROD_LOG(kLogSource, "Station: %s", msg);
}

// evt_streamtitle: Current track/song title from ICY metadata
// Contains: "Artist - Song Title" or full track name
// Use: Display on UI, visible to user (PROD_LOG)
void handleStreamTitleEvent(const char* msg) {
    PROD_LOG(kLogSource, "Track: %s", msg);
}

// evt_icylogo: Logo URL embedded in ICY metadata
// Contains: URL to station logo image
// Use: Download and display logo on TFT
void handleIcyLogoEvent(const char* msg) {
    PROD_LOG(kLogSource, "LogoUrl: %s", msg);
}

// evt_icydescription: Stream description from ICY metadata
// Contains: "Description of the station/stream"
// Use: Display stream description on TFT, metadata display
void handleIcyDescriptionEvent(const char* msg) {
    PROD_LOG(kLogSource, "Description: %s", msg);
}

// evt_lasthost: Last HTTP host that was connected to
// Contains: Hostname or IP address of the streaming server
// Use: Connection debugging, diagnostics, fallback logic
void handleLastHostEvent(const char* msg) {
    DEBUG_LOG(kLogSource, "Host: %s", msg);
}

// evt_eof: End-of-file or stream termination
// Contains: Name of the stream/file that ended
// Use: Detect stream errors/disconnection, trigger skip to next station
void handleEofEvent(const char* msg) {
    ERROR_LOG(kLogSource, "StreamEof: connection lost: %s", msg);
}

// evt_log: Structured log message from library (ESP-IDF derived)
// Contains: Log text, level stored in message.s (LOGE, LOGW, LOGI, LOGD, LOGV)
// Use: Error reporting, library diagnostics, performance issues
void handleLogEvent(Audio::msg_t message) {
    const char* msg = safeText(message.msg);
    const char* level = safeText(message.s);

    if (strcmp(level, "LOGE") == 0) {
        ERROR_LOG(kLogSource, "LibraryError: %s", msg);
    } else if (strcmp(level, "LOGW") == 0) {
        DEBUG_LOG(kLogSource, "LibraryWarn: %s", msg);
    } else {
        DEBUG_LOG(kLogSource, "LibraryInfo: %s", msg);
    }
}

void logAudioMessage(Audio::msg_t message) {
    const char* text = safeText(message.msg);
    if (text[0] == '\0') {
        return;
    }

    switch (message.e) {
        case Audio::evt_info:
            handleInfoEvent(text);
            break;

        case Audio::evt_bitrate:
            handleBitrateEvent(text);
            break;

        case Audio::evt_icyurl:
            handleIcyUrlEvent(text);
            break;

        case Audio::evt_name:
            handleNameEvent(text);
            break;

        case Audio::evt_streamtitle:
            handleStreamTitleEvent(text);
            break;

        case Audio::evt_icylogo:
            handleIcyLogoEvent(text);
            break;

        case Audio::evt_icydescription:
            handleIcyDescriptionEvent(text);
            break;

        case Audio::evt_lasthost:
            handleLastHostEvent(text);
            break;

        case Audio::evt_eof:
            handleEofEvent(text);
            break;

        case Audio::evt_log:
            handleLogEvent(message);
            break;

        default:
            DEBUG_LOG(kLogSource, "UnhandledEvent %d: %s", message.e, text);
            break;
    }
}

}  // namespace

void registerAudioLibraryCallbacks() {
    Audio::audio_info_callback = logAudioMessage;
}
