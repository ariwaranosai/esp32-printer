#include "refresh_policy.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>

static std::time_t local(int year, int month, int day, int hour, int minute = 0, int second = 0) {
    std::tm t{};
    t.tm_year = year - 1900; t.tm_mon = month - 1; t.tm_mday = day;
    t.tm_hour = hour; t.tm_min = minute; t.tm_sec = second; t.tm_isdst = -1;
    return std::mktime(&t);
}
int main() {
    using namespace refresh_policy;
    setenv("TZ", "CST-8", 1); tzset();
    QuietHours hours;
    auto at = [](int hour, int minute = 0, int second = 0) {
        return local(2026, 9, 19, hour, minute, second);
    };
    assert(!quiet(at(23,59,59), hours));
    assert(quiet(at(0), hours));
    assert(quiet(at(6,59,59), hours));
    assert(!quiet(at(7), hours));
    assert(skip_refresh(at(2), hours, false));
    assert(!skip_refresh(at(2), hours, true)); // Nighttime manual refresh remains usable.
    assert(next_wake(at(2), 10800, hours) == at(7));
    assert(next_wake(at(6,59,59), 10800, hours) == at(7));
    assert(next_wake(at(7), 10800, hours) == at(10));
    assert(next_wake(at(20), 10800, hours) == at(23));
    assert(next_wake(at(21), 10800, hours) == local(2026,9,20,7));
    assert(next_wake(local(2026,12,31,23), 10800, hours) == local(2027,1,1,7));
    assert(!quiet(0, hours)); // Unset clock must be allowed to sync.
    assert(next_wake(0, 10800, hours) == 10800);
    hours.enabled = false;
    assert(!quiet(at(2), hours));
    assert(next_wake(at(2),10800,hours) == at(5));
    hours = {true, 23, 7};
    assert(quiet(at(23), hours));
    assert(next_wake(at(23),10800,hours) == local(2026,9,20,7));
    assert(next_wake(at(2),10800,hours) == at(7));
    hours = {true, 12, 14};
    assert(next_wake(at(12),10800,hours) == at(14));
    hours = {true, 7, 7};
    assert(!quiet(at(7), hours));
    // End-of-night uses civil time, not a fixed number of seconds per day.
    setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0/2", 1); tzset();
    hours = {true, 23, 7};
    auto before = local(2026,3,7,23);
    auto after = next_wake(before,10800,hours);
    assert(after == local(2026,3,8,7) && after-before == 7*3600);
    before = local(2026,10,31,23);
    after = next_wake(before,10800,hours);
    assert(after == local(2026,11,1,7) && after-before == 9*3600);
    puts("Refresh scheduling: boundaries, manual override, invalid clock and DST passed");
}
