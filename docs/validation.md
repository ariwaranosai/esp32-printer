# 验证记录

## 2026-09-18：天气界面与实机验证

- 按批准的天气相框布局实现：照片框 (24,140,432,576)、顶部室外天气、底部室内温湿度与采样时间。每小时更新；旧 SD 配置的 300 秒被提升为 3600 秒。
- `weather_adcode` 填六位行政区划码时优先使用；空值按公网 IP 定位。
- 天气解析测试覆盖数值/字符串温度、缺失与错误响应、无效日期、相对发布时间、错误时保留缓存，以及 adcode 查询选择。
- 本轮 Windows 主机测试使用 w64devkit GCC（未启用 sanitizers）：生产渲染器几何/边界、图片兼容性、8 项转换工具测试、天气解析与真实 API 响应测试均通过。CI 保留 ASan/UBSan，并增加天气解析测试。
- 检查了正常、未更新缓存、无天气与长文本预览；`weather-firmware-preview.png` 是生产 C++ 渲染器输出，数值为排版示例。
- ESP-IDF v5.5.2 构建、链接及 4 MiB 应用分区大小检查通过。本机复用了 GCC esp-15.2.0 工具链，通过 IDF_MAINTAINER 放行版本检查；并非 IDF 推荐的 esp-14.2.0 工具链组合。
- COM6 上的 ESP32-S3 已写入新应用并通过哈希校验。启动日志确认：SD 挂载、3600 秒周期、HTTPS 天气请求成功（杭州市、多云、24°C）、SHTC3 29.7°C / 31.2% RH。
- 本轮未等待完整一小时验证定时唤醒，未测量休眠电流，也未通过相机核对实屏色彩。启动日志保存于本地 `build-v5/weather-device.log`，不提交到仓库。

## 2026-09-16：初始版本历史记录

## 已完成

- ESP-IDF v5.5.2，ESP32-S3，16 MB Flash、Octal 8 MB PSRAM 配置下，完整编译和链接成功。
- 同一份生产 C++ 渲染/解码逻辑，在 macOS clang AddressSanitizer + UndefinedBehaviorSanitizer 下通过测试。
- UTC 日期转换验证：闰日、无效闰日、指定日期与 epoch 对照；不依赖宿主时区和 ESP-IDF 缺失的 `timegm`。
- 图片：800×480、480×800、432×576、431×575、1×1、736×1325、1350×1350，分别覆盖 PNG/JPEG/BMP 和 cover/contain。
- PNG RGB/RGBA/灰度/灰度透明/调色板透明，渐进 JPEG，JPEG EXIF 1–8 与 Pillow 物理旋转结果逐像素对照。
- 空文件、截断 JPEG、异常 BMP、异常 EXIF、超大边长、超大总像素、超过 8 MiB 文件均被安全拒绝。
- 旋转后 192000 字节缓冲逐像素反查；照片绘制不会修改照片框外的像素。
- `docs/firmware-preview.png` 来自测试程序调用生产代码；已检查中文、状态栏、照片边界及底栏。

## 代码审查

审查了输入图像尺寸/通道、EXIF 有界读取、照片与整屏坐标分离、六色色码、SPI DMA 内存要求、忙信号超时、CRC、RTC 无效日期、网络配置失败、深睡眠唤醒与按键常按保护。修正了：字体默认字重过细、RTC 的 `timegm` 可移植性、解码输出通道判断和唤醒后的 RTC GPIO 复用。

## 未完成的硬件验证

没有连接实物设备，因此不能据编译或主机测试声称已经上屏成功。实机验收项目见 [design.md](design.md)。尤其需要核对面板方向、E6 色彩、PMIC/RTC 的具体板卡版本、充电状态与实际深睡眠电流。

图像的 2 MP 检查是输入上限；设备的可用 PSRAM、格式及压缩方式仍影响解码峰值。大图若内存不足会跳过，推荐提前转换到 432×576。


## 2026-09-19 NAS、节电与按键实机验收

