#pragma once
#include <cstddef>
#include <cstdint>

namespace audio_sleep {
struct Register { uint8_t address, value, mask; };
// Power-down sequences adapted from Espressif esp_codec_dev:
// device/es8311/es8311.c (es8311_power_down), device/es7210/es7210.c (es7210_stop).
// Copyright (c) 2023-2026 Espressif Systems (Shanghai) CO., LTD.
// See third_party/Espressif-codec-LICENSE.txt and docs/audio-power.md.
inline constexpr Register es8311[] = {
    {0x32, 0x00, 0xff}, {0x17, 0x00, 0xff}, {0x0e, 0xff, 0x60},
    {0x12, 0x02, 0x03}, {0x14, 0x00, 0x50}, {0x0d, 0xfa, 0xfc},
    {0x15, 0x00, 0xff}, {0x02, 0x10, 0xff}, {0x00, 0x00, 0x80},
    {0x00, 0x1f, 0x80}, {0x01, 0x30, 0x3f}, {0x01, 0x00, 0x3f},
    {0x45, 0x00, 0x01}, {0x0d, 0xfc, 0xfc}, {0x02, 0x00, 0xff},
};
// ES7210 DS rev21: MIC1/3 low-power bits 5:0; MIC2/4 bits 4:0.
// Reserved upper bits read as zero on the actual board.
inline constexpr Register es7210[] = {
    {0x47, 0xff, 0x3f}, {0x48, 0xff, 0x1f}, {0x49, 0xff, 0x3f},
    {0x4a, 0xff, 0x1f}, {0x4b, 0xff, 0xff}, {0x4c, 0xff, 0xff},
    {0x40, 0xc0, 0xff}, {0x01, 0x7f, 0x7f}, {0x06, 0x07, 0x07},
};
enum class Status { already_asleep, applied, read_failed, write_failed, verify_failed };
struct Result { Status status; uint8_t reg = 0, before = 0, after = 0; };

// Read every final target before making changes. Do not skip intermediate writes:
// the ES8311 sequence deliberately writes some registers more than once.
template <std::size_t N, class Read, class Write>
Result apply(const Register (&sequence)[N], Read read, Write write) {
    bool asleep = true;
    uint8_t first_reg = 0, first_before = 0, first_after = 0;
    auto final_write = [&](std::size_t i) {
        for (std::size_t j = i + 1; j < N; ++j)
            if (sequence[i].address == sequence[j].address) return false;
        return true;
    };
    for (std::size_t i = 0; i < N; ++i) {
        if (!final_write(i)) continue;
        const auto &r = sequence[i];
        uint8_t value = 0;
        if (!read(r.address, value)) return {Status::read_failed, r.address};
        if ((value & r.mask) != (r.value & r.mask)) {
            if (asleep) { first_reg = r.address; first_before = value; first_after = r.value; }
            asleep = false;
        }
    }
    if (asleep) return {Status::already_asleep};
    for (const auto &r : sequence)
        if (!write(r.address, r.value)) return {Status::write_failed, r.address};
    for (std::size_t i = 0; i < N; ++i) {
        if (!final_write(i)) continue;
        const auto &r = sequence[i];
        uint8_t value = 0;
        if (!read(r.address, value)) return {Status::read_failed, r.address};
        if ((value & r.mask) != (r.value & r.mask))
            return {Status::verify_failed, r.address, value, r.value};
    }
    return {Status::applied, first_reg, first_before, first_after};
}
} // namespace audio_sleep
