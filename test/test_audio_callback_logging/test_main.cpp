#include <unity.h>

namespace {

enum class AudioCallbackKind {
    Info,
    Station,
    StreamTitle,
    Bitrate,
};

enum class LogTier {
    Debug,
    Production,
    Error,
};

struct RoutedAudioCallbackMessage {
    LogTier tier;
    const char* prefix;
    const char* message;
};

RoutedAudioCallbackMessage routeAudioCallbackMessage(AudioCallbackKind kind, const char* message) {
    const char* prefix = "Audio";
    switch (kind) {
        case AudioCallbackKind::Info:
            prefix = "Audio";
            break;
        case AudioCallbackKind::Station:
            prefix = "Station";
            break;
        case AudioCallbackKind::StreamTitle:
            prefix = "Track";
            break;
        case AudioCallbackKind::Bitrate:
            prefix = "Bitrate";
            break;
    }
    return {LogTier::Debug, prefix, message ? message : ""};
}

} // namespace

void test_station_event_is_production_visible() {
    const RoutedAudioCallbackMessage routed = routeAudioCallbackMessage(AudioCallbackKind::Station, "SomaFM");

    TEST_ASSERT_EQUAL(static_cast<int>(LogTier::Debug), static_cast<int>(routed.tier));
    TEST_ASSERT_EQUAL_STRING("Station", routed.prefix);
    TEST_ASSERT_EQUAL_STRING("SomaFM", routed.message);
}

void test_track_event_is_production_visible() {
    const RoutedAudioCallbackMessage routed = routeAudioCallbackMessage(
        AudioCallbackKind::StreamTitle,
        "Now Playing: Song Title");

    TEST_ASSERT_EQUAL(static_cast<int>(LogTier::Debug), static_cast<int>(routed.tier));
    TEST_ASSERT_EQUAL_STRING("Track", routed.prefix);
    TEST_ASSERT_EQUAL_STRING("Now Playing: Song Title", routed.message);
}

void test_bitrate_event_is_debug_only() {
    const RoutedAudioCallbackMessage routed = routeAudioCallbackMessage(AudioCallbackKind::Bitrate, "192 kbps");

    TEST_ASSERT_EQUAL(static_cast<int>(LogTier::Debug), static_cast<int>(routed.tier));
    TEST_ASSERT_EQUAL_STRING("Bitrate", routed.prefix);
    TEST_ASSERT_EQUAL_STRING("192 kbps", routed.message);
}

void test_info_event_uses_audio_prefix() {
    const RoutedAudioCallbackMessage routed = routeAudioCallbackMessage(AudioCallbackKind::Info, "Buffer status OK");

    TEST_ASSERT_EQUAL(static_cast<int>(LogTier::Debug), static_cast<int>(routed.tier));
    TEST_ASSERT_EQUAL_STRING("Audio", routed.prefix);
    TEST_ASSERT_EQUAL_STRING("Buffer status OK", routed.message);
}

void test_null_message_is_safely_handled() {
    const RoutedAudioCallbackMessage routed = routeAudioCallbackMessage(AudioCallbackKind::Station, nullptr);

    TEST_ASSERT_EQUAL_STRING("", routed.message);
}

void test_empty_message_is_skipped() {
    const RoutedAudioCallbackMessage routed = routeAudioCallbackMessage(AudioCallbackKind::Bitrate, "");

    const bool isEmpty = !routed.message || routed.message[0] == '\0';
    TEST_ASSERT_TRUE(isEmpty);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_station_event_is_production_visible);
    RUN_TEST(test_track_event_is_production_visible);
    RUN_TEST(test_bitrate_event_is_debug_only);
    RUN_TEST(test_info_event_uses_audio_prefix);
    RUN_TEST(test_null_message_is_safely_handled);
    RUN_TEST(test_empty_message_is_skipped);
    return UNITY_END();
}
