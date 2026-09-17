#include "weather.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <fstream>
#include <iterator>

int main(int argc, char **argv) {
    frame::Weather w;
    auto parse = [&](const char *s) { return weather::parse(s, strlen(s), w); };
    assert(parse(R"({"city":"杭州","weather":"多云","temperature":26.5,"weather_icon":"101","wind_direction":"东北风","wind_power":"2级","report_time":"2026-09-18 08:20:00"})"));
    assert(w.valid && !w.stale && w.temperature == 26.5f && w.icon == 101);
    assert(!strcmp(w.wind, "东北风 2级"));
    assert(!strcmp(w.report_time, "2026-09-18 08:20:00"));
    // Failures must leave a caller's cached reading untouched.
    assert(!parse("{broken"));
    assert(!parse(R"({"code":500,"message":"error"})"));
    assert(!parse(R"({"city":"杭州","weather":"晴","temperature":null})"));
    assert(!parse(R"({"city":"杭州","weather":"晴","temperature":999})"));
    assert(!parse(R"({"city":"杭州","weather":"晴","temperature":12} junk)"));
    assert(w.temperature == 26.5f && w.icon == 101);
    assert(parse(R"({"city":"北京","weather":"晴","temperature":"-5.5","report_time":"2026-02-30 08:20:00"})"));
    assert(w.temperature == -5.5f && w.icon == -1 && !w.report_time[0]);
    assert(!parse(R"({"city":"北京","weather":"晴","temperature":"23C"})"));
    assert(!parse(R"({"city":"北京","weather":"晴","temperature":"nan"})"));
    assert(weather::valid_adcode(""));
    assert(weather::valid_adcode("110000"));
    assert(!weather::valid_adcode("11000"));
    assert(!weather::valid_adcode("11&000"));
    assert(weather::url("") == "https://uapis.cn/api/v1/misc/weather?lang=zh");
    assert(weather::url("110000").find("&adcode=110000") != std::string::npos);
    assert(weather::url("&city=abc") == weather::url(""));
    const char *relative = R"({"city":"杭州市","weather":"多云","temperature":24,"report_time":"6 分钟前发布"})";
    assert(weather::parse(relative, strlen(relative), w, 1789653600));
    const time_t expected = 1789653600 - 360;
    char report[20];
    strftime(report, sizeof report, "%Y-%m-%d %H:%M:%S", std::localtime(&expected));
    assert(!strcmp(w.report_time, report));
    if (argc == 2) {
        std::ifstream file(argv[1]);
        std::string body((std::istreambuf_iterator<char>(file)), {});
        assert(weather::parse(body.data(), body.size(), w, time(nullptr)));
        assert(w.valid && w.report_time[0]);
        std::cout << "Live response parsed with report time " << w.report_time << '\n';
    }
    std::cout << "weather parsing, error/cache preservation and location selection passed\n";
}
