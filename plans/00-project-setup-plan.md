# 00 — Tổng quan project `smart-street-lighting` (Mức 1)

> Trạng thái: **đã chốt phạm vi Mức 1** (2026-09-15).
> Chi tiết AI, luật chiếu sáng, đo điện năng, dataset: [01-ai-models-and-dataset.md](01-ai-models-and-dataset.md)
> Linh kiện: [02-linh-kien-can-mua.md](02-linh-kien-can-mua.md)
> Sơ đồ nối dây chi tiết: [03-so-do-noi-day.md](03-so-do-noi-day.md)

## 1. Bối cảnh và phạm vi

Đồ án môn học: **hệ thống chiếu sáng công cộng thông minh, tiết kiệm năng lượng, có sử dụng AI**,
demo trên **mô hình thu nhỏ: 4 đèn, xe và người đồ chơi**.

Bản trước quá nhiều chức năng và quá khó so với thời gian làm đồ án, nên thu gọn về **Mức 1**:
làm ít nhưng chạy chắc, sau đó mới mở rộng.

| Có trong Mức 1 | Không có trong Mức 1 (mở rộng sau) |
|---|---|
| 1 board ESP32-S3 có camera | Board thứ 2, ESP-NOW |
| Model 1: FOMO nhận diện `nguoi` / `xe` trên board | Model AI phát hiện bóng hỏng (autoencoder) |
| Chiếu sáng theo zone và theo loại đối tượng | Cảm biến vật cản FC-51 dự phòng, kiểm tra chéo |
| Chế độ ngày/đêm (BH1750), đèn nền 20% | Telegram bot |
| **Đo dòng/áp từng bóng (INA219), tính điện năng tiêu thụ** | Camera xác nhận đèn sáng |
| Cảnh báo vật dừng lâu **kèm ảnh** trên dashboard | |
| **Dashboard web xem từ xa**: thông số, điện năng từng bóng, chỉnh tay | |

## 2. Các quyết định đã chốt

| Hạng mục | Quyết định | Lý do / ghi chú |
|---|---|---|
| Loại project | TinyML / Edge AI | Model AI chạy trên board |
| Quy mô | **4 đèn = 4 zone** | Đủ để demo, tiết kiệm linh kiện |
| Board | **1 × ESP32-S3 có khe camera** (N16R8 + OV2640), chưa mua | Vừa chạy AI vừa điều khiển đèn; ít dây, dễ debug |
| Nguồn | **2 nguồn riêng, nối chung GND**: sạc dự phòng 5 V ≥ 2 A cho ESP32; adapter 5 V cho 4 đèn | Đèn bật tắt không làm sụt áp ESP32/camera; sạc dự phòng dễ tự tắt nếu chỉ nuôi LED dòng nhỏ |
| Mạch đóng cắt đèn | **ULN2003** (1 IC, dùng 4/7 kênh) | Mở chắc chắn với tín hiệu 3.3 V; ít dây |
| Đo điện năng | **4 × INA219** (1 bóng 1 module), cùng bus I2C | Đo được dòng mA của LED với độ phân giải ~0.1 mA, đo cả áp; I2C không tốn thêm GPIO; 4 địa chỉ bằng pad A0/A1. *ACS712 không dùng: nhiễu lớn hơn tín hiệu LED nhiều lần* |
| Toolchain | **ESP-IDF** | esp32-camera, esp-mqtt, thư viện Edge Impulse |
| Nhận diện | **FOMO (Edge Impulse)**, 2 class `nguoi` / `xe` | Chỉ cần tâm vật thể để biết zone; nhẹ, nhanh, không cần Internet |
| Ra quyết định | Luật (state machine) | Không cần nhãn, an toàn, dễ giải thích |
| Ngày/đêm | BH1750: ngày tắt đèn + không chạy AI; đêm đèn nền 20% | Tiết kiệm điện; đèn nền giúp camera nhìn thấy vật thể |
| Dashboard | **HiveMQ Cloud (MQTT miễn phí) + trang web tĩnh trên GitHub Pages** | Xem từ xa, không cần máy chủ bật liên tục. *Đã cân nhắc Blynk IoT (dễ, có app điện thoại) nhưng không chọn: khó hiện ảnh cảnh báo, gói miễn phí giới hạn số datastream (không đủ cho điện năng 4 bóng), thư viện chủ yếu cho Arduino* |
| Ảnh | **Chỉ gửi khi có cảnh báo vật dừng** | Tiết kiệm băng thông, quota |
| Cấu trúc | `firmware/streetlight-node/` (1 app ESP-IDF) + `dashboard/` (web tĩnh) | |

