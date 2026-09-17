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
    int refresh = 3600;
    float offset = 0;
    bool flip = false;
    frame::Fit fit = frame::Fit::Cover;
};
static RTC_DATA_ATTR unsigned photo_index = 0;
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
    if (!weather::valid_adcode(c.adcode)) {
        ESP_LOGW(TAG, "Invalid weather_adcode; using IP location");
        c.adcode.clear();
    }
    auto v = cJSON_GetObjectItemCaseSensitive(json, "refresh_seconds");
    if (cJSON_IsNumber(v) && std::isfinite(v->valuedouble) && v->valuedouble >= 60 &&
        v->valuedouble <= 86400)
        c.refresh = std::max(3600, int(v->valuedouble));
    v = cJSON_GetObjectItemCaseSensitive(json, "temperature_offset");
    if (cJSON_IsNumber(v) && std::isfinite(v->valuedouble) && fabs(v->valuedouble) <= 20)
        c.offset = v->valuedouble;
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
static bool sync_network(const Config &c, frame::Weather &weather) {
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
    for (int attempt = 0; attempt < 2 && !connected; ++attempt) {
        esp_wifi_connect();
        connected = xEventGroupWaitBits(wifi_events, 1, pdFALSE, pdFALSE, pdMS_TO_TICKS(10000)) & 1;
    }
    if (connected) {
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, c.ntp.c_str());
        esp_sntp_init();
        for (int i = 0; i < 100; ++i) {
            if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
                if (!rtc_save())
                    ESP_LOGW(TAG, "RTC write failed; system clock still valid");
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        esp_sntp_stop();
        fetch_weather(c.adcode, weather);
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
extern "C" void app_main() {
    auto err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG, "NVS incompatible; back up settings then erase NVS before flashing");
        return;
    }
    ESP_ERROR_CHECK(err);
    hardware_init();
    bool sd = mount_sd();
    Config c = sd ? read_config() : Config{};
    ESP_LOGI(TAG, "SD %s; refresh interval %d seconds", sd ? "mounted" : "unavailable", c.refresh);
    setenv("TZ", c.tz.c_str(), 1);
    tzset();
    // ESP32 RTC retains system time through deep sleep; only restore external RTC after cold boot.
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED && !rtc_restore())
        ESP_LOGW(TAG, "RTC unset: connect Wi-Fi to set clock");
    frame::State state;
    state.weather = cached_weather(c.adcode);
    state.refresh_seconds = c.refresh;
    state.wifi = sync_network(c, state.weather);
    sample_sensors(state, c.offset);
    time_t sampled = time(nullptr);
    frame::Canvas canvas;
    bool loaded = false;
    auto list = sd ? photos() : std::vector<std::string>{};
    if (!list.empty()) {
        if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1 &&
            (esp_sleep_get_ext1_wakeup_status() & (1ULL << 0)))
            ++photo_index;
        photo_index %= list.size();
        // Skip at most 8 bad files per wake to bound latency and power use.
        for (size_t i = 0; i < std::min(list.size(), size_t(8)); ++i) {
            frame::Image photo;
            std::string error;
            size_t index = (photo_index + i) % list.size();
            if (frame::load_image(list[index], photo, error)) {
                canvas.photo(photo, c.fit);
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
    canvas.ui(state, loaded);
    int64_t started = esp_timer_get_time();
    err = display_frame(canvas, c.flip);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "Display failed; retry on next scheduled wake");
    // Next refresh interval measured from the sampled time, excluding refresh duration.
    int64_t elapsed = (esp_timer_get_time() - started) / 1000000;
    uint64_t delay = std::max(int64_t(30), int64_t(c.refresh) - elapsed);
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
    ESP_LOGI(TAG, "Sleeping %llu seconds", (unsigned long long)delay);
    esp_deep_sleep_start();
}
