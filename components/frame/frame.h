#pragma once
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>
namespace frame {
constexpr int W = 480, H = 800, PX = 24, PY = 192, PW = 432, PH = 576;
enum Color : uint8_t { Black = 0, White = 1, Yellow = 2, Red = 3, Blue = 5, Green = 6 };
enum class Fit { Cover, Contain };
struct Rect {
    int x, y, w, h;
};
struct Placement {
    Rect source, dest;
};
Placement placement(int w, int h, Fit fit);
bool valid_dimensions(int w, int h);
bool utc_epoch(const std::tm &date, time_t &result);
struct Image {
    int w = 0, h = 0, orientation = 1;
    uint8_t *rgb = nullptr;
    ~Image();
    Image() = default;
    Image(const Image &) = delete;
    Image &operator=(const Image &) = delete;
};
bool load_image(const std::string &path, Image &out, std::string &error);
struct State {
    float temperature = 0, humidity = 0;
    bool sensor_valid = false, wifi = false, time_valid = false;
    int battery = -1;
    std::tm local{};
};
class Canvas {
  public:
    std::vector<uint8_t> pixels;
    Canvas() : pixels(W * H / 2, 0x11) {}
    void clear();
    void pixel(int x, int y, uint8_t color);
    uint8_t pixel(int x, int y) const;
    void rect(int x, int y, int w, int h, uint8_t color = Black);
    void text(int x, int y, const char *text, int size);
    void photo(const Image &, Fit);
    void ui(const State &, bool photo_ok);
    std::vector<uint8_t> panel(bool flip = false) const;
};
} // namespace frame
