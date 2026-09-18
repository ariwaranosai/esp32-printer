#pragma once
#include <cstdint>
namespace power_policy {
inline uint8_t peripheral_ldos(uint8_t enabled, bool screen_on) {
    return (enabled & ~0x0c) | 0x04 | (screen_on ? 0x08 : 0);
}
inline bool ntp_due(int64_t now, int64_t synced) {
    return now < 1704067200 || synced < 1704067200 || now < synced || now-synced >= 86400;
}
}
