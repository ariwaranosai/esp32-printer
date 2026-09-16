#include "frame.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#define STBI_MALLOC(n) heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define STBI_REALLOC(p, n) heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define STBI_FREE(p) heap_caps_free(p)
#endif
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS 4096
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
namespace frame {
// Read JPEG EXIF orientation without trusting TIFF offsets or allocating the image.
static int orientation(FILE *f) {
    if (fgetc(f) != 0xff || fgetc(f) != 0xd8) {
        rewind(f);
        return 1;
    }
    int result = 1;
    while (true) {
        if (fgetc(f) != 0xff)
            break;
        int marker;
        do {
            marker = fgetc(f);
        } while (marker == 0xff);
        if (marker < 0 || marker == 0xda || marker == 0xd9)
            break;
        if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7))
            continue;
        int hi = fgetc(f), lo = fgetc(f);
        if (hi < 0 || lo < 0)
            break;
        int len = hi * 256 + lo - 2;
        if (len < 0)
            break;
        if (marker != 0xe1) {
            if (fseek(f, len, SEEK_CUR) != 0)
                break;
            continue;
        }
        std::vector<uint8_t> b(len);
        if (fread(b.data(), 1, len, f) != size_t(len))
            break;
        if (len < 14 || memcmp(b.data(), "Exif\0\0", 6) != 0)
            continue;
        const uint8_t *t = b.data() + 6;
        size_t n = b.size() - 6;
        bool little = t[0] == 'I' && t[1] == 'I';
        if (!little && !(t[0] == 'M' && t[1] == 'M'))
            continue;
        auto u16 = [&](size_t p) -> uint16_t {
            return little ? uint16_t(t[p] | (t[p + 1] << 8)) : uint16_t((t[p] << 8) | t[p + 1]);
        };
        auto u32 = [&](size_t p) -> uint32_t {
            return little ? uint32_t(t[p]) | (uint32_t(t[p + 1]) << 8) |
                                (uint32_t(t[p + 2]) << 16) | (uint32_t(t[p + 3]) << 24)
                          : (uint32_t(t[p]) << 24) | (uint32_t(t[p + 1]) << 16) |
                                (uint32_t(t[p + 2]) << 8) | t[p + 3];
        };
        if (u16(2) != 42)
            continue;
        size_t off = u32(4);
        if (off > n - 2)
            continue;
        size_t count = u16(off);
        off += 2;
        for (size_t i = 0; i < count && off <= n && n - off >= 12; ++i, off += 12) {
            if (u16(off) == 0x112 && u16(off + 2) == 3 && u32(off + 4) == 1) {
                int v = u16(off + 8);
                if (v >= 1 && v <= 8)
                    result = v;
                break;
            }
        }
        break;
    }
    rewind(f);
    return result;
}
Image::~Image() { stbi_image_free(rgb); }
bool load_image(const std::string &path, Image &out, std::string &error) {
    stbi_image_free(out.rgb);
    out.rgb = nullptr;
    out.w = out.h = 0;
    out.orientation = 1;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) {
        error = "Cannot open photo";
        return false;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        error = "Cannot seek photo";
        return false;
    }
    long length = ftell(f);
    rewind(f);
    if (length <= 0 || length > 8 * 1024 * 1024) {
        fclose(f);
        error = "Photo file exceeds 8 MiB or is empty";
        return false;
    }
    out.orientation = orientation(f);
    int w = 0, h = 0, n = 0;
    if (!stbi_info_from_file(f, &w, &h, &n) || !valid_dimensions(w, h)) {
        fclose(f);
        error = "Invalid image or exceeds 4096px / 2MP; run prepare_photo.py";
        return false;
    }
    rewind(f);
    int decoded_w = 0, decoded_h = 0;
    const int channels = (n == 2 || n == 4) ? 4 : 3;
    auto *data = stbi_load_from_file(f, &decoded_w, &decoded_h, &n, channels);
    fclose(f);
    if (!data) {
        error = stbi_failure_reason() ? stbi_failure_reason() : "Decode allocation failure";
        return false;
    }
    if (decoded_w != w || decoded_h != h) {
        stbi_image_free(data);
        error = "Image dimensions changed during decoding";
        return false;
    }
    // Compact RGBA in place to RGB, flatten transparency onto the white display.
    if (channels == 4)
        for (int i = 0; i < w * h; ++i) {
            int a = data[4 * i + 3];
            for (int c = 0; c < 3; ++c)
                data[3 * i + c] = (data[4 * i + c] * a + 255 * (255 - a) + 127) / 255;
        }
    out.rgb = data;
    out.w = w;
    out.h = h;
    error.clear();
    return true;
}
} // namespace frame
