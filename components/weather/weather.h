#pragma once
#include "frame.h"
#include <cstddef>
#include <string>

namespace weather {
// Does not modify out unless a complete, usable response was parsed.
bool parse(const char *json, size_t length, frame::Weather &out, time_t fetched_at = 0);
bool valid_adcode(const std::string &value);
std::string url(const std::string &adcode);
}
