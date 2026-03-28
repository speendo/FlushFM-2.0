#include "settings.h"

#include <Preferences.h>
#include <string.h>

namespace {

constexpr const char* kNamespace = "flushfm";
constexpr const char* kSsidKey = "ssid";
constexpr const char* kPassKey = "pass";
constexpr const char* kStationKey = "station";

bool loadString(const char* key, char* out, size_t outLen) {
    if (!out || outLen == 0) {
        return false;
    }

    out[0] = '\0';

    Preferences prefs;
    if (!prefs.begin(kNamespace, true)) {
        return false;
    }

    const size_t valueLen = prefs.getString(key, out, outLen);
    prefs.end();

    return valueLen > 0;
}

bool saveString(const char* key, const char* value) {
    if (!value) {
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
        return false;
    }

    const size_t written = prefs.putString(key, value);
    prefs.end();

    return written > 0;
}

} // namespace

namespace settings {

bool loadSsid(char* out, size_t outLen) {
    return loadString(kSsidKey, out, outLen);
}

bool loadPass(char* out, size_t outLen) {
    return loadString(kPassKey, out, outLen);
}

bool loadStation(char* out, size_t outLen) {
    return loadString(kStationKey, out, outLen);
}

bool saveSsid(const char* ssid) {
    return saveString(kSsidKey, ssid);
}

bool savePass(const char* pass) {
    return saveString(kPassKey, pass);
}

bool saveStation(const char* station) {
    return saveString(kStationKey, station);
}

bool clearAll() {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
        return false;
    }

    const bool ok = prefs.clear();
    prefs.end();

    return ok;
}

} // namespace settings
