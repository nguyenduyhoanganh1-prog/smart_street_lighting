# 00 — Tổng quan project `smart-street-lighting` (Mức 1)

> Trạng thái: **đã chốt phạm vi Mức 1** (2026-09-15).
> Chi tiết AI, luật chiếu sáng, dataset: [01-ai-models-and-dataset.md](01-ai-models-and-dataset.md)
> Linh kiện: [02-linh-kien-can-mua.md](02-linh-kien-can-mua.md)

## 1. Bối cảnh và phạm vi

Đồ án môn học: **hệ thống chiếu sáng công cộng thông minh, tiết kiệm năng lượng, có sử dụng AI**,
demo trên **mô hình thu nhỏ: 6 đèn, xe và người đồ chơi**.

Bản trước quá nhiều chức năng và quá khó so với thời gian làm đồ án, nên thu gọn về **Mức 1**:
làm ít nhưng chạy chắc, sau đó mới mở rộng.

| Có trong Mức 1 | Không có trong Mức 1 (mở rộng sau) |
|---|---|
| 1 board ESP32-S3 có camera | Board thứ 2, ESP-NOW |
| Model 1: FOMO nhận diện `nguoi` / `xe` trên board | Model phát hiện bóng hỏng (INA219, autoencoder) |
| Chiếu sáng theo zone và theo loại đối tượng | Cảm biến vật cản FC-51 dự phòng, kiểm tra chéo |
| Chế độ ngày/đêm (BH1750), đèn nền 20% | Telegram bot |
| Cảnh báo vật dừng lâu **kèm ảnh** trên dashboard | Camera xác nhận đèn sáng |
| **Dashboard web xem từ xa**, chỉnh tay | |

## 2. Các quyết định đã chốt

| Hạng mục | Quyết định | Lý do / ghi chú |
|---|---|---|
| Loại project | TinyML / Edge AI | Model AI chạy trên board |
| Board | **1 × ESP32-S3 có khe camera** (N16R8 + OV2640), chưa mua | Vừa chạy AI vừa điều khiển đèn; ít dây, ít pin, dễ debug |
| Nguồn | **2 nguồn riêng, nối chung GND**: sạc dự phòng 5 V ≥ 2 A cho ESP32; adapter 5 V cho 6 đèn | Đèn bật tắt không làm sụt áp ESP32/camera; sạc dự phòng dễ tự tắt nếu chỉ nuôi LED dòng nhỏ |
| Mạch đóng cắt đèn | **ULN2003** (1 IC, 7 kênh) | 2N7000 có thể không mở chắc chắn với tín hiệu 3.3 V của ESP32; ULN2003 ít dây, cắm breadboard được |
| Toolchain | **ESP-IDF** | esp32-camera, esp-mqtt, thư viện Edge Impulse |
| Nhận diện | **FOMO (Edge Impulse)**, 2 class `nguoi` / `xe` | Chỉ cần tâm vật thể để biết zone; nhẹ, nhanh, không cần Internet |
| Ra quyết định | Luật (state machine) | Không cần nhãn, an toàn, dễ giải thích |
| Ngày/đêm | BH1750: ngày tắt đèn + không chạy AI; đêm đèn nền 20% | Tiết kiệm điện; đèn nền giúp camera nhìn thấy vật thể |
| Dashboard | **HiveMQ Cloud (MQTT miễn phí) + trang web tĩnh trên GitHub Pages** | Xem từ xa, không cần máy chủ bật liên tục, toàn quyền giao diện |
| Ảnh | **Chỉ gửi khi có cảnh báo vật dừng**, không xem camera liên tục | Tiết kiệm băng thông, pin, quota |
| Cấu trúc | `firmware/streetlight-node/` (1 app ESP-IDF) + `dashboard/` (web tĩnh) | |

## 3. Kiến trúc hệ thống

```
┌──────────────────────────────────────────────┐
│ ESP32-S3 CAM (streetlight-node)              │
│  OV2640 → cắt ROI → FOMO → tâm vật thể       │
│  → zone, loại, đứng yên?                     │
│  BH1750 → ngày/đêm                           │
│  Luật → PWM 6 kênh → ULN2003 → 6 LED         │
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

**Mất Internet:** đèn và AI vẫn chạy bình thường (mọi thứ nằm trên board); chỉ dashboard không xem được.

### Nguồn điện

```
Sạc dự phòng 5 V ──USB-C──► ESP32-S3 CAM ──GPIO (PWM 3.3 V)──► IN1..IN6 ULN2003
                              │ GND                                 │ GND (chân 8)
                              └──────────────┬──────────────────────┘
                                             │  ◄── BẮT BUỘC nối chung GND
