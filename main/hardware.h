#pragma once
#include "esp_err.h"
#include "frame.h"
void hardware_init();
bool mount_sd();
void sample_sensors(frame::State &state, float temperature_offset);
bool rtc_restore();
bool rtc_save();
void hardware_prepare_sleep();
esp_err_t display_frame(const frame::Canvas &canvas, bool flip);
