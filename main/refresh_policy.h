#pragma once
#include <ctime>

namespace refresh_policy {
struct QuietHours {
    bool enabled = true;
    int start = 0;
    int end = 7;
};
inline bool quiet(std::time_t now, const QuietHours &hours) {
    if (!hours.enabled || hours.start == hours.end || now < 1704067200) return false;
    std::tm local{};
    if (!localtime_r(&now, &local)) return false;
    const int hour = local.tm_hour;
    return hours.start < hours.end ? hour >= hours.start && hour < hours.end
                                  : hour >= hours.start || hour < hours.end;
}
inline std::time_t quiet_end(std::time_t now, const QuietHours &hours) {
    std::tm local{};
    if (!localtime_r(&now, &local)) return now;
    if (hours.start > hours.end && local.tm_hour >= hours.start) ++local.tm_mday;
    local.tm_hour = hours.end;
    local.tm_min = local.tm_sec = 0;
    local.tm_isdst = -1;
    const auto end = std::mktime(&local);
    return end > now ? end : now;
}
inline bool skip_refresh(std::time_t now, const QuietHours &hours, bool force_photo) {
    return !force_photo && quiet(now, hours);
}
// Use local calendar time for the end of quiet hours, including DST transitions.
inline std::time_t next_wake(std::time_t now, int interval, const QuietHours &hours) {
    if (quiet(now, hours)) return quiet_end(now, hours);
    const auto target = now + interval;
    return quiet(target, hours) ? quiet_end(target, hours) : target;
}
} // namespace refresh_policy
