#pragma once
#include <cstdint>
#include <string>

namespace remote_photo {
inline bool valid_settings(const std::string &url, const std::string &token) {
    if (url.empty()) return false;
    const size_t start = url.rfind("http://", 0) == 0 ? 7 :
                         url.rfind("https://", 0) == 0 ? 8 : 0;
    if (!start || url.size() > 512 || token.size() < 24 || token.size() > 256) return false;
    const auto end = url.find('/', start);
    const auto authority = url.substr(start, end - start);
    if (authority.empty() || authority.find('@') != std::string::npos) return false;
    for (unsigned char c : url) if (c <= 32 || c == 127 || c == '#' || c == '\\') return false;
    for (unsigned char c : token) if (c <= 32 || c >= 127) return false;
    return true;
}
inline bool due(bool cached, int64_t fetched, int64_t now, int interval, bool force) {
    return force || !cached || fetched < 1704067200 || now < fetched || now - fetched >= interval;
}
inline uint64_t source_id(const std::string &url) {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : url) { hash ^= c; hash *= 1099511628211ULL; }
    return hash;
}
} // namespace remote_photo
