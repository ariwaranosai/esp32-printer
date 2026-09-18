#include "photo_client.h"
#include "photo_policy.h"
#include "frame.h"
#ifdef PHOTO_CLIENT_HOST_TEST
#include "photo_client_test_support.h"
#else
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#endif
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr size_t MAX_BYTES = 1024 * 1024;
constexpr const char *TAG = "nas_photo";
#ifndef PHOTO_CACHE_DIR
#define PHOTO_CACHE_DIR "/sdcard/.nasphoto"
#endif
struct Record {
    uint64_t source = 0;
    int64_t fetched = 0;
    uint32_t version = 1;
    uint32_t slot = 0;
};
std::string slot_path(unsigned slot) {
    return std::string(PHOTO_CACHE_DIR) + (slot ? "/b.jpg" : "/a.jpg");
}
bool read_record(const std::string &url, Record &record) {
    nvs_handle_t handle;
    if (nvs_open("nas_photo", NVS_READONLY, &handle) != ESP_OK) return false;
    size_t size = sizeof record;
    auto err = nvs_get_blob(handle, "active", &record, &size);
    nvs_close(handle);
    return err == ESP_OK && size == sizeof record && record.version == 1 && record.slot <= 1 &&
           record.source == remote_photo::source_id(url);
}
bool save_record(const Record &record) {
    nvs_handle_t handle;
    if (nvs_open("nas_photo", NVS_READWRITE, &handle) != ESP_OK) return false;
    auto err = nvs_set_blob(handle, "active", &record, sizeof record);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}
bool exists(const std::string &path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0 &&
           st.st_size <= static_cast<off_t>(MAX_BYTES);
}
bool download(const std::string &url, const std::string &token, const std::string &path) {
    esp_http_client_config_t config{};
    config.url = url.c_str();
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.timeout_ms = 30000;
    config.buffer_size = 2048;
    config.disable_auto_redirect = true;
    auto client = esp_http_client_init(&config);
    if (!client) return false;
    const std::string auth = "Bearer " + token;
    bool ok = false;
    FILE *file = nullptr;
    size_t received = 0;
    const auto deadline = esp_timer_get_time() + 120000000LL;
    if (esp_http_client_set_header(client, "Authorization", auth.c_str()) == ESP_OK &&
        esp_http_client_set_header(client, "Accept", "image/jpeg") == ESP_OK &&
        esp_http_client_open(client, 0) == ESP_OK) {
        const auto length = esp_http_client_fetch_headers(client);
        const int status = esp_http_client_get_status_code(client);
        if (status == 200 && length >= 0 && length <= static_cast<int64_t>(MAX_BYTES)) {
            file = fopen(path.c_str(), "wb");
            if (file) {
                char buffer[2048];
                while (esp_timer_get_time() < deadline) {
                    const int count = esp_http_client_read(client, buffer, sizeof buffer);
                    if (count < 0 || received + count > MAX_BYTES) break;
                    if (!count) {
                        ok = received > 0 && esp_http_client_is_complete_data_received(client);
                        break;
                    }
                    if (fwrite(buffer, 1, count, file) != static_cast<size_t>(count)) break;
                    received += count;
                }
            }
        } else ESP_LOGW(TAG, "HTTP status %d, length %lld; keeping cached photo", status,
                        static_cast<long long>(length));
    }
    if (file) {
        if (fflush(file) != 0 || fsync(fileno(file)) != 0) ok = false;
        if (fclose(file) != 0) ok = false;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (ok) {
        // Decode before publishing: reject HTML/JSON, truncation and unexpected dimensions.
        FILE *check = fopen(path.c_str(), "rb");
        ok = check && fgetc(check) == 0xff && fgetc(check) == 0xd8;
        if (ok) ok = fseek(check, -2, SEEK_END) == 0 && fgetc(check) == 0xff && fgetc(check) == 0xd9;
        if (check) fclose(check);
        frame::Image image;
        std::string error;
        ok = ok && frame::load_image(path, image, error) && image.w == frame::PW && image.h == frame::PH;
    }
    if (!ok) {
        remove(path.c_str());
        ESP_LOGW(TAG, "Download or image validation failed; keeping cached photo");
    } else ESP_LOGI(TAG, "Verified JPEG: %u bytes, 432x576", static_cast<unsigned>(received));
    return ok;
}
} // namespace

std::string cached_remote_photo(const std::string &url) {
    if (url.empty()) return {};
    Record record;
    if (!read_record(url, record)) return {};
    const auto path = slot_path(record.slot);
    return exists(path) ? path : std::string{};
}

bool refresh_remote_photo(const std::string &url, const std::string &token, int interval, bool force) {
    if (!remote_photo::valid_settings(url, token)) return false;
    Record previous;
    const bool recorded = read_record(url, previous);
    const bool cached = recorded && exists(slot_path(previous.slot));
    if (!remote_photo::due(cached, previous.fetched, time(nullptr), interval, force)) {
        ESP_LOGI(TAG, "Cached photo is current; next change after %d seconds", interval);
        return true;
    }
    if (mkdir(PHOTO_CACHE_DIR, 0775) != 0 && errno != EEXIST) return false;
    // Preserve the active slot even if URL changes, until a new photo is committed.
    Record active;
    nvs_handle_t handle;
    bool has_active = false;
    if (nvs_open("nas_photo", NVS_READONLY, &handle) == ESP_OK) {
        size_t size = sizeof active;
        has_active = nvs_get_blob(handle, "active", &active, &size) == ESP_OK &&
                     size == sizeof active && active.version == 1 && active.slot <= 1;
        nvs_close(handle);
    }
    Record next;
    next.source = remote_photo::source_id(url);
    next.slot = has_active ? 1 - active.slot : 0;
    ESP_LOGI(TAG, "Requesting a new NAS photo");
    if (!download(url, token, slot_path(next.slot))) return false;
    next.fetched = time(nullptr);
    if (!save_record(next)) {
        ESP_LOGW(TAG, "Could not commit photo selection; keeping previous photo");
        return false;
    }
    return true;
}
