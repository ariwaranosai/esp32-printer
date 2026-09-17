#include "weather_client.h"
#include "weather.h"
#include "esp_attr.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>

static RTC_DATA_ATTR frame::Weather cache;
static RTC_DATA_ATTR char cache_adcode[7]{};
static RTC_DATA_ATTR time_t cached_at = 0;

frame::Weather cached_weather(const std::string &adcode) {
    auto result = cache;
    auto now = time(nullptr);
    if (adcode != cache_adcode || !cache.valid || cached_at < 1704067200 ||
        now < cached_at || now - cached_at > 86400)
        return {};
    result.stale = true;
    return result;
}

bool fetch_weather(const std::string &adcode, frame::Weather &out) {
    const auto url = weather::url(adcode);
    esp_http_client_config_t config{};
    config.url = url.c_str();
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.timeout_ms = 8000;
    config.buffer_size = 1024;
    config.disable_auto_redirect = true;
    auto client = esp_http_client_init(&config);
    if (!client) return false;
    const int64_t deadline = esp_timer_get_time() + 20000000;
    bool ok = false;
    std::string body;
    body.reserve(2048);
    const auto err = esp_http_client_open(client, 0);
    if (err == ESP_OK) {
        const auto length = esp_http_client_fetch_headers(client);
        const int status = esp_http_client_get_status_code(client);
        if (length >= 0 && length <= 16384 && status == 200) {
            char buffer[1024];
            while (esp_timer_get_time() < deadline) {
                const int n = esp_http_client_read(client, buffer, sizeof buffer);
                if (n < 0 || body.size() + n > 16384) break;
                if (!n) {
                    if (esp_http_client_is_complete_data_received(client))
                        ok = weather::parse(body.data(), body.size(), out, time(nullptr));
                    break;
                }
                body.append(buffer, n);
            }
        } else {
            ESP_LOGW("weather", "HTTP status %d; content length %lld", status, (long long)length);
        }
    } else {
        ESP_LOGW("weather", "HTTPS: %s", esp_err_to_name(err));
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (ok) {
        cache = out;
        snprintf(cache_adcode, sizeof cache_adcode, "%s", adcode.c_str());
        cached_at = time(nullptr);
        ESP_LOGI("weather", "%s: %s, %.1f C, report %s", out.city, out.description,
                 out.temperature, out.report_time);
    } else {
        ESP_LOGW("weather", "No fresh weather; showing labelled cache or unavailable state");
    }
    return ok;
}
