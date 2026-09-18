#include "audio_sleep.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

namespace {
constexpr const char *TAG = "audio_sleep";
template <std::size_t N>
void suspend(i2c_master_bus_handle_t bus, uint8_t address, const char *name,
             const audio_sleep::Register (&sequence)[N]) {
    if (i2c_master_probe(bus, address, 100) != ESP_OK) {
        ESP_LOGW(TAG, "%s at 0x%02x unavailable; standby skipped", name, address);
        return;
    }
    i2c_device_config_t config{};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = address;
    config.scl_speed_hz = 100000;
    i2c_master_dev_handle_t dev = nullptr;
    if (i2c_master_bus_add_device(bus, &config, &dev) != ESP_OK) {
        ESP_LOGW(TAG, "%s I2C handle unavailable", name);
        return;
    }
    const auto result = audio_sleep::apply(sequence,
        [&](uint8_t reg, uint8_t &value) {
            return i2c_master_transmit_receive(dev, &reg, 1, &value, 1, 100) == ESP_OK;
        },
        [&](uint8_t reg, uint8_t value) {
            const uint8_t bytes[] = {reg, value};
            return i2c_master_transmit(dev, bytes, sizeof bytes, 100) == ESP_OK;
        });
    using audio_sleep::Status;
    if (result.status == Status::already_asleep)
        ESP_LOGI(TAG, "%s standby already verified", name);
    else if (result.status == Status::applied)
        ESP_LOGI(TAG, "%s standby verified; first changed reg 0x%02x: 0x%02x -> 0x%02x",
                 name, result.reg, result.before, result.after);
    else
        ESP_LOGW(TAG, "%s standby failed (%d) at reg 0x%02x; actual 0x%02x expected 0x%02x",
                 name, int(result.status), result.reg, result.before, result.after);
    i2c_master_bus_rm_device(dev);
}
} // namespace

void hardware_audio_sleep(i2c_master_bus_handle_t bus) {
    // Board addresses are 7-bit, not esp_codec_dev's shifted 8-bit addresses.
    // Audio_VCC remains on: disabling ALDO3 breaks shared RTC/SHTC3 I2C on this board.
    suspend(bus, 0x18, "ES8311", audio_sleep::es8311);
    suspend(bus, 0x40, "ES7210", audio_sleep::es7210);
}
