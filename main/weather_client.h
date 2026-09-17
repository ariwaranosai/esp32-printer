#pragma once
#include "frame.h"
#include <string>
// Cache is retained through deep sleep, and invalidated when the location changes.
frame::Weather cached_weather(const std::string &adcode);
bool fetch_weather(const std::string &adcode, frame::Weather &out);
