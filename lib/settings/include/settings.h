#pragma once

#include <stddef.h>

namespace settings {

constexpr size_t kSsidMaxLen = 64;
constexpr size_t kPassMaxLen = 64;
constexpr size_t kStationMaxLen = 256;

bool loadSsid(char* out, size_t outLen);
bool loadPass(char* out, size_t outLen);
bool loadStation(char* out, size_t outLen);

bool saveSsid(const char* ssid);
bool savePass(const char* pass);
bool saveStation(const char* station);

bool clearAll();

} // namespace settings