Adapter 5 V (+) ──► điện trở ──► LED (anode→cathode) ──► OUT1..OUT6 ULN2003
Adapter 5 V (−) ─────────────────────────────┘
```

- **Bắt buộc nối GND của ESP32 với GND (−) của adapter.** Tín hiệu PWM 3.3 V chỉ có nghĩa khi hai bên có chung mốc 0 V; không nối thì đèn không bật hoặc chập chờn.
- **Không nối 5 V của adapter vào chân 5 V của ESP32** (đang cấp từ sạc dự phòng): hai nguồn 5 V nối thẳng vào nhau có thể đẩy dòng ngược vào sạc/adapter.
- ULN2003 **kéo cathode LED xuống GND** khi chân IN ở mức cao (kiểu *low-side sink*); chân COM (chân 9) để trống vì LED không cần diode chống ngược.
- Điện trở hạn dòng: `R = (5 V − Vf_LED − V_ULN2003) / I_LED` ≈ (5 − 3.1 − 0.8) / 0.02 ≈ **56 Ω**. Đo lại Vf thực tế rồi chọn giá trị gần nhất (56–68 Ω).
- Adapter **5 V ≥ 1 A** là dư: 6 LED × 20 mA ≈ 120 mA. Đặt 1 tụ 100 µF gần ULN2003 trên đường 5 V của đèn.
- Nguồn đèn là adapter cắm điện nên **không cần đo áp pin**. Sạc dự phòng cho ESP32 cũng không đo được áp, nên Mức 1 **không có % pin trên dashboard**.

### MQTT topics

| Topic | Chiều | Retained | Nội dung |
|---|---|---|---|
| `streetlight/online` | board → web | ✓ | `online` / `offline` (LWT: broker tự báo `offline` khi board mất kết nối) |
| `streetlight/state` | board → web | ✓ | JSON mỗi `TELEMETRY_INTERVAL`: ngày/đêm, lux, chế độ, độ sáng 6 đèn, số người/xe, zone có vật, % điện tiết kiệm, fps |
| `streetlight/alert` | board → web | ✓ | JSON cảnh báo đang mở: `{id, type, zone, class, since, status}`; payload rỗng khi đã đóng |
| `streetlight/alert/image` | board → web | ✓ | Ảnh JPEG (base64) của cảnh báo vật dừng; xoá khi đóng cảnh báo |
| `streetlight/cmd/mode` | web → board | | `auto` / `manual` |
| `streetlight/cmd/lamp` | web → board | | `{"lamp": 3, "duty": 60}` |
| `streetlight/cmd/force_night` | web → board | | `true` / `false` |
| `streetlight/cmd/alert_ack` | web → board | | `{"id": ...}` người quản lý đã xem / đã xử lý |

- Dùng **retained** cho trạng thái và cảnh báo, để mở dashboard lúc nào cũng thấy ngay giá trị mới nhất.
- **Board kiểm tra mọi lệnh nhận được** (lamp 1–6, duty 0–100, JSON hợp lệ), sai thì bỏ qua.
- **Chế độ chỉnh tay tự hết hạn** sau `MANUAL_TIMEOUT` (vd 10 phút) không có lệnh mới → quay về tự động, tránh quên để đèn ở chế độ tay.

### Bảo mật
- Mật khẩu WiFi và tài khoản MQTT của board lưu trong **NVS**, không nằm trong source.
- **Trang web không chứa mật khẩu MQTT**: GitHub Pages là công khai, nên người dùng **nhập tài khoản khi mở trang**.
- Nếu gói miễn phí cho phép, tạo **2 tài khoản MQTT riêng** cho board và dashboard.
- Ai có tài khoản dashboard đều điều khiển được đèn → không chia sẻ tài khoản.

### Cần kiểm tra trước khi làm
- **Giới hạn gói miễn phí HiveMQ Cloud:** số kết nối, số tin nhắn/tháng, **kích thước tối đa mỗi tin nhắn** (ảnh base64 khoảng 30–50 KB).
- **Chân GPIO trống trên board camera:** camera chiếm nhiều chân; cần 6 chân PWM + 2 chân I2C.
  Nếu thiếu → dùng module **PCA9685** (16 kênh PWM qua I2C, dùng chung 2 chân với BH1750).

## 4. Logic chiếu sáng (tóm tắt)

- **Ban ngày** → tắt tất cả đèn, không chạy AI.
- **Ban đêm, không có vật thể** → tất cả đèn **20%**.
- **Người** ở zone K → K−1, K, K+1 **100%**, giữ lâu. **Xe** ở zone K → K và 2 zone phía trước **100%**, tắt nhanh.
- **Vật dừng lâu** → đèn quanh đó 100% + **cảnh báo kèm ảnh trên dashboard**.
- **Camera/model lỗi** → tất cả đèn 100% + cảnh báo. **BH1750 lỗi** → coi là đêm + cảnh báo.
- Chi tiết và tham số: mục 3 của [01-ai-models-and-dataset.md](01-ai-models-and-dataset.md).

## 5. Dashboard

| Khu vực | Nội dung |
|---|---|
| Trạng thái | Online/offline, ngày/đêm + lux, chế độ tự động/tay, fps nhận diện |
| 6 đèn | Độ sáng hiện tại từng đèn, zone đang có người/xe |
| Tiết kiệm điện | % điện năng tiết kiệm **ước tính** so với sáng 100% suốt đêm |
| Điều khiển | Công tắc tự động/tay, slider độ sáng từng đèn, nút "ép chế độ đêm" |
| Cảnh báo | **Chỉ hiện khi có cảnh báo**: loại, zone, thời gian, **ảnh** (với vật dừng), nút "Đã xử lý" |

- Viết bằng HTML + JavaScript thuần, dùng thư viện `mqtt.js` (tải từ CDN) để kết nối MQTT qua WebSocket.
- Trang có form nhập tài khoản MQTT; không lưu mật khẩu vào code.

## 6. Cây thư mục

```
smart-street-lighting/
├── .claude/
│   ├── commands/        setup-board, flash-firmware, profile-memory, deploy-model, collect-data
│   └── rules/board-config.md
├── plans/               00 tổng quan, 01 AI + luật + dataset, 02 linh kiện
├── reference/           datasheet, tài liệu ESP-IDF / Edge Impulse / HiveMQ
├── todo/
├── firmware/
│   ├── components/      driver BH1750, PWM đèn, logic zone, MQTT client
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
| 1 | Camera + FOMO mẫu trên ESP32-S3 | Chạy được model, đo fps, ảnh ở đèn 20% còn nhận ra đồ chơi |
| 2 | PWM 6 đèn + BH1750 + luật ngày/đêm + luật zone (giả lập vật thể bằng lệnh serial) | Đèn đổi đúng mức sáng |
| 3 | Thu dataset, train Model 1 trên Edge Impulse, ghép vào luật | Đẩy đồ chơi → đèn sáng theo đúng zone |
| 4 | MQTT HiveMQ Cloud + dashboard (trạng thái, chỉnh tay) | Xem và chỉnh đèn từ điện thoại dùng 4G |
| 5 | Cảnh báo vật dừng + ảnh trên dashboard | Để đồ chơi đứng yên → dashboard hiện ảnh |

## Lịch sử thay đổi

- 2026-09-14 → 2026-09-15: thiết kế bản đầy đủ (2 board, 2 model, FC-51, Telegram, ESP-NOW...) — quá nhiều chức năng, quá khó.
- 2026-09-15: **thu gọn về Mức 1**: 1 board ESP32-S3 CAM, FOMO `nguoi`/`xe`, BH1750, luật zone, cảnh báo vật dừng kèm ảnh,
  dashboard web tĩnh + HiveMQ Cloud. Gộp firmware thành `firmware/streetlight-node/`.
- 2026-09-15: chốt **6 đèn**; **2 nguồn riêng** (sạc dự phòng cho ESP32, adapter 5 V cho đèn, nối chung GND);
  đổi mạch đóng cắt từ 2N7000 sang **ULN2003**; bỏ đo áp pin.
