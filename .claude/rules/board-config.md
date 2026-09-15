# Board Configuration

Mức 1: **1 board ESP32-S3 có camera** vừa chạy AI vừa điều khiển **4 đèn** và đo điện năng từng bóng.

- Board / MCU: ESP32-S3 có khe camera, module chưa chọn `<điền sau khi mua>`
  (đề xuất: N16R8 kèm OV2640, vd Freenove ESP32-S3-WROOM CAM)
- CPU: Xtensa LX7 dual-core, tối đa 240 MHz
- RAM: 512 KB SRAM nội + PSRAM `<điền theo module, vd 8 MB>`
- Flash: `<điền theo module, vd 16 MB>`
- FPU: có (single-precision)
- Clock: 240 MHz
- Power source: **2 nguồn riêng, nối chung GND** — ESP32: sạc dự phòng 5 V ≥ 2 A qua USB-C; 4 đèn: adapter 5 V ≥ 1 A. Không đo áp pin.
- Toolchain: ESP-IDF (`idf.py`), esp32-camera, esp-mqtt, thư viện C++ xuất từ Edge Impulse
- Peripherals:
  - DVP camera OV2640 (RGB565/GRAYSCALE, tắt AEC/AGC), PSRAM cho frame buffer
  - LEDC PWM 4 kênh, 5 kHz → ULN2003 (low-side sink) → 4 LED trên nguồn 5 V riêng (hoặc PCA9685 nếu thiếu GPIO) `<xác nhận chân trống>`
  - I2C (1 bus): BH1750 `0x23` (ngày/đêm); 4 × INA219 `0x40–0x43` (đo dòng/áp từng bóng, shunt 0.1 Ω, trung bình 128 mẫu)
  - WiFi: MQTT/TLS tới HiveMQ Cloud
- Secrets (NVS, không commit): WiFi, tài khoản MQTT. NVS còn lưu: `P100_i`, điện năng tích luỹ, lịch sử 24 giờ.

## AI trên board

- Model 1: FOMO (Edge Impulse), int8, 2 class `nguoi` / `xe`. Input: ROI mặt đường thu nhỏ về 96×96 hoặc 160×160.
- Hậu xử lý: tracking tâm vật thể, zone (4), hướng, phát hiện vật dừng (gửi ảnh JPEG khi có cảnh báo).
- Luật: ngày tắt đèn và dừng AI; đêm nền 20%; người → K−1..K+1 100%; xe → K + 1 zone phía trước 100%; camera lỗi → tất cả 100%.
- Chi tiết: plans/01-ai-models-and-dataset.md.

> File này được các rule chung ở ~/.claude/rules/ecc/embedded/*.md tham chiếu tới
> để quyết định quantization (int8 vs float32), resource budget, sleep mode, v.v.
> Cập nhật lại nếu đổi board.
