#include "photo_cache.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace frame {
namespace {
constexpr uint64_t BASIS = 14695981039346656037ULL;
// Bump magic when palette, dithering algorithm, or photo geometry changes.
constexpr uint64_t MAGIC = 0x5048433435360004ULL;
constexpr size_t PHOTO_BYTES = PW * PH / 2;
uint64_t hash_bytes(const uint8_t *bytes, size_t size, uint64_t hash = BASIS) {
    for (size_t i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= 1099511628211ULL; }
    return hash;
}
bool source_hash(const std::string &path, uint64_t &hash) {
    FILE *file = fopen(path.c_str(), "rb");
    if (!file) return false;
    uint8_t buffer[4096];
    hash = BASIS;
    size_t total = 0, size;
    while ((size = fread(buffer, 1, sizeof buffer, file))) {
        total += size;
        if (total > 8 * 1024 * 1024) { fclose(file); return false; }
        hash = hash_bytes(buffer, size, hash);
    }
    const bool ok = total > 0 && !ferror(file);
    fclose(file);
    return ok;
}
bool read_cache(const std::string &path, uint64_t source, Fit fit, Canvas &canvas) {
    FILE *file = fopen(path.c_str(), "rb");
    if (!file) return false;
    std::array<uint64_t, 4> header{};
    std::vector<uint8_t> photo(PHOTO_BYTES);
    bool ok = fread(header.data(), sizeof header, 1, file) == 1 && header[0] == MAGIC &&
              header[1] == source && header[2] == static_cast<uint64_t>(fit) &&
              fread(photo.data(), 1, photo.size(), file) == photo.size() &&
              fgetc(file) == EOF && !ferror(file) && header[3] == hash_bytes(photo.data(), photo.size());
    fclose(file);
    if (ok) for (int y = 0; y < PH; ++y)
        memcpy(canvas.pixels.data() + ((PY+y)*W+PX)/2, photo.data()+y*PW/2, PW/2);
    return ok;
}
void write_cache(const std::string &path, uint64_t source, Fit fit, const Canvas &canvas) {
    const std::string temporary = path + ".tmp";
    FILE *file = fopen(temporary.c_str(), "wb");
    if (!file) return;
    uint64_t hash = BASIS;
    for (int y = 0; y < PH; ++y)
        hash = hash_bytes(canvas.pixels.data()+((PY+y)*W+PX)/2, PW/2, hash);
    std::array<uint64_t,4> header{MAGIC, source, static_cast<uint64_t>(fit), hash};
    bool ok = fwrite(header.data(), sizeof header, 1, file) == 1;
    for (int y = 0; ok && y < PH; ++y)
        ok = fwrite(canvas.pixels.data()+((PY+y)*W+PX)/2, 1, PW/2, file) == PW/2;
    if (fflush(file) != 0 || fsync(fileno(file)) != 0) ok = false;
    if (fclose(file) != 0) ok = false;
    if (ok) {
        // FAT cannot always rename over an existing file. This is a disposable derived cache;
        // an interruption here only triggers regeneration, never loss of the original photo.
        remove(path.c_str());
        if (rename(temporary.c_str(), path.c_str()) == 0) return;
    }
    remove(temporary.c_str());
}
}
bool render_photo_cached(const std::string &source, Fit fit, const std::string &cache,
                         Canvas &canvas, std::string &error, bool &cache_hit) {
    uint64_t hash = 0;
    const bool hashed = source_hash(source, hash);
    cache_hit = hashed && read_cache(cache, hash, fit, canvas);
    if (cache_hit) return true;
    Image image;
    if (!load_image(source, image, error)) return false;
    canvas.photo(image, fit);
    if (hashed) write_cache(cache, hash, fit, canvas);
    return true;
}
}