## 3. Kiến trúc hệ thống

```
┌──────────────────────────────────────────────┐
│ ESP32-S3 CAM (streetlight-node)              │
│  OV2640 → cắt ROI → FOMO → tâm vật thể       │
│  → zone, loại, đứng yên?                     │
│  I2C: BH1750 (ngày/đêm) + 4 × INA219 (điện)  │
│  Luật → PWM 4 kênh → ULN2003 → 4 LED         │
│  Tích luỹ điện năng từng bóng                │
└──────────────────────┬───────────────────────┘
                       │ MQTT/TLS (cổng 8883)
                       ▼
            ┌──────────────────────┐
            │ HiveMQ Cloud (free)  │
            └──────────▲───────────┘
                       │ MQTT qua WebSocket/TLS (cổng 8884)
┌──────────────────────┴───────────────────────┐
│ Dashboard: trang HTML/JS trên GitHub Pages   │
│ mở bằng điện thoại/laptop ở bất kỳ đâu       │
└──────────────────────────────────────────────┘
```

**Mất Internet:** đèn, AI và việc đo điện năng vẫn chạy bình thường (mọi thứ nằm trên board); chỉ dashboard không xem được.

### Nguồn điện và mạch đèn (mỗi bóng i = 1..4)

```
Sạc dự phòng 5 V ──USB-C──► ESP32-S3 CAM ──GPIO (PWM 3.3 V)──► INi ULN2003
                              │ 3.3 V ──► VCC của 4 INA219 + BH1750
                              │ SDA/SCL ─► bus I2C chung (BH1750 + 4 INA219)
                              │ GND
                              └──────────────┬──────────────────────── GND ULN2003 (chân 8), GND các module
                                             │  ◄── BẮT BUỘC nối chung GND
Adapter 5 V (+) ──► [VIN+ INA219 i VIN−] ──► điện trở ──► LED i (anode→cathode) ──► OUTi ULN2003
Adapter 5 V (−) ─────────────────────────────┘
```

- **Bắt buộc nối GND của ESP32 với cực (−) của adapter**; không nối cực (+) 5 V adapter vào chân 5 V của ESP32.
- **INA219 đặt phía +5 V của từng bóng** (nối tiếp trước điện trở): đo dòng qua shunt 0.1 Ω trên module, đo áp tại VIN−.
- **Địa chỉ I2C 4 INA219** bằng pad A0/A1: `0x40` (không hàn), `0x41` (A0), `0x44` (A1), `0x45` (A0 + A1). BH1750 là `0x23`, không trùng. Chạy quét I2C để kiểm tra sau khi hàn.
- ULN2003 kéo cathode LED xuống GND khi IN mức cao; chân COM (9) để trống; IN5–IN7 nối GND.
- Điện trở hạn dòng ≈ (5 − Vf_LED − 0.8) / 0.02 ≈ **56–68 Ω** (đo Vf thực tế rồi chọn).
- **Mỗi bóng 1 điện trở riêng**; anode 4 bóng chung +5 V (qua INA219 riêng), cathode mỗi bóng về 1 chân OUT riêng.

### MQTT topics

| Topic | Chiều | Retained | Nội dung |
|---|---|---|---|
| `streetlight/online` | board → web | ✓ | `online` / `offline` (LWT) |
| `streetlight/state` | board → web | ✓ | JSON mỗi `TELEMETRY_INTERVAL`: ngày/đêm, lux, chế độ, độ sáng 4 đèn, số người/xe, zone có vật, fps |
| `streetlight/config` | board → web | ✓ | JSON **giá trị cài đặt đang áp dụng**: `lux_night`, `lux_day`, `fade_up_ms`, `fade_down_ms`, `night_start`, `night_end` (gửi lại mỗi khi thay đổi) |
| `streetlight/energy` | board → web | ✓ | JSON mỗi `TELEMETRY_INTERVAL`: **từng bóng** dòng, áp, công suất, điện năng hôm nay, tổng, % tiết kiệm; và tổng 4 bóng |
| `streetlight/energy/history` | board → web | ✓ | JSON mỗi giờ: điện năng từng bóng trong **24 giờ gần nhất** (24 giá trị × 4 bóng) |
| `streetlight/alert` | board → web | ✓ | JSON cảnh báo đang mở: `{id, type, zone, class, since, status}`; payload rỗng khi đã đóng |
| `streetlight/alert/image` | board → web | ✓ | Ảnh JPEG (base64) của cảnh báo vật dừng; xoá khi đóng cảnh báo |
| `streetlight/cmd/mode` | web → board | | `auto` / `manual` |
| `streetlight/cmd/lamp` | web → board | | `{"lamp": 3, "duty": 60}` |
| `streetlight/cmd/force_night` | web → board | | `true` / `false` |
| `streetlight/cmd/alert_ack` | web → board | | `{"id": ...}` |
| `streetlight/cmd/config` | web → board | | JSON chỉ chứa **các tham số muốn đổi**, vd `{"lux_night": 40}` hoặc `{"fade_down_ms": 5000}`; `{"reset": true}` để về mặc định |

