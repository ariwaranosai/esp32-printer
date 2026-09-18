#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "hardware.h"
#include "nvs_flash.h"
#include "weather.h"
#include "weather_client.h"
#include "photo_client.h"
#include "photo_policy.h"
#include "photo_cache.h"
#include "power_policy.h"
#include "refresh_policy.h"
#if __has_include("photo_service_private.h")
#include "photo_service_private.h"
#endif
#ifndef PHOTO_SERVICE_URL
#define PHOTO_SERVICE_URL ""
#define PHOTO_SERVICE_TOKEN ""
#endif
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <vector>
static const char *TAG = "photopainter";
struct Config {
    std::string ssid, password, tz = "CST-8", ntp = "pool.ntp.org";
    std::string adcode;
    std::string photo_url = PHOTO_SERVICE_URL, photo_token = PHOTO_SERVICE_TOKEN;
    int photo_refresh = 10800;
    int refresh = 10800;
    refresh_policy::QuietHours quiet;
    float offset = 0;
    bool flip = false;
    frame::Fit fit = frame::Fit::Cover;
};
static RTC_DATA_ATTR unsigned photo_index = 0;
static RTC_DATA_ATTR int64_t last_ntp_sync = 0;
struct AccessPointCache {
    uint64_t network = 0;
    uint8_t bssid[6]{};
    uint8_t channel = 0;
};
static RTC_DATA_ATTR AccessPointCache access_point;
static bool next_photo_requested() {
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT1) return false;
    const uint64_t buttons = esp_sleep_get_ext1_wakeup_status();
    if (buttons & (1ULL << 0)) return true; // Existing BOOT next-photo shortcut.
    if (!(buttons & (1ULL << 4))) return false;
    // Check before SD/network operations so their latency cannot swallow a long press.
    // Deep-sleep boot adds roughly one second: hold KEY for about three seconds in total.
    const int64_t deadline = esp_timer_get_time() + 2000000;
    int released = 0;
    while (esp_timer_get_time() < deadline) {
        if (gpio_get_level(GPIO_NUM_4)) {
            if (++released >= 3) return false; // 60 ms release debounce.
        } else released = 0;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (released || gpio_get_level(GPIO_NUM_4)) return false;
    ESP_LOGI(TAG, "KEY long press: force next photo once");
    return true;
}
static Config read_config() {
    Config c;
    FILE *f = fopen("/sdcard/config.json", "rb");
    if (!f)
        return c;
    char b[4097];
    size_t n = fread(b, 1, 4096, f);
    bool too_big = fgetc(f) != EOF;
    fclose(f);
    b[n] = 0;
    cJSON *json = too_big ? nullptr : cJSON_ParseWithLength(b, n);
    if (!json || !cJSON_IsObject(json)) {
        ESP_LOGW(TAG, "Invalid config.json; using defaults");
        cJSON_Delete(json);
        return c;
    }
    auto str = [&](const char *key, std::string &value, size_t max) {
        const auto *v = cJSON_GetObjectItemCaseSensitive(json, key);
        if (cJSON_IsString(v) && strlen(v->valuestring) <= max)
            value = v->valuestring;
    };
    str("wifi_ssid", c.ssid, 32);
    str("wifi_password", c.password, 64);
    str("timezone", c.tz, 63);
    str("ntp_server", c.ntp, 127);
    str("weather_adcode", c.adcode, 6);
    str("photo_service_url", c.photo_url, 512);
    str("photo_service_token", c.photo_token, 256);
    if (!c.photo_url.empty() && !remote_photo::valid_settings(c.photo_url, c.photo_token)) {
        ESP_LOGW(TAG, "Invalid photo service settings; using SD photos");
        c.photo_url.clear();
    }
    if (!weather::valid_adcode(c.adcode)) {
        ESP_LOGW(TAG, "Invalid weather_adcode; using IP location");
        c.adcode.clear();
    }
    auto v = cJSON_GetObjectItemCaseSensitive(json, "refresh_seconds");
    if (cJSON_IsNumber(v) && std::isfinite(v->valuedouble) && v->valuedouble >= 60 &&
        v->valuedouble <= 86400)
        c.refresh = std::max(10800, int(v->valuedouble));
    v = cJSON_GetObjectItemCaseSensitive(json, "quiet_hours_enabled");
    if (cJSON_IsBool(v)) c.quiet.enabled = cJSON_IsTrue(v);
    auto hour = [&](const char *key, int &value) {
        const auto *item = cJSON_GetObjectItemCaseSensitive(json, key);
        if (cJSON_IsNumber(item) && std::isfinite(item->valuedouble) &&
            item->valuedouble >= 0 && item->valuedouble <= 23 &&
            std::floor(item->valuedouble) == item->valuedouble)
            value = int(item->valuedouble);
    };
    hour("quiet_hours_start", c.quiet.start);
    hour("quiet_hours_end", c.quiet.end);
    v = cJSON_GetObjectItemCaseSensitive(json, "temperature_offset");
    if (cJSON_IsNumber(v) && std::isfinite(v->valuedouble) && fabs(v->valuedouble) <= 20)
        c.offset = v->valuedouble;
    v = cJSON_GetObjectItemCaseSensitive(json, "photo_refresh_seconds");
    if (cJSON_IsNumber(v) && std::isfinite(v->valuedouble) && v->valuedouble >= 3600 &&
        v->valuedouble <= 604800)
        c.photo_refresh = int(v->valuedouble);
    v = cJSON_GetObjectItemCaseSensitive(json, "rotate_180");
    c.flip = cJSON_IsTrue(v);
    v = cJSON_GetObjectItemCaseSensitive(json, "photo_fit");
    if (cJSON_IsString(v) && strcmp(v->valuestring, "contain") == 0)
        c.fit = frame::Fit::Contain;
    cJSON_Delete(json);
    return c;
}
static EventGroupHandle_t wifi_events;
static void wifi_event(void *, esp_event_base_t base, int32_t id, void *) {
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
        xEventGroupSetBits(wifi_events, 1);
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
        xEventGroupClearBits(wifi_events, 1);
}
static bool sync_network(const Config &c, frame::Weather &weather, bool sd, bool next_photo) {
    if (c.ssid.empty())
        return false;
    wifi_events = xEventGroupCreate();
    if (!wifi_events)
        return false;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, nullptr));
    wifi_config_t wifi{};
    memcpy(wifi.sta.ssid, c.ssid.data(), c.ssid.size());
    memcpy(wifi.sta.password, c.password.data(), c.password.size());
    const auto network_id = remote_photo::source_id(c.ssid);
    const bool known_ap = access_point.network == network_id && access_point.channel > 0 &&
                          access_point.channel <= 14;
    if (known_ap) {
        wifi.sta.channel = access_point.channel;
        memcpy(wifi.sta.bssid, access_point.bssid, 6);
        wifi.sta.bssid_set = true;
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    auto config_error = esp_wifi_set_config(WIFI_IF_STA, &wifi);
    if (config_error != ESP_OK) {
        ESP_LOGW(TAG, "Invalid Wi-Fi configuration: %s; continuing offline",
                 esp_err_to_name(config_error));
        esp_wifi_deinit();
        return false;
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    bool connected = false;
    esp_wifi_connect();
    connected = xEventGroupWaitBits(wifi_events, 1, pdFALSE, pdFALSE,
                                   pdMS_TO_TICKS(known_ap ? 5000 : 12000)) & 1;
    if (!connected && known_ap) {
        // AP/channel may have changed: fall back to a full scan once, within a bounded budget.
        esp_wifi_stop();
        xEventGroupClearBits(wifi_events, 1);
        wifi.sta.bssid_set = false;
        wifi.sta.channel = 0;
        access_point = {};
        esp_wifi_set_config(WIFI_IF_STA, &wifi);
        esp_wifi_start();
        esp_wifi_connect();
        connected = xEventGroupWaitBits(wifi_events, 1, pdFALSE, pdFALSE, pdMS_TO_TICKS(12000)) & 1;
    }
    if (connected) {
        wifi_ap_record_t ap{};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            access_point.network = network_id;
            access_point.channel = ap.primary;
            memcpy(access_point.bssid, ap.bssid, 6);
        }
        if (power_policy::ntp_due(time(nullptr), last_ntp_sync)) {
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, c.ntp.c_str());
            esp_sntp_init();
            for (int i = 0; i < 80; ++i) {
                if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
                    last_ntp_sync = time(nullptr);
                    if (!rtc_save()) ESP_LOGW(TAG, "RTC write failed; system clock still valid");
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            esp_sntp_stop();
        } else ESP_LOGI(TAG, "RTC clock retained; daily NTP sync not due");
        // NTP may have corrected an unknown clock into the quiet interval.
        if (!refresh_policy::skip_refresh(time(nullptr), c.quiet, next_photo)) {
            fetch_weather(c.adcode, weather);
            if (sd && !c.photo_url.empty() &&
                !refresh_policy::skip_refresh(time(nullptr), c.quiet, next_photo))
                refresh_remote_photo(c.photo_url, c.photo_token, c.photo_refresh, next_photo);
        }
    }
    connected = (xEventGroupGetBits(wifi_events) & 1) != 0;
    esp_wifi_stop();
    esp_wifi_deinit();
    return connected;
}
static std::vector<std::string> photos() {
    std::vector<std::string> paths;
    // Original Waveshare cards remain usable; no fixed 800x480 assumption.
    for (const char *dir : {"/sdcard/photos", "/sdcard/06_user_Foundation_img"}) {
        DIR *d = opendir(dir);
        if (!d)
            continue;
        dirent *e;
        while ((e = readdir(d)) && paths.size() < 256) {
            std::string name = e->d_name;
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            auto dot = lower.rfind('.');
            if (name[0] == '.' || dot == std::string::npos || lower == "sys_decode.bmp")
                continue;
            auto ext = lower.substr(dot);
            if (ext != ".jpg" && ext != ".jpeg" && ext != ".png" && ext != ".bmp")
                continue;
            auto path = std::string(dir) + "/" + name;
            struct stat st{};
            if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode))
                paths.push_back(path);
        }
        closedir(d);
        if (!paths.empty())
            break;
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}
static void sleep_until(time_t target, int64_t wake_started) {
    const uint64_t delay = std::max(int64_t(30), int64_t(target) - int64_t(time(nullptr)));
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(delay * 1000000ULL));
    // Avoid a held button causing a wake-refresh loop.
    for (int i = 0; i < 30 && (!gpio_get_level(GPIO_NUM_0) || !gpio_get_level(GPIO_NUM_4)); ++i)
        vTaskDelay(pdMS_TO_TICKS(100));
    if (gpio_get_level(GPIO_NUM_0) && gpio_get_level(GPIO_NUM_4)) {
        rtc_gpio_pullup_en(GPIO_NUM_0);
        rtc_gpio_pulldown_dis(GPIO_NUM_0);
        rtc_gpio_pullup_en(GPIO_NUM_4);
        rtc_gpio_pulldown_dis(GPIO_NUM_4);
        ESP_ERROR_CHECK(
            esp_sleep_enable_ext1_wakeup_io((1ULL << 0) | (1ULL << 4), ESP_EXT1_WAKEUP_ANY_LOW));
    }
    hardware_prepare_sleep();
    ESP_LOGI(TAG, "Awake %lld ms; sleeping %llu seconds",
             (long long)((esp_timer_get_time()-wake_started)/1000), (unsigned long long)delay);
    // Give USB console time to deliver the final diagnostics before disconnecting.
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(30) + 1);
    esp_deep_sleep_start();
}
extern "C" void app_main() {
    const int64_t wake_started = esp_timer_get_time();
    auto err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG, "NVS incompatible; back up settings then erase NVS before flashing");
        return;
    }
    ESP_ERROR_CHECK(err);
    hardware_init();
    const bool next_photo = next_photo_requested();
    bool sd = mount_sd();
    Config c = sd ? read_config() : Config{};
    ESP_LOGI(TAG, "SD %s; refresh interval %d seconds", sd ? "mounted" : "unavailable", c.refresh);
    setenv("TZ", c.tz.c_str(), 1);
    tzset();
    // ESP32 RTC retains system time through deep sleep; only restore external RTC after cold boot.
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED && !rtc_restore())
        ESP_LOGW(TAG, "RTC unset: connect Wi-Fi to set clock");
    auto sleep_if_quiet = [&]() {
        const auto now = time(nullptr);
        if (!refresh_policy::skip_refresh(now, c.quiet, next_photo)) return;
        ESP_LOGI(TAG, "Quiet hours %02d:00-%02d:00; keeping display until morning",
                 c.quiet.start, c.quiet.end);
        sleep_until(refresh_policy::next_wake(now, c.refresh, c.quiet), wake_started);
    };
    sleep_if_quiet(); // Skip Wi-Fi, sensors and panel when the clock is already valid.
    frame::State state;
    state.weather = cached_weather(c.adcode);
    state.refresh_seconds = c.refresh;
    state.wifi = sync_network(c, state.weather, sd, next_photo);
    sleep_if_quiet(); // Recheck after NTP correction / a midnight boundary.
    sample_sensors(state, c.offset);
    time_t sampled = time(nullptr);
    frame::Canvas canvas;
    bool loaded = false;
    if (sd) mkdir("/sdcard/.nasphoto", 0775);
    auto render = [&](const std::string &path, std::string &error) {
        bool hit = false;
        const auto start = esp_timer_get_time();
        const bool ok = frame::render_photo_cached(path, c.fit, "/sdcard/.nasphoto/render.bin",
                                                  canvas, error, hit);
        if (ok) ESP_LOGI(TAG, "Photo %s in %lld ms", hit ? "cache hit" : "rendered",
                        (long long)((esp_timer_get_time()-start)/1000));
        return ok;
    };
    if (sd && !c.photo_url.empty()) {
        const auto path = cached_remote_photo(c.photo_url);
        std::string error;
        if (!path.empty() && render(path, error)) {
            loaded = true;
            ESP_LOGI(TAG, "Displaying cached NAS photo");
        }
    }
    auto list = sd && !loaded ? photos() : std::vector<std::string>{};
    if (!loaded && !list.empty()) {
        if (next_photo)
            ++photo_index;
        photo_index %= list.size();
        // Skip at most 8 bad files per wake to bound latency and power use.
        for (size_t i = 0; i < std::min(list.size(), size_t(8)); ++i) {
            std::string error;
            size_t index = (photo_index + i) % list.size();
            if (render(list[index], error)) {
                photo_index = index;
                loaded = true;
                break;
            }
            ESP_LOGW(TAG, "Photo %s: %s", list[index].c_str(), error.c_str());
        }
    }
    time_t now = sampled;
    state.time_valid = now >= 1704067200;
    localtime_r(&now, &state.local);
    ESP_LOGI(TAG, "Display time %02d:%02d:%02d; clock %s", state.local.tm_hour,
             state.local.tm_min, state.local.tm_sec, state.time_valid ? "valid" : "unset");
    sleep_if_quiet(); // Rendering may also cross the start of quiet hours.
    canvas.ui(state, loaded);
    // Default mounting orientation is inverted relative to the panel's native scan order.
    ESP_LOGI(TAG, "Panel rotation: %d degrees", c.flip ? 0 : 180);
    err = display_frame(canvas, !c.flip);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "Display failed; retry on next scheduled wake");
    // Include rendering and panel time in the interval, and skip quiet hours.
    sleep_until(refresh_policy::next_wake(sampled, c.refresh, c.quiet), wake_started);
}