- ESP-IDF 5.5.2 编译完成，最新应用镜像 2,765,360 字节；已通过 USB 刷入并校验。沿用本机 esp-15.2.0_20251204 工具链（并非 IDF 5.5.2 推荐的 esp-14.2 工具链）。
- 接入 NAS Bearer 鉴权取图，每 3 小时更换照片，每小时更新天气和传感器；失败保留 SD 缓存。新增电量百分比、KEY 长按换图及默认方向翻转 180°。
- 主机 ASan/UBSan 测试覆盖图片兼容性、天气、下载错误、缓存恢复、六色缓存一致性/损坏重建、UI 隔离、每日校时及 PMIC 电源位保护。
- 首次尝试关闭 ALDO3 后，实机 RTC/SHTC3 通信异常；已修正为保留 ALDO3，完整关机重启后恢复。休眠关闭 ALDO4，卸载 SD 并释放总线，保留主电源和 RTC。不能仅凭原理图将 ALDO3 视为可关闭的未用音频电源。
- 修正版启动日志：SHTC3 28.9°C / 33.6% RH，照片六色缓存命中 156 ms，时钟有效，面板旋转 180°；没有再出现 PMIC/RTC 初始化错误。
- 用户确认当前显示方向、运行及功能均无问题。尚未测量整板休眠电流或长期续航，不承诺具体省电比例。
- NAS 密钥保存在被忽略的本地私有头文件；带密钥的本地二进制、SD 私有配置、设备日志和私人照片均不提交。


## 2026-09-19 Audio codec software standby (awaiting hardware validation)

- Added boot-time ES8311/ES7210 standby with bounded I2C operations and final register readback; ALDO3 stays enabled and GPIO7 keeps the amplifier off.
- Focused C++ tests passed under AddressSanitizer/UndefinedBehaviorSanitizer: repeated wake/idempotence, ordered transient writes, read failure before changes, interrupted writes, ignored writes, readback failure and verification masks.
- ESP-IDF 5.5.2 build passed. The existing local esp-15.2.0 toolchain override remains in use; IDF still warns that its supported version is esp-14.2.0_20251107.
- No target USB serial port was present (only COM3/COM4). This firmware has not yet been flashed or verified on the device. RTC/SHTC3/SD/photo regression, cold boot/deep-sleep wake, and battery-side current measurements remain pending.


## 2026-09-19 Three-hour refresh and quiet hours (awaiting hardware validation)

- Automatic weather/sensor/display refresh defaults to 10800 seconds, matching the default NAS photo interval. Existing shorter `refresh_seconds` values are raised to 10800 without changing SD Wi-Fi credentials.
- Quiet hours default to local 00:00 inclusive through 07:00 exclusive. A valid clock skips Wi-Fi, sensor sampling and panel refresh during that interval, scheduling the next wake at its end. A manual next-photo request bypasses the pause. Unknown clocks may connect for NTP; quiet hours are rechecked after sync and before panel refresh.
- The ASan/UBSan scheduling tests passed: exact boundaries, overnight windows, year rollover, disabled/equal hours, manual override, invalid clock, and both DST transitions.
- ESP-IDF 5.5.2 incremental build passed with the existing compiler override. Only COM3/COM4 were present; this combined firmware is not flashed yet, and actual power savings remain unmeasured.


### Audio standby hardware check and readback correction

- Initial combined firmware (2769056 bytes) was flashed on COM6 and esptool verified its hash. Boot logs reported ES8311 already in standby.
- ES7210 initially logged a verification mismatch at 0x47 (0x3f read vs 0xff written). Full ES7210 datasheet revision 21 confirms reserved bits 7:6 at 0x47/0x49 and 7:5 at 0x48/0x4a. Verification now uses 0x3f/0x1f masks while retaining the vendor power-down write sequence.
- Added a regression test modeling those reserved bits and checking that an enabled MICBIAS still triggers shutdown; ASan/UBSan tests passed. The corrected ESP-IDF 5.5.2 firmware is compiled; a repeat flash/hardware check is pending. A short USB-log drain before deep sleep preserves final diagnostics.

