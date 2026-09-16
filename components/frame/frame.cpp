#include "frame.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include "font_data.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace frame {
bool utc_epoch(const std::tm &date, time_t &result) {
    const int year = date.tm_year + 1900;
    if (year < 2024 || year > 2099 || date.tm_mon < 0 || date.tm_mon > 11 || date.tm_hour < 0 ||
        date.tm_hour > 23 || date.tm_min < 0 || date.tm_min > 59 || date.tm_sec < 0 ||
        date.tm_sec > 59)
        return false;
    auto leap = [](int y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); };
    const int months[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (date.tm_mday < 1 || date.tm_mday > months[date.tm_mon] + (date.tm_mon == 1 && leap(year)))
        return false;
    int64_t days = 0;
    for (int y = 1970; y < year; ++y)
        days += 365 + leap(y);
    for (int m = 0; m < date.tm_mon; ++m)
        days += months[m] + (m == 1 && leap(year));
    days += date.tm_mday - 1;
    result = days * 86400 + date.tm_hour * 3600 + date.tm_min * 60 + date.tm_sec;
    return true;
}

bool valid_dimensions(int w, int h) {
    return w > 0 && h > 0 && w <= 4096 && h <= 4096 && int64_t(w) * h <= 2000000;
}
Placement placement(int w, int h, Fit fit) {
    if (w <= 0 || h <= 0)
        return {{0, 0, 0, 0}, {PX, PY, 0, 0}};
    Placement p{{0, 0, w, h}, {PX, PY, PW, PH}};
    if (fit == Fit::Cover) {
        if (int64_t(w) * PH > int64_t(h) * PW) {
            p.source.w = std::max(1, int(int64_t(h) * PW / PH));
            p.source.x = (w - p.source.w) / 2;
        } else {
            p.source.h = std::max(1, int(int64_t(w) * PH / PW));
            p.source.y = (h - p.source.h) / 2;
        }
    } else {
        if (int64_t(w) * PH > int64_t(h) * PW) {
            p.dest.h = std::max(1, int(int64_t(PW) * h / w));
            p.dest.y += (PH - p.dest.h) / 2;
        } else {
            p.dest.w = std::max(1, int(int64_t(PH) * w / h));
            p.dest.x += (PW - p.dest.w) / 2;
        }
    }
    return p;
}
void Canvas::clear() { std::fill(pixels.begin(), pixels.end(), 0x11); }
void Canvas::pixel(int x, int y, uint8_t c) {
    if (x < 0 || x >= W || y < 0 || y >= H)
        return;
    auto &b = pixels[(y * W + x) / 2];
    b = (x & 1) ? ((b & 0xf0) | (c & 15)) : ((b & 15) | (c << 4));
}
uint8_t Canvas::pixel(int x, int y) const {
    if (x < 0 || x >= W || y < 0 || y >= H)
        return White;
    auto b = pixels[(y * W + x) / 2];
    return (x & 1) ? (b & 15) : (b >> 4);
}
void Canvas::rect(int x, int y, int w, int h, uint8_t c) {
    for (int j = std::max(y, 0); j < std::min(y + h, H); ++j)
        for (int i = std::max(x, 0); i < std::min(x + w, W); ++i)
            pixel(i, j, c);
}
void Canvas::text(int x, int y, const char *t, int size) {
    while (*t) {
        uint32_t cp = static_cast<unsigned char>(*t++);
        if (cp >= 0xe0 && *t && t[1]) {
            cp = ((cp & 15) << 12) | ((uint8_t(t[0]) & 63) << 6) | (uint8_t(t[1]) & 63);
            t += 2;
        } else if (cp >= 0xc0 && *t) {
            cp = ((cp & 31) << 6) | (uint8_t(*t++) & 63);
        }
        const Glyph *found = nullptr;
        for (const auto &g : glyphs)
            if (g.cp == cp && g.size == size) {
                found = &g;
                break;
            }
        if (!found) {
            x += size / 2;
            continue;
        }
        const auto &g = *found;
        for (int j = 0; j < g.h; ++j)
            for (int i = 0; i < g.w; ++i) {
                int bit = j * g.w + i;
                if (font_bits[g.offset + bit / 8] & (128 >> (bit % 8)))
                    pixel(x + i, y + j, Black);
            }
        x += g.advance;
    }
}
void Canvas::photo(const Image &im, Fit fit) {
    if (!im.rgb || !valid_dimensions(im.w, im.h))
        return;
    const int ow = im.orientation >= 5 ? im.h : im.w, oh = im.orientation >= 5 ? im.w : im.h;
    const auto p = placement(ow, oh, fit);
    const auto r = p.dest, s = p.source;
    const int pal[6][3] = {{0, 0, 0},   {255, 255, 255}, {255, 255, 0},
                           {255, 0, 0}, {0, 0, 255},     {0, 160, 0}};
    const uint8_t code[6] = {Black, White, Yellow, Red, Blue, Green};
    auto sample = [&](int x, int y, int c) {
        int sx = x, sy = y;
        switch (im.orientation) {
        case 2:
            sx = im.w - 1 - x;
            break;
        case 3:
            sx = im.w - 1 - x;
            sy = im.h - 1 - y;
            break;
        case 4:
            sy = im.h - 1 - y;
            break;
        case 5:
            sx = y;
            sy = x;
            break;
        case 6:
            sx = y;
            sy = im.h - 1 - x;
            break;
        case 7:
            sx = im.w - 1 - y;
            sy = im.h - 1 - x;
            break;
        case 8:
            sx = im.w - 1 - y;
            sy = x;
            break;
        default:
            break;
        }
        return im.rgb[(sy * im.w + sx) * 3 + c];
    };
    std::vector<float> row((r.w + 2) * 3), next(row.size());
    for (int y = 0; y < r.h; ++y) {
#ifdef ESP_PLATFORM
        if (y % 16 == 0)
            vTaskDelay(1);
#endif
        for (int x = 0; x < r.w; ++x) {
            const double sx = s.x + std::clamp((x + .5) * s.w / r.w - .5, 0., double(s.w - 1));
            const double sy = s.y + std::clamp((y + .5) * s.h / r.h - .5, 0., double(s.h - 1));
            int x0 = int(sx), y0 = int(sy), x1 = std::min(x0 + 1, ow - 1),
                y1 = std::min(y0 + 1, oh - 1);
            double ax = sx - x0, ay = sy - y0;
            float value[3];
            for (int c = 0; c < 3; ++c) {
                double top = sample(x0, y0, c) * (1 - ax) + sample(x1, y0, c) * ax;
                double bot = sample(x0, y1, c) * (1 - ax) + sample(x1, y1, c) * ax;
                value[c] =
                    std::clamp(float(top * (1 - ay) + bot * ay) + row[(x + 1) * 3 + c], 0.f, 255.f);
            }
            int best = 0;
            float distance = 1e30f;
            for (int k = 0; k < 6; ++k) {
                float dist = 0;
                for (int c = 0; c < 3; ++c) {
                    float d = value[c] - pal[k][c];
                    dist += d * d;
                }
                if (dist < distance) {
                    distance = dist;
                    best = k;
                }
            }
            pixel(r.x + x, r.y + y, code[best]);
            for (int c = 0; c < 3; ++c) {
                float err = value[c] - pal[best][c];
                row[(x + 2) * 3 + c] += err * 7 / 16;
                next[x * 3 + c] += err * 3 / 16;
                next[(x + 1) * 3 + c] += err * 5 / 16;
                next[(x + 2) * 3 + c] += err / 16;
            }
        }
        row.swap(next);
        std::fill(next.begin(), next.end(), 0);
    }
}
void Canvas::ui(const State &s, bool photo_ok) {
    char b[80];
    text(24, 20, "温度", 18);
    if (s.sensor_valid)
        snprintf(b, sizeof b, "%.1f°", s.temperature);
    else
        snprintf(b, sizeof b, "--°");
    text(66, 18, b, 22);
    text(156, 20, "湿度", 18);
    if (s.sensor_valid)
        snprintf(b, sizeof b, "%.0f%%", s.humidity);
    else
        snprintf(b, sizeof b, "--%%");
    text(200, 18, b, 22);
    // Concentric Wi-Fi arcs. Slash means disconnected at sampling time.
    for (int radius : {14, 9, 4})
        for (int deg = 220; deg <= 320; ++deg) {
            double a = deg * 3.14159265 / 180;
            rect(336 + int(radius * cos(a)), 40 + int(radius * sin(a)), 2, 2);
        }
    rect(335, 39, 3, 3);
    if (!s.wifi)
        for (int i = 0; i < 26; ++i)
            rect(323 + i, 20 + i, 2, 2);
    rect(363, 23, 30, 2);
    rect(363, 37, 30, 2);
    rect(363, 23, 2, 16);
    rect(391, 23, 2, 16);
    rect(393, 28, 3, 6);
    if (s.battery >= 0) {
        rect(367, 27, std::clamp(s.battery, 0, 100) * 22 / 100, 8);
        snprintf(b, sizeof b, "%d%%", s.battery);
    } else
        snprintf(b, sizeof b, "--");
    text(403, 20, b, 18);
    rect(24, 60, 432, 2);
    rect(247, 82, 2, 63);
    rect(24, 164, 432, 2);
    if (s.time_valid) {
        strftime(b, sizeof b, "%H:%M", &s.local);
        text(24, 78, b, 65);
        strftime(b, sizeof b, "%Y / %m / %d", &s.local);
        text(272, 84, b, 23);
        const char *days[] = {"星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"};
        text(272, 120, days[std::clamp(s.local.tm_wday, 0, 6)], 23);
        strftime(b, sizeof b, "更新于 %H:%M", &s.local);
    } else {
        text(24, 78, "--:--", 65);
        text(272, 84, "等待校时", 23);
        text(272, 120, "请配置网络", 23);
        snprintf(b, sizeof b, "时间未同步");
    }
    text(24, 777, b, 14);
    text(371, 777, "室内环境", 14);
    if (!photo_ok) {
        text(146, 445, "请放入照片", 23);
        text(122, 483, "SD / photos", 23);
    }
}
std::vector<uint8_t> Canvas::panel(bool flip) const {
    std::vector<uint8_t> out(W * H / 2, 0x11);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            int nx = H - 1 - y, ny = x;
            if (flip) {
                nx = 799 - nx;
                ny = 479 - ny;
            }
            int at = ny * 800 + nx;
            auto c = pixel(x, y);
            out[at / 2] = (at & 1) ? ((out[at / 2] & 0xf0) | c) : ((out[at / 2] & 15) | (c << 4));
        }
    return out;
}
} // namespace frame
