#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
constexpr int ESP_OK = 0, NVS_READONLY = 0, NVS_READWRITE = 1;
using nvs_handle_t = int;
inline std::vector<uint8_t> committed, pending;
inline bool fail_commit = false, fail_open = false, complete = true;
inline int status = 200, requests = 0;
inline size_t cursor = 0;
inline int64_t content_length = 0;
inline std::string body, authorization;
struct esp_http_client_config_t {
    const char *url;
    void (*crt_bundle_attach)();
    int timeout_ms, buffer_size;
    bool disable_auto_redirect;
};
inline void esp_crt_bundle_attach() {}
inline int64_t esp_timer_get_time() { return 0; }
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
inline int nvs_open(const char *, int mode, nvs_handle_t *h) {
    *h = 1;
    return mode == NVS_READONLY && committed.empty() ? -1 : ESP_OK;
}
inline int nvs_get_blob(int, const char *, void *data, size_t *size) {
    if (committed.empty() || *size < committed.size()) return -1;
    *size = committed.size(); memcpy(data, committed.data(), *size); return ESP_OK;
}
inline int nvs_set_blob(int, const char *, const void *data, size_t size) {
    const auto *bytes = static_cast<const uint8_t *>(data);
    pending.assign(bytes, bytes + size); return ESP_OK;
}
inline int nvs_commit(int) {
    if (fail_commit) return -1;
    committed = pending; return ESP_OK;
}
inline void nvs_close(int) {}
inline void *esp_http_client_init(const esp_http_client_config_t *c) {
    if (!c->disable_auto_redirect) return nullptr;
    return reinterpret_cast<void *>(1);
}
inline int esp_http_client_set_header(void *, const char *name, const char *value) {
    if (std::string(name) == "Authorization") authorization = value;
    return ESP_OK;
}
inline int esp_http_client_open(void *, int) { ++requests; cursor = 0; return fail_open ? -1 : ESP_OK; }
inline int64_t esp_http_client_fetch_headers(void *) { return content_length; }
inline int esp_http_client_get_status_code(void *) { return status; }
inline int esp_http_client_read(void *, char *out, int size) {
    const auto n = std::min(body.size() - cursor, static_cast<size_t>(size));
    memcpy(out, body.data() + cursor, n); cursor += n; return static_cast<int>(n);
}
inline bool esp_http_client_is_complete_data_received(void *) { return complete; }
inline void esp_http_client_close(void *) {}
inline void esp_http_client_cleanup(void *) {}