- Dùng **retained** để mở dashboard lúc nào cũng thấy ngay giá trị mới nhất, kể cả biểu đồ 24 giờ, mà không cần server lưu trữ.
- **Board kiểm tra mọi lệnh nhận được** (lamp 1–4, duty 0–100, JSON hợp lệ, tham số cài đặt trong giới hạn), sai thì bỏ qua.
- **Tham số cài đặt** được board kiểm tra, **lưu NVS** (khởi động lại vẫn giữ), rồi publish lại `streetlight/config`. Dashboard luôn hiển thị theo `streetlight/config`, nên slider phản ánh **giá trị board đang dùng thật**.
- **Chế độ chỉnh tay tự hết hạn** sau `MANUAL_TIMEOUT` (vd 10 phút) → quay về tự động.

### Bảo mật
- Mật khẩu WiFi và tài khoản MQTT của board lưu trong **NVS**, không nằm trong source.
- **Trang web không chứa mật khẩu MQTT** (GitHub Pages công khai): người dùng **nhập tài khoản khi mở trang**.
- Nếu gói miễn phí cho phép, tạo **2 tài khoản MQTT riêng** cho board và dashboard. Không chia sẻ tài khoản dashboard.

### Cần kiểm tra trước khi làm
- **Giới hạn gói miễn phí HiveMQ Cloud:** số kết nối, số tin nhắn/tháng, **kích thước tối đa mỗi tin nhắn** (ảnh base64 khoảng 30–50 KB).
- **Chân GPIO trống trên board camera:** cần **4 chân PWM + 2 chân I2C**. Nếu thiếu → module PCA9685 (PWM qua I2C).

## 4. Logic chiếu sáng (tóm tắt)

- **Ban ngày** → tắt tất cả đèn, không chạy AI.
- **Ban đêm, không có vật thể** → tất cả đèn **20%**.
- **Người** ở zone K → K−1, K, K+1 **100%**, giữ lâu. **Xe** ở zone K → K và zone phía trước **100%**, tắt nhanh.
- **Vật dừng lâu** → đèn quanh đó 100% + **cảnh báo kèm ảnh trên dashboard**.
- **Chuyển độ sáng:** tăng nhanh (~200 ms), giảm từ từ (~3 s); đang giảm mà có vật thể mới thì tăng lại ngay từ mức hiện tại.
- **Camera/model hoặc BH1750 lỗi** → **chế độ theo giờ** + cảnh báo: trước 18:00 (từ 06:00) chỉ cảnh báo, đèn tắt; từ 18:00 đến 06:00 tất cả đèn 100%.
  Giờ lấy từ SNTP; chưa có giờ thì sáng 100%.
- Chi tiết và tham số: mục 3 của [01-ai-models-and-dataset.md](01-ai-models-and-dataset.md).

## 5. Dashboard

| Khu vực | Nội dung |
|---|---|
| Trạng thái | Online/offline, ngày/đêm, chế độ tự động/tay/theo giờ, fps nhận diện |
| **Cường độ ánh sáng** | **Lux hiện tại** (số lớn, cập nhật mỗi 5 s), thanh ngang có đánh dấu vị trí lux hiện tại so với 2 ngưỡng, biểu đồ lux vài phút gần nhất (vẽ trong lúc trang mở) |
| 4 đèn | Độ sáng hiện tại từng đèn, zone đang có người/xe |
| **Điện năng từng bóng** | Bảng 4 bóng: **công suất hiện tại (mW), dòng (mA), điện năng hôm nay (mWh), tổng tích luỹ (mWh), % tiết kiệm**; dòng tổng cộng |
| **Biểu đồ 24 giờ** | Biểu đồ cột điện năng theo giờ, chọn xem từng bóng hoặc cả 4 bóng chồng lên nhau |
| Điều khiển | Công tắc tự động/tay, slider độ sáng từng đèn, nút "ép chế độ đêm" |
| **Cài đặt** | **Slider ngưỡng lux** chuyển sang đêm / về ngày; **slider thời gian sáng dần** và **tối dần**; ô chọn giờ bắt đầu/kết thúc ban đêm (chế độ theo giờ); nút "Khôi phục mặc định" |
| Cảnh báo | **Chỉ hiện khi có cảnh báo**: loại, zone, thời gian, **ảnh** (với vật dừng), nút "Đã xử lý" |

