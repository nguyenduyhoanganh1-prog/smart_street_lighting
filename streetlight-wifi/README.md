# streetlight-wifi

Project ESP32-S3 độc lập, giống project chính nhưng **bỏ camera/AI và MQTT**. Board tự phát WiFi
(chế độ AP) và chạy một trang web giống dashboard: xem số đo trực tiếp và chỉnh thông số đèn
từ điện thoại hoặc laptop. Không cần router hay Internet.

## Cách dùng

```
cd streetlight-wifi
idf.py set-target esp32s3
idf.py -p COM15 flash monitor
```

1. Kết nối WiFi **`StreetLight-Tune`**, mật khẩu **`12345678`** (đổi trong `main/tune_config.h`).
2. Mở trình duyệt: **http://192.168.4.1** (điện thoại báo "không có Internet" thì chọn giữ kết nối).

## Trang web

| Phần | Nội dung |
|---|---|
| Trạng thái | lux (BH1750), ngày/đêm, chế độ, giờ trên board, thời gian chạy |
| Bốn đèn | độ sáng %, V / I / P (INA219), điện năng hôm nay + tổng, % tiết kiệm, cảnh báo lỗi bóng |
| Điều khiển | chế độ chỉnh tay (kéo độ sáng từng đèn, tự về tự động sau 10 phút), ép chế độ đêm |
| Cài đặt | ngưỡng lux đêm/ngày, thời gian sáng dần/tối dần, giờ ban đêm, khôi phục mặc định |

Cài đặt lưu trong NVS nên giữ lại sau khi mất điện. Giới hạn và luật kiểm tra giống dashboard
(`sl_codec_apply_config`).

## Phần cứng (xem `main/app_config.h`)

| Thứ | Chân / địa chỉ |
|---|---|
| I2C | SDA/SCL theo `APP_I2C_SDA_GPIO` / `APP_I2C_SCL_GPIO`, 100 kHz, pull-up nội bật |
| BH1750 | 0x23 |
| 4 × INA219 | 0x40, 0x41, 0x44, 0x45 (shunt 0.1 Ω, ±40 mV, trung bình 128 mẫu) |
| 4 đèn | GPIO 1, 14, 21, 47 → ULN2003 IN1..IN4, PWM 2 kHz |

PWM 2 kHz vì ULN2003 (Darlington) chuyển mạch chậm. Nếu chân I2C trong file không khớp với
board thì bus scan trong log serial sẽ không thấy thiết bị nào: kiểm tra `app_config.h` trước.

## Cấu trúc

```
streetlight-wifi/
  main/main.c          vòng lặp 200 ms + log serial
  main/node_ctrl.c     cảm biến -> luật đèn -> PWM -> điện năng (có mutex)
  main/lighting.c      luật ngày/đêm, chế độ theo giờ (logic thuần, test được trên PC)
  main/energy_*.c      tích phân điện năng, thống kê, % tiết kiệm
  main/node_store.c    lưu cấu hình + điện năng vào NVS
  main/sensor_hub.c    bus I2C, BH1750 + 4 INA219, tự khởi tạo lại khi cắm lại
  main/wifi_ap.c       phát WiFi AP
  main/web_server.c    HTTP: GET /, GET /api/status, POST /api/cmd/<name>, POST /api/time
  main/web/index.html  trang web (nhúng vào firmware)
  main/tune_config.h   SSID / mật khẩu AP
  components/          bh1750, ina219, lamp_pwm, sl_codec (JSON + kiểm tra lệnh)
```

Project tự chứa toàn bộ code, không phụ thuộc thư mục `firmware/`. Sửa lỗi ở `firmware/` thì
cần sửa lại ở đây.

## Lưu ý

- **Giờ:** không có Internet nên không có SNTP. Trình duyệt gửi giờ của máy bạn khi mở trang;
  chưa mở trang thì board coi là chưa có giờ.
- **Không chạy cùng lúc** với `firmware/streetlight-node`: hai app dùng chung phần cứng.
- **Bảo mật:** API không có đăng nhập, chỉ dựa vào mật khẩu WiFi AP. Đổi mật khẩu mặc định khi dùng thật.
- **Trạng thái:** đã build thành công với ESP-IDF v5.5.4 (esp32s3), chưa kiểm tra trên board thật.
