# Audio standby

This photo-frame firmware does not use audio. At every boot, after enabling
Audio_VCC and driving amplifier shutdown GPIO7 low, it checks the ES8311 (7-bit
I2C address `0x18`) and ES7210 (`0x40`) standby registers. If needed, it executes
their software power-down sequences and reads back the final settings. Existing
standby settings are left intact across deep-sleep wakeups. No I2S driver or
audio clock is started.

**Keep ALDO3 enabled.** Disabling this rail previously caused RTC and SHTC3
communication failures on the actual PhotoPainter. These changes only address
the two codecs, leaving the shared I2C bus and PMIC rail settings in place.

Probe, read, write and verification errors are bounded and logged, not fatal to
the frame. A failed initial register read prevents writes to that codec. A
failed write stops its sequence; it may leave a partially suspended codec, so
the next boot checks again. No success log is emitted without readback.
ES8311 reset/transient and reserved bits are excluded from verification masks;
only each register's final write is checked. ES7210 registers 0x47/0x49 use
mask 0x3f; 0x48/0x4a use 0x1f, excluding reserved bits that read zero.

## Sources

- [Waveshare board configuration](https://github.com/waveshareteam/ESP32-S3-PhotoPainter/blob/a5e8f757ba0cafbb5586f07d3e83bda3184c0845/01_Example/xiaozhi-esp32/main/boards/waveshare-s3-PhotoPainter/config.h).
- [Espressif ES8311 driver](https://github.com/espressif/esp-audio-dev/blob/master/esp_codec_dev/device/es8311/es8311.c): `es8311_power_down`.
- [Espressif ES7210 driver](https://github.com/espressif/esp-audio-dev/blob/master/esp_codec_dev/device/es7210/es7210.c): `es7210_stop`.
- [ES8311 register definitions](https://files.waveshare.com/wiki/common/ES8311.DS.pdf).

Sequences reviewed on 2026-09-19. Adapted register tables are covered by
`third_party/Espressif-codec-LICENSE.txt` (Espressif products only).

- [ES7210 full register definitions](https://files.waveshare.com/wiki/common/ES7210_DS.pdf), revision 21, registers 0x47-0x4a.

## Hardware validation

After flashing, check for both `ES8311 standby verified` and `ES7210 standby
verified` (or `standby already verified`). Confirm RTC, SHTC3, battery percentage,
SD and photo refresh still work. Repeat after a deep-sleep wake and a full power
cycle. Register verification proves configuration, not power consumption:
measure battery-side sleep current before/after with USB disconnected. Some
codec blocks already power down by default; no battery-life gain is assumed.