- HTML + JavaScript thuần; `mqtt.js` (kết nối MQTT qua WebSocket) và `Chart.js` (vẽ biểu đồ) tải từ CDN.
- Trang có form nhập tài khoản MQTT; không lưu mật khẩu vào code.

## 6. Cây thư mục

```
smart-street-lighting/
├── .claude/
│   ├── commands/        setup-board, flash-firmware, profile-memory, deploy-model, collect-data
│   └── rules/board-config.md
├── plans/               00 tổng quan, 01 AI + luật + điện năng + dataset, 02 linh kiện, 03 sơ đồ nối dây
├── reference/           datasheet, tài liệu ESP-IDF / Edge Impulse / HiveMQ
├── todo/
├── firmware/
│   ├── components/      driver BH1750, INA219, PWM đèn, logic zone, đo điện năng, MQTT client
│   └── streetlight-node/ app ESP-IDF duy nhất
├── ml/
│   ├── vision/          ghi chú dataset + project Edge Impulse
│   ├── sensor-models/   chưa dùng ở Mức 1
│   └── data/
├── dashboard/           trang web tĩnh (GitHub Pages)
├── test/  docs/
├── .gitignore
└── README.md
```

## 7. Các giai đoạn thực hiện

Mỗi giai đoạn xong là **có sản phẩm chạy được**, hết thời gian ở giai đoạn nào vẫn có cái để demo.

| # | Giai đoạn | Kết quả kiểm tra |
|---|---|---|
| 1 | Camera + FOMO mẫu trên ESP32-S3 | Chạy được model, đo fps, ảnh ở đèn 20% còn nhận ra đồ chơi, không bị sọc do PWM |
| 2 | PWM 4 đèn qua ULN2003 + BH1750 + luật ngày/đêm + luật zone (giả lập vật thể bằng lệnh serial) | Đèn đổi đúng mức sáng |
| 3 | 4 × INA219: đọc dòng/áp, tích luỹ điện năng, hiệu chỉnh `P100` từng bóng | Số đo khớp đồng hồ vạn năng (sai lệch < 5–10%) |
| 4 | Thu dataset, train Model 1 trên Edge Impulse, ghép vào luật | Đẩy đồ chơi → đèn sáng theo đúng zone |
| 5 | MQTT HiveMQ Cloud + dashboard (trạng thái, điện năng từng bóng, biểu đồ, chỉnh tay) | Xem và chỉnh đèn từ điện thoại dùng 4G |
| 6 | Cảnh báo vật dừng + ảnh trên dashboard | Để đồ chơi đứng yên → dashboard hiện ảnh |

## Lịch sử thay đổi

- 2026-09-14 → 2026-09-15: thiết kế bản đầy đủ (2 board, 2 model, FC-51, Telegram, ESP-NOW...) — quá nhiều chức năng, quá khó.
- 2026-09-15: **thu gọn về Mức 1**: 1 board ESP32-S3 CAM, FOMO `nguoi`/`xe`, BH1750, luật zone, cảnh báo vật dừng kèm ảnh,
  dashboard web tĩnh + HiveMQ Cloud. Gộp firmware thành `firmware/streetlight-node/`.
- 2026-09-15: 2 nguồn riêng (sạc dự phòng cho ESP32, adapter 5 V cho đèn, nối chung GND); mạch đóng cắt ULN2003.
- 2026-09-15: **giảm còn 4 đèn**; thêm **4 × INA219 đo dòng/áp từng bóng**, dashboard hiện điện năng từng bóng + biểu đồ 24 giờ;
  xe → sáng zone K và **1** zone phía trước (vì chỉ còn 4 zone).
- 2026-09-15: đã cân nhắc Blynk, giữ HiveMQ + web tự viết. Camera/model hoặc BH1750 lỗi → **chế độ theo giờ** (06–18h chỉ cảnh báo, 18–06h sáng 100%).
  Chuyển độ sáng tăng nhanh, giảm từ từ.
- 2026-09-15: dashboard thêm **hiển thị lux**, **slider ngưỡng lux**, **slider thời gian sáng dần/tối dần**, giờ ban đêm; topic `streetlight/config` và `streetlight/cmd/config`.
