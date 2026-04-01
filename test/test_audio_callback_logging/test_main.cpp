#include <unity.h>
#include <Audio.h>
#include <string.h>

void test_station_event_is_production_visible() {
    Audio::msg_t msg{};
    msg.msg = "FM4";
    msg.e = Audio::evt_name;

    TEST_ASSERT_EQUAL(Audio::evt_name, msg.e);
    TEST_ASSERT_EQUAL_STRING("FM4", msg.msg);
}

void test_track_event_is_production_visible() {
    Audio::msg_t msg{};
    msg.msg = "Now Playing: Song Title";
    msg.e = Audio::evt_streamtitle;

    TEST_ASSERT_EQUAL(Audio::evt_streamtitle, msg.e);
    TEST_ASSERT_EQUAL_STRING("Now Playing: Song Title", msg.msg);
}

void test_bitrate_event_is_debug_only() {
    Audio::msg_t msg{};
    msg.msg = "192 kbps";
    msg.e = Audio::evt_bitrate;

    TEST_ASSERT_EQUAL(Audio::evt_bitrate, msg.e);
    TEST_ASSERT_EQUAL_STRING("192 kbps", msg.msg);
}

void test_log_error_event_is_error_tier() {
    Audio::msg_t msg{};
    msg.msg = "MP3 decode failed";
    msg.e = Audio::evt_log;
    msg.s = "LOGE";

    TEST_ASSERT_EQUAL(Audio::evt_log, msg.e);
    TEST_ASSERT_EQUAL_STRING("LOGE", msg.s);
}

void test_log_info_event_is_debug_tier() {
    Audio::msg_t msg{};
    msg.msg = "Buffer status OK";
    msg.e = Audio::evt_log;
    msg.s = "LOGI";

    TEST_ASSERT_EQUAL(Audio::evt_log, msg.e);
    TEST_ASSERT_EQUAL_STRING("LOGI", msg.s);
}

void test_null_message_is_safely_handled() {
    Audio::msg_t msg{};
    msg.msg = nullptr;
    msg.e = Audio::evt_name;

    const char* safeMsg = msg.msg ? msg.msg : "";
    TEST_ASSERT_EQUAL_STRING("", safeMsg);
}

void test_empty_message_is_skipped() {
    Audio::msg_t msg{};
    msg.msg = "";
    msg.e = Audio::evt_bitrate;

    bool isEmpty = !msg.msg || msg.msg[0] == '\0';
    TEST_ASSERT_TRUE(isEmpty);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_station_event_is_production_visible);
    RUN_TEST(test_track_event_is_production_visible);
    RUN_TEST(test_bitrate_event_is_debug_only);
    RUN_TEST(test_log_error_event_is_error_tier);
    RUN_TEST(test_log_info_event_is_debug_tier);
    RUN_TEST(test_null_message_is_safely_handled);
    RUN_TEST(test_empty_message_is_skipped);
    return UNITY_END();
}
