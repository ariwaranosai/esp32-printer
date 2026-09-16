// Panel sequence and pinout adapted from Waveshare PhotoPainter commit
// a5e8f757ba0cafbb5586f07d3e83bda3184c0845. See third_party/Waveshare-MIT.txt.
#include "hardware.h"
#include "XPowersLib.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/rtc_io.h"
#include "driver/sdmmc_host.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include <algorithm>
#include <cstring>
#include <sys/time.h>
static i2c_master_dev_handle_t sht, rtc, pmic;
static spi_device_handle_t spi;
static XPowersPMU power;
static bool power_ok = false;
static const char *TAG = "hardware";
static int read_power(uint8_t, uint8_t reg, uint8_t *data, uint8_t len) {
    return i2c_master_transmit_receive(pmic, &reg, 1, data, len, 200) == ESP_OK ? 0 : -1;
}
static int write_power(uint8_t, uint8_t reg, uint8_t *data, uint8_t len) {
    uint8_t b[257];
    b[0] = reg;
    memcpy(b + 1, data, len);
    return i2c_master_transmit(pmic, b, len + 1, 200) == ESP_OK ? 0 : -1;
}
void hardware_init() {
    rtc_gpio_deinit(GPIO_NUM_0);
    rtc_gpio_deinit(GPIO_NUM_4);
    i2c_master_bus_config_t bus{};
    bus.i2c_port = I2C_NUM_0;
    bus.sda_io_num = GPIO_NUM_47;
    bus.scl_io_num = GPIO_NUM_48;
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    i2c_master_bus_handle_t handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &handle));
    auto add = [&](int addr, i2c_master_dev_handle_t *d) {
        i2c_device_config_t c{};
        c.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        c.device_address = addr;
        c.scl_speed_hz = 100000;
        ESP_ERROR_CHECK(i2c_master_bus_add_device(handle, &c, d));
    };
    add(0x70, &sht);
    add(0x51, &rtc);
    add(0x34, &pmic);
    power_ok = power.begin(0x34, read_power, write_power);
    if (power_ok) {
        power.setDC1Voltage(3300);
        power.setALDO1Voltage(3300);
        power.setALDO2Voltage(3300);
        power.setALDO3Voltage(3300);
        power.setALDO4Voltage(3300);
    } else
        ESP_LOGW(TAG, "PMIC unavailable: battery shown as unknown");
    gpio_config_t io{};
    io.mode = GPIO_MODE_OUTPUT;
    io.pin_bit_mask = (1ULL << 8) | (1ULL << 9) | (1ULL << 12);
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(GPIO_NUM_9, 1);
    io.mode = GPIO_MODE_INPUT;
    io.pin_bit_mask = (1ULL << 13) | (1ULL << 0) | (1ULL << 4);
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&io));
    spi_bus_config_t b{};
    b.mosi_io_num = 11;
    b.miso_io_num = -1;
    b.sclk_io_num = 10;
    b.quadwp_io_num = -1;
    b.quadhd_io_num = -1;
    b.max_transfer_sz = 4096;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &b, SPI_DMA_CH_AUTO));
    spi_device_interface_config_t d{};
    d.clock_speed_hz = 40000000;
    d.mode = 0;
    d.spics_io_num = -1;
    d.queue_size = 1;
    d.flags = SPI_DEVICE_HALFDUPLEX;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &d, &spi));
}
bool mount_sd() {
    esp_vfs_fat_sdmmc_mount_config_t m{};
    m.format_if_mount_failed = false;
    m.max_files = 5;
    m.allocation_unit_size = 16 * 1024;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = GPIO_NUM_39;
    slot.cmd = GPIO_NUM_41;
    slot.d0 = GPIO_NUM_40;
    slot.d1 = GPIO_NUM_1;
    slot.d2 = GPIO_NUM_2;
    slot.d3 = GPIO_NUM_38;
    sdmmc_card_t *card = nullptr;
    auto e = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &m, &card);
    if (e != ESP_OK)
        ESP_LOGW(TAG, "SD mount: %s", esp_err_to_name(e));
    return e == ESP_OK;
}
static bool crc(const uint8_t *b, uint8_t expected) {
    uint8_t c = 255;
    for (int j = 0; j < 2; ++j) {
        c ^= b[j];
        for (int i = 0; i < 8; ++i)
            c = (c & 128) ? (c << 1) ^ 0x31 : c << 1;
    }
    return c == expected;
}
void sample_sensors(frame::State &s, float offset) {
    uint8_t wake[] = {0x35, 0x17}, measure[] = {0x78, 0x66}, sleep[] = {0xb0, 0x98}, b[6];
    s.sensor_valid = false;
    if (i2c_master_transmit(sht, wake, 2, 200) == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(2));
        if (i2c_master_transmit(sht, measure, 2, 200) == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(20));
            if (i2c_master_receive(sht, b, 6, 200) == ESP_OK && crc(b, b[2]) && crc(b + 3, b[5])) {
                s.temperature = 175.f * ((b[0] << 8) | b[1]) / 65536.f - 45.f + offset;
                s.humidity = 100.f * ((b[3] << 8) | b[4]) / 65536.f;
                s.sensor_valid = true;
            }
        }
    }
    i2c_master_transmit(sht, sleep, 2, 200);
    s.battery = -1;
    // Validate both I2C reads; XPowers' boolean getters can mask bus errors.
    uint8_t status = 0, percent = 255;
    if (power_ok && read_power(0, 0x00, &status, 1) == 0 && (status & 8) &&
        read_power(0, 0xa4, &percent, 1) == 0 && percent <= 100)
        s.battery = percent;
}
static int bcd(uint8_t v) {
    if ((v & 15) > 9 || (v >> 4) > 9)
        return -1;
    return (v >> 4) * 10 + (v & 15);
}
static uint8_t enc(int n) { return (n / 10) * 16 + n % 10; }
bool rtc_restore() {
    uint8_t reg = 0, b[11];
    if (i2c_master_transmit_receive(rtc, &reg, 1, b, sizeof b, 200) != ESP_OK || (b[0] & 0x22) ||
        (b[4] & 0x80))
        return false;
    std::tm t{};
    t.tm_sec = bcd(b[4] & 0x7f);
    t.tm_min = bcd(b[5] & 0x7f);
    t.tm_hour = bcd(b[6] & 0x3f);
    t.tm_mday = bcd(b[7] & 0x3f);
    t.tm_mon = bcd(b[9] & 0x1f) - 1;
    t.tm_year = bcd(b[10]) + 100;
    if (t.tm_sec < 0 || t.tm_sec > 59 || t.tm_min < 0 || t.tm_min > 59 || t.tm_hour < 0 ||
        t.tm_hour > 23 || t.tm_mday < 1 || t.tm_mday > 31 || t.tm_mon < 0 || t.tm_mon > 11 ||
        t.tm_year < 124 || t.tm_year > 199)
        return false;
    // RTC stores UTC. POSIX TZ is only applied to the user-facing local time.
    time_t value;
    if (!frame::utc_epoch(t, value))
        return false;
    timeval tv{value, 0};
    return settimeofday(&tv, nullptr) == 0;
}
bool rtc_save() {
    time_t now = time(nullptr);
    std::tm t{};
    gmtime_r(&now, &t);
    if (t.tm_year < 124 || t.tm_year > 199)
        return false;
    uint8_t reg = 0, ctrl;
    if (i2c_master_transmit_receive(rtc, &reg, 1, &ctrl, 1, 200) != ESP_OK)
        return false;
    uint8_t stop[] = {0, uint8_t((ctrl & ~0x02) | 0x20)};
    if (i2c_master_transmit(rtc, stop, 2, 200) != ESP_OK)
        return false;
    uint8_t b[] = {4,
                   enc(t.tm_sec),
                   enc(t.tm_min),
                   enc(t.tm_hour),
                   enc(t.tm_mday),
                   uint8_t(t.tm_wday),
                   enc(t.tm_mon + 1),
                   enc(t.tm_year - 100)};
    bool ok = i2c_master_transmit(rtc, b, sizeof b, 200) == ESP_OK;
    uint8_t start[] = {0, uint8_t(ctrl & ~0x22)};
    return i2c_master_transmit(rtc, start, 2, 200) == ESP_OK && ok;
}
static esp_err_t busy() {
    int64_t deadline = esp_timer_get_time() + 90000000;
    while (!gpio_get_level(GPIO_NUM_13)) {
        if (esp_timer_get_time() > deadline)
            return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_OK;
}
static esp_err_t send(bool data, const uint8_t *b, size_t n) {
    gpio_set_level(GPIO_NUM_8, data);
    gpio_set_level(GPIO_NUM_9, 0);
    spi_transaction_t t{};
    t.length = n * 8;
    t.tx_buffer = b;
    esp_err_t e = spi_device_polling_transmit(spi, &t);
    gpio_set_level(GPIO_NUM_9, 1);
    return e;
}
static esp_err_t command(uint8_t cmd, std::initializer_list<uint8_t> data = {}) {
    auto e = send(false, &cmd, 1);
    if (e != ESP_OK)
        return e;
    for (auto b : data) {
        e = send(true, &b, 1);
        if (e != ESP_OK)
            return e;
    }
    return ESP_OK;
}
#define TRY(x)                                                                                     \
    do {                                                                                           \
        esp_err_t result_ = (x);                                                                   \
        if (result_ != ESP_OK) {                                                                   \
            ESP_LOGE(TAG, "Panel: %s", esp_err_to_name(result_));                                  \
            return result_;                                                                        \
        }                                                                                          \
    } while (0)
esp_err_t display_frame(const frame::Canvas &canvas, bool flip) {
    gpio_set_level(GPIO_NUM_12, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(GPIO_NUM_12, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(GPIO_NUM_12, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    TRY(busy());
    vTaskDelay(pdMS_TO_TICKS(50));
    TRY(command(0xaa, {0x49, 0x55, 0x20, 0x08, 0x09, 0x18}));
    TRY(command(0x01, {0x3f}));
    TRY(command(0x00, {0x5f, 0x69}));
    TRY(command(0x03, {0, 0x54, 0, 0x44}));
    TRY(command(0x05, {0x40, 0x1f, 0x1f, 0x2c}));
    TRY(command(0x06, {0x6f, 0x1f, 0x17, 0x49}));
    TRY(command(0x08, {0x6f, 0x1f, 0x1f, 0x22}));
    TRY(command(0x30, {3}));
    TRY(command(0x50, {0x3f}));
    TRY(command(0x60, {2, 0}));
    TRY(command(0x61, {3, 0x20, 1, 0xe0}));
    TRY(command(0x84, {1}));
    TRY(command(0xe3, {0x2f}));
    TRY(command(0x04));
    TRY(busy());
    auto panel = canvas.panel(flip);
    TRY(command(0x10));
    // DMA bounce buffer in internal RAM; keep CS low for the full image.
    auto *chunk = static_cast<uint8_t *>(heap_caps_malloc(4096, MALLOC_CAP_DMA));
    if (!chunk)
        return ESP_ERR_NO_MEM;
    gpio_set_level(GPIO_NUM_8, 1);
    gpio_set_level(GPIO_NUM_9, 0);
    esp_err_t e = ESP_OK;
    for (size_t i = 0; i < panel.size(); i += 4096) {
        size_t n = std::min(size_t(4096), panel.size() - i);
        memcpy(chunk, panel.data() + i, n);
        spi_transaction_t t{};
        t.length = n * 8;
        t.tx_buffer = chunk;
        e = spi_device_polling_transmit(spi, &t);
        if (e != ESP_OK)
            break;
    }
    gpio_set_level(GPIO_NUM_9, 1);
    heap_caps_free(chunk);
    TRY(e);
    TRY(command(0x04));
    TRY(busy());
    TRY(command(0x06, {0x6f, 0x1f, 0x17, 0x49}));
    TRY(command(0x12, {0}));
    TRY(busy());
    TRY(command(0x02, {0}));
    TRY(busy());
    return ESP_OK;
}