- Corrected firmware subsequently flashed successfully: 2769072 bytes at 0x10000, esptool hash verified. Boot reported ELF SHA256 prefix `0a5903ba6` and ESP-IDF 5.5.2. USB disconnected after `app_main`; the captured boot log alone does not establish standby, RTC/SHTC3 or display validation. Manual-wake diagnostics remain pending.

- Corrected firmware manual KEY wake was then observed: both `ES8311 standby already verified` and `ES7210 standby already verified`; long press detected; SD mounted with 10800-second refresh interval; NAS returned a verified 112024-byte 432x576 JPEG; SHTC3 read 29.9 C / 42.0% RH; valid display time 00:32:04 and 180-degree panel rotation. This confirms the manual override during quiet hours and intact sensor communication after audio standby. USB disconnected later; the final panel-completion/sleep log and the scheduled 07:00 wake were not captured. Battery-side current remains unmeasured.


## 2026-09-19 Photo clarity processing (preview, not deployed)

- NAS: final-size luminance gamma 0.96, contrast 1.05 and unsharp radius 0.8 / percent 80 / threshold 3; configurable and disableable. Existing JPEG geometry/quality/API are retained.
- Firmware: serpentine Floyd-Steinberg diffusion with mirrored neighbor weights; photo-cache format version advanced to invalidate old renders. Panel palette and UI layout unchanged.
- All 15 service tests passed, including byte-identical legacy output with enhancement disabled, edge contrast, neutral/flat images, invalid settings, white contain padding and existing API/Postgres tests (not the live-database suite).
- `tests/run.sh` passed, including ASan/UBSan frame bounds/EXIF tests and old-cache invalidation. ESP-IDF 5.5.2 build passed using the existing compiler override.
- Comparison uses `sdcard/photos/00-landscape.png`, identical cover crop, the saved pre-change renderer and current firmware renderer. Artifacts: `host-build/clarity/comparison.png`, before/after JPEGs and six-color PNGs. Preview is ideal RGB, not a photograph of the panel. No quantified perceptual improvement is claimed.
- This build has not been flashed. NAS source changes have not been built/published as a new Docker image or deployed. Existing device/NAS retain the previous implementation until updated.

- Clarity image built and five image-processing tests passed inside the non-root/read-only container. Published `nkssai/esp32-printer-nas:20260919-clarity`, digest `sha256:e53b641fb9ad1d37dd674404c1597e68589c881a91c18fefc2405a5a8da8b228`. NAS deployment is user-managed. Firmware flashing wait was cancelled at the user request before a new flash; the installed firmware is unchanged.


## 2026-09-19 Approved top-information layout

- Promoted the approved preview to firmware: 456x656 photo at (12,132), all information above it, 12px side/bottom margins. Cache magic advanced for new geometry. Local preparation tool uses new dimensions.
- All host tests passed (ASan/UBSan layout bounds, UI/photo isolation, original and native JPEG client inputs, cache invalidation, image compatibility, eight preparation tests). Sixteen NAS unit tests passed; eight API tests also passed inside the final non-root read-only Docker image.
- ESP-IDF 5.5.2 build passed with existing compiler override. Published `nkssai/esp32-printer-nas:20260919-topinfo`, digest `sha256:4f96006d9c8c1e2148b9e9d58e245f39d081e3aa2fa3fbaf6e3c6922151da965`. Container deployment is user-managed. Device flash pending wake.

- Final top-information firmware flashed: 2769264 bytes at 0x10000, write hash verified. Manual wake fetched a verified 159198-byte JPEG, rendered in 8743ms, read SHTC3 28.5 C / 43.5% RH, and started panel refresh at valid local time 01:10:04 with 180-degree orientation. An independent authenticated request to the user-updated NAS returned HTTP 200 and 456x656 JPEG, confirming native-size service support. USB disconnected later; final display-completion/deep-sleep diagnostics were not captured.
