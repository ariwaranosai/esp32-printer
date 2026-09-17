#include "weather.h"
#include "cJSON.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace weather {
static bool string_field(const cJSON *root, const char *key, char *out, size_t capacity) {
    const auto *v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(v) || !v->valuestring[0])
        return false;
    const auto length = strlen(v->valuestring);
    if (length >= capacity)
        return false;
    // Reject control bytes. Text rendering handles unknown Unicode safely.
    for (size_t i = 0; i < length; ++i)
        if (static_cast<unsigned char>(v->valuestring[i]) < 32)
            return false;
    memcpy(out, v->valuestring, length + 1);
    return true;
}
static bool number_field(const cJSON *root, const char *key, double &out) {
    const auto *v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsNumber(v))
        out = v->valuedouble;
    else if (cJSON_IsString(v) && v->valuestring[0]) {
        char *end;
        out = strtod(v->valuestring, &end);
        if (*end || end == v->valuestring)
            return false;
    } else
        return false;
    return std::isfinite(out);
}
bool parse(const char *json, size_t length, frame::Weather &out, time_t fetched_at) {
    if (!json || !length || length > 16384 || memchr(json, 0, length))
        return false;
    // Require the entire body to be JSON, not a valid prefix followed by junk.
    std::string body(json, length);
    cJSON *root = cJSON_ParseWithLengthOpts(body.c_str(), body.size() + 1, nullptr, true);
    frame::Weather value;
    double temperature;
    bool ok = cJSON_IsObject(root) &&
              string_field(root, "city", value.city, sizeof value.city) &&
              string_field(root, "weather", value.description, sizeof value.description) &&
              number_field(root, "temperature", temperature) && temperature >= -100 && temperature <= 70;
    if (ok) {
        value.temperature = static_cast<float>(temperature);
        double icon;
        if (number_field(root, "weather_icon", icon) && icon >= 0 && icon <= 999 && floor(icon) == icon)
            value.icon = static_cast<int>(icon);
        char direction[49]{}, power[33]{};
        string_field(root, "wind_direction", direction, sizeof direction);
        string_field(root, "wind_power", power, sizeof power);
        snprintf(value.wind, sizeof value.wind, "%s%s%s", direction,
                 direction[0] && power[0] ? " " : "", power);
        char report[40]{};
        if (string_field(root, "report_time", report, sizeof report)) {
            int year, month, day, hour, minute, second;
            if (sscanf(report, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) == 6) {
                std::tm t{};
                t.tm_year = year - 1900; t.tm_mon = month - 1; t.tm_mday = day;
                t.tm_hour = hour; t.tm_min = minute; t.tm_sec = second;
                time_t epoch;
                if (frame::utc_epoch(t, epoch))
                    snprintf(value.report_time, sizeof value.report_time,
                             "%04d-%02d-%02d %02d:%02d:%02d", year, month, day, hour, minute, second);
            }
            if (!value.report_time[0] && fetched_at >= 1704067200) {
                std::string relative;
                for (char c : std::string(report)) if (c != ' ') relative += c;
                char *end;
                long amount = strtol(relative.c_str(), &end, 10);
                long seconds = -1;
                if (relative == "刚刚" || relative == "刚刚发布") seconds = 0;
                else if (end != relative.c_str() && amount >= 0 && amount <= 1440) {
                    if (!strcmp(end, "分钟前") || !strcmp(end, "分钟前发布")) seconds = amount * 60;
                    if ((!strcmp(end, "小时前") || !strcmp(end, "小时前发布")) && amount <= 24)
                        seconds = amount * 3600;
                }
                if (seconds >= 0) {
                    time_t report_at = fetched_at - seconds;
                    const auto *local = std::localtime(&report_at);
                    if (local) strftime(value.report_time, sizeof value.report_time, "%Y-%m-%d %H:%M:%S", local);
                }
            }
        }
        value.valid = true;
        out = value;
    }
    cJSON_Delete(root);
    return ok;
}
bool valid_adcode(const std::string &value) {
    if (value.empty()) return true;
    if (value.size() != 6) return false;
    for (char c : value) if (c < '0' || c > '9') return false;
    return true;
}
std::string url(const std::string &adcode) {
    std::string result = "https://uapis.cn/api/v1/misc/weather?lang=zh";
    if (!adcode.empty() && valid_adcode(adcode)) result += "&adcode=" + adcode;
    return result;
}
}
