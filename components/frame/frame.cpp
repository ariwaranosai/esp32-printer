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
static uint32_t next_codepoint(const char *&t) {
    uint32_t cp = static_cast<unsigned char>(*t++);
    int count = cp >= 0xf0 && cp <= 0xf4 ? 3 : cp >= 0xe0 ? 2 : cp >= 0xc2 ? 1 : 0;
    if (cp < 128) return cp;
    if (!count || cp > 0xf4) return '?';
    cp &= (1u << (6 - count)) - 1;
    for (int i = 0; i < count; ++i) {
        if ((static_cast<unsigned char>(*t) & 0xc0) != 0x80) return '?';
        cp = (cp << 6) | (static_cast<unsigned char>(*t++) & 63);
    }
    return cp;
}
static const Glyph *glyph(uint32_t cp, int size) {
    const auto end = std::end(glyphs);
    auto found = std::lower_bound(std::begin(glyphs), end, std::pair<int, uint32_t>{size, cp},
        [](const Glyph &g, const std::pair<int, uint32_t> &key) {
            return g.size < key.first || (g.size == key.first && g.cp < key.second);
        });
    if (found != end && found->cp == cp && found->size == size) return found;
    return cp == '?' ? nullptr : glyph('?', size);
}
void Canvas::text_fit(int x, int y, const char *t, int size, int width, bool right) {
    std::string fitted;
    int used = 0;
    while (*t) {
        const char *start = t;
        const auto *g = glyph(next_codepoint(t), size);
        if (!g) continue;
        if (used + g->advance > width) break;
        fitted.append(start, t - start);
        used += g->advance;
    }
    text(right ? x + width - used : x, y, fitted.c_str(), size);
}
void Canvas::text(int x, int y, const char *t, int size) {
    while (*t) {
        const Glyph *found = glyph(next_codepoint(t), size);
        if (!found) continue;
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
    char b[160];
    const auto &w = s.weather;
    snprintf(b, sizeof b, "%s%s", w.valid ? w.city : "", w.valid ? " · 户外天气" : "户外天气");
    text_fit(24, 22, b, 18, 330);
    for (int radius : {14, 9, 4})
        for (int deg = 220; deg <= 320; ++deg) {
            double a = deg * 3.14159265 / 180;
            rect(387 + int(radius * cos(a)), 39 + int(radius * sin(a)), 2, 2);
        }
    rect(386, 37, 3, 3);
    if (!s.wifi)
        for (int i = 0; i < 25; ++i) rect(375 + i, 19 + i, 2, 2);
    rect(422, 24, 30, 2); rect(422, 36, 30, 2);
    rect(422, 24, 2, 14); rect(450, 24, 2, 14); rect(452, 28, 3, 6);
    if (s.battery >= 0)
        rect(426, 28, std::clamp(s.battery, 0, 100) * 22 / 100, 6);
    else
        text(431, 24, "?", 16);

    const bool sun = w.icon == 100 || w.icon == 101 || w.icon == 102 || w.icon == 103;
    const bool night = w.icon >= 150 && w.icon <= 153;
    const bool rain = w.icon >= 300 && w.icon < 400;
    const bool snow = w.icon >= 400 && w.icon < 500;
    const bool fog = w.icon >= 500 && w.icon < 600;
    const bool cloud = (w.icon >= 101 && w.icon <= 104) || (night && w.icon != 150) || rain || snow;
    auto circle = [&](int cx, int cy, int r, uint8_t fill) {
        for (int y = -r; y <= r; ++y)
            for (int x = -r; x <= r; ++x)
                if (x*x + y*y <= r*r) pixel(cx+x, cy+y, fill);
    };
    if (w.valid && (sun || night)) {
        circle(52, 69, 16, Black); circle(52, 69, 13, Yellow);
        if (night) circle(59, 63, 12, White);
        else for (int d = 0; d < 360; d += 45) {
            double a = d * 3.14159265 / 180;
            for (int r = 21; r <= 26; ++r)
                rect(52 + int(r*cos(a)), 69 + int(r*sin(a)), 2, 2);
        }
    }
    if (w.valid && cloud) {
        auto inside = [](int x, int y) {
            return ((x-39)*(x-39)+(y-81)*(y-81) <= 11*11) ||
                   ((x-52)*(x-52)+(y-76)*(y-76) <= 14*14) ||
                   ((x-69)*(x-69)+(y-82)*(y-82) <= 10*10) ||
                   (x >= 38 && x <= 69 && y >= 78 && y <= 91);
        };
        for (int y = 60; y <= 93; ++y)
            for (int x = 25; x <= 81; ++x)
                if (inside(x,y)) pixel(x,y, inside(x-2,y) && inside(x+2,y) &&
                    inside(x,y-2) && inside(x,y+2) ? White : Black);
        if (rain) for (int x : {38, 53, 68})
            for (int j = 0; j < 7; ++j) rect(x-j/2, 96+j, 2, 1, Blue);
        if (snow) for (int x : {38, 53, 68}) {
            rect(x-3, 98, 7, 2); rect(x, 95, 2, 8);
        }
    }
    if (w.valid && fog) for (int y : {65, 77, 89}) rect(28, y, 49, 2);
    if (!w.valid || !(sun || night || cloud || fog)) text(42, 59, "?", 32);
    text_fit(94, 58, w.valid ? w.description : "暂无天气", 32, 195);
    if (w.valid) snprintf(b, sizeof b, "%.0f°C", w.temperature);
    else snprintf(b, sizeof b, "--°C");
    int size = w.valid && (w.temperature < -9.5 || w.temperature >= 99.5) ? 32 : 46;
    text_fit(295, 51, b, size, 161, true);
    text_fit(24, 107, w.valid && w.wind[0] ? w.wind : "风力暂无数据", 16, 216);
    if (w.valid && w.stale) {
        if (w.report_time[0]) snprintf(b, sizeof b, "未更新 %.5s %.5s", w.report_time+5, w.report_time+11);
        else snprintf(b, sizeof b, "未更新 · 时间未知");
    } else if (w.valid && w.report_time[0]) {
        char today[11]{};
        if (s.time_valid) strftime(today, sizeof today, "%Y-%m-%d", &s.local);
        if (strncmp(today, w.report_time, 10) == 0)
            snprintf(b, sizeof b, "气象 %.5s 更新", w.report_time+11);
        else snprintf(b, sizeof b, "气象 %.5s %.5s", w.report_time+5, w.report_time+11);
    } else snprintf(b, sizeof b, "%s", w.valid ? "气象时间未知" : "天气获取失败");
    text_fit(244, 107, b, 16, 212, true);

    text(24, 738, "室内", 18);
    if (s.sensor_valid) snprintf(b, sizeof b, "%.1f°C", s.temperature);
    else snprintf(b, sizeof b, "--°C");
    text_fit(77, 735, b, 25, 118);
    text(204, 738, "湿度", 18);
    if (s.sensor_valid) snprintf(b, sizeof b, "%.0f%%", s.humidity);
    else snprintf(b, sizeof b, "--%%");
    text_fit(250, 735, b, 25, 112);
    if (s.time_valid) strftime(b, sizeof b, "采样于 %H:%M", &s.local);
    else snprintf(b, sizeof b, "等待校时");
    text(24, 773, b, 16);
    if (s.refresh_seconds == 3600) snprintf(b, sizeof b, "每小时更新");
    else if (s.refresh_seconds % 3600 == 0) snprintf(b, sizeof b, "每%d小时更新", s.refresh_seconds/3600);
    else snprintf(b, sizeof b, "每%d分钟更新", s.refresh_seconds/60);
    text_fit(252, 773, b, 16, 204, true);
    if (!photo_ok) {
        text(146, 410, "请放入照片", 18);
        text(146, 449, "SD / photos", 18);
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
