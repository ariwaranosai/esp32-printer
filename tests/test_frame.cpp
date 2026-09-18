#include "frame.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
using namespace frame;
static void ppm(const Canvas &c, const char *path) {
    const unsigned char pal[7][3] = {{0, 0, 0},       {255, 255, 255}, {255, 255, 0}, {255, 0, 0},
                                     {255, 255, 255}, {0, 0, 255},     {0, 160, 0}};
    std::ofstream o(path, std::ios::binary);
    o << "P6\n480 800\n255\n";
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            o.write((const char *)pal[c.pixel(x, y)], 3);
}
int main(int argc, char **argv) {
    if (argc == 5) {
        Image im;
        std::string error;
        bool ok = load_image(argv[1], im, error);
        if (!ok) {
            fprintf(stderr, "%s\n", error.c_str());
            return 2;
        }
        Canvas c;
        c.photo(im, std::string(argv[3]) == "contain" ? Fit::Contain : Fit::Cover);
        if (std::string(argv[4]) != "none") {
            State s;
            s.sensor_valid = s.wifi = s.time_valid = true;
            s.temperature = 24.6;
            s.humidity = 58;
            s.battery = 82;
            if (std::string(argv[4]) == "battery100") s.battery = 100;
            if (std::string(argv[4]) == "battery0") s.battery = 0;
            if (std::string(argv[4]) == "batteryunknown") s.battery = -1;
            s.local.tm_year = 126;
            s.local.tm_mon = 8;
            s.local.tm_mday = 16;
            s.local.tm_wday = 3;
            s.local.tm_hour = 14;
            s.local.tm_min = 30;
            s.refresh_seconds = 10800;
            s.weather.valid = std::string(argv[4]) != "missing";
            s.weather.stale = std::string(argv[4]) == "stale";
            s.weather.temperature = 26;
            s.weather.icon = 101;
            snprintf(s.weather.city, sizeof s.weather.city, "杭州");
            snprintf(s.weather.description, sizeof s.weather.description, "多云");
            snprintf(s.weather.wind, sizeof s.weather.wind, "东北风 2级");
            snprintf(s.weather.report_time, sizeof s.weather.report_time, "2026-09-16 14:20:00");
            if (std::string(argv[4]) == "long") {
                snprintf(s.weather.city, sizeof s.weather.city, "阿拉善左旗特别长的城市名称");
                snprintf(s.weather.description, sizeof s.weather.description, "雷阵雨伴有冰雹");
                s.weather.temperature = -32;
                s.weather.icon = 302;
            }
            c.ui(s, true);
        }
        ppm(c, argv[2]);
        return 0;
    }
    std::tm date{};
    date.tm_year = 124;
    date.tm_mon = 1;
    date.tm_mday = 29;
    time_t epoch = 0;
    assert(utc_epoch(date, epoch) && epoch == 1709164800);
    date.tm_year = 125;
    assert(!utc_epoch(date, epoch));
    date.tm_year = 126;
    date.tm_mon = 8;
    date.tm_mday = 16;
    date.tm_hour = 6;
    date.tm_min = 30;
    assert(utc_epoch(date, epoch) && epoch == 1789540200);
    assert(!valid_dimensions(0, 100));
    assert(!valid_dimensions(-1, 1));
    assert(!valid_dimensions(4097, 1));
    assert(!valid_dimensions(2000, 2000));
    assert(valid_dimensions(1350, 1350));
    for (auto dim : {std::pair<int, int>{800, 480},
                     {480, 800},
                     {432, 576},
                     {431, 575},
                     {1, 1},
                     {4096, 1},
                     {1, 4096},
                     {1350, 1350}}) {
        for (auto fit : {Fit::Cover, Fit::Contain}) {
            auto p = placement(dim.first, dim.second, fit);
            assert(p.source.w > 0 && p.source.h > 0);
            assert(p.source.x >= 0 && p.source.y >= 0);
            assert(p.source.x + p.source.w <= dim.first && p.source.y + p.source.h <= dim.second);
            assert(p.dest.x >= PX && p.dest.y >= PY && p.dest.x + p.dest.w <= PX + PW &&
                   p.dest.y + p.dest.h <= PY + PH);
        }
    }
    auto p = placement(800, 480, Fit::Cover);
    assert(p.source.x == 233 && p.source.w == 333 && p.source.h == 480);
    p = placement(800, 480, Fit::Contain);
    assert(p.dest.w == 456 && p.dest.h == 273 && p.dest.y == PY + (PH-273)/2);
    Canvas c;
    c.pixel(-1, 0, Black);
    c.pixel(480, 0, Black);
    c.pixel(0, 800, Black);
    assert(c.pixel(0, 0) == White);
    c.pixel(0, 0, Red);
    c.pixel(479, 0, Blue);
    c.pixel(0, 799, Green);
    c.pixel(479, 799, Yellow);
    for (bool flip : {false, true}) {
        auto out = c.panel(flip);
        assert(out.size() == 192000);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                int nx = 799 - y, ny = x;
                if (flip) {
                    nx = 799 - nx;
                    ny = 479 - ny;
                }
                int at = ny * 800 + nx;
                int v = (at & 1) ? (out[at / 2] & 15) : (out[at / 2] >> 4);
                assert(v == c.pixel(x, y));
            }
    }
    Image im;
    im.w = 3;
    im.h = 2;
    im.rgb = (uint8_t *)malloc(18);
    for (int i = 0; i < 18; ++i)
        im.rgb[i] = i * 13;
    for (int orientation = 1; orientation <= 8; ++orientation)
        for (auto fit : {Fit::Cover, Fit::Contain}) {
            im.orientation = orientation;
            c.clear();
            c.photo(im, fit);
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                    if (x < PX || x >= PX + PW || y < PY || y >= PY + PH)
                        assert(c.pixel(x, y) == White);
        }
    // Header/footer must not overwrite the photo, even with long network text.
    c.clear();
    c.rect(PX, PY, PW, PH, Red);
    State state;
    state.weather.valid = true;
    snprintf(state.weather.city, sizeof state.weather.city, "阿拉善左旗特别长的城市名称");
    snprintf(state.weather.description, sizeof state.weather.description, "雷阵雨伴有冰雹");
    state.weather.temperature = -99;
    for (int battery : {-1, 0, 1, 82, 100, 101}) {
        state.battery = battery;
        c.ui(state, true);
        for (int y = PY; y < PY+PH; ++y)
            for (int x = PX; x < PX+PW; ++x) assert(c.pixel(x,y) == Red);
        // Keep the outer margin clear for every percentage width, including 100%.
        for (int y = 0; y < 45; ++y)
            for (int x = 456; x < W; ++x) assert(c.pixel(x,y) == White);
    }
    puts("geometry, bounds, EXIF sampling and 192000-byte panel rotation passed");
}
