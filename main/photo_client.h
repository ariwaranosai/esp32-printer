#pragma once
#include <string>

// Two SD slots keep the last verified image intact until the new file is decoded and committed.
std::string cached_remote_photo(const std::string &url);
bool refresh_remote_photo(const std::string &url, const std::string &token,
                          int interval, bool force);
