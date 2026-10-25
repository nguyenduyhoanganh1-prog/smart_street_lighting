# tune-by-wifi

ESP32-S3 tự phát WiFi (chế độ AP) và chạy một trang web giống dashboard: xem số đo trực tiếp
và chỉnh thông số đèn từ điện thoại/laptop. Không cần router, Internet hay MQTT.

## Cách dùng

1. Build và nạp (cần ESP-IDF v5.x):
   ```
   cd tune-by-wifi
   idf.py set-target esp32s3
   idf.py build flash monitor
   ```
2. Kết nối WiFi **`StreetLight-Tune`**, mật khẩu **`12345678`** (đổi trong `main/tune_config.h`).
3. Mở trình duyệt: **http://192.168.4.1**
   (điện thoại có thể báo "không có Internet": cứ chọn giữ kết nối).

## Trang web hiển thị và chỉnh gì

| Phần | Nội dung |
|---|---|
| Trạng thái | lux (BH1750), ngày/đêm, chế độ, giờ trên board, thời gian chạy |
| Bốn đèn | độ sáng %, V / I / P (INA219), điện năng hôm nay + tổng, % tiết kiệm, cảnh báo lỗi bóng |
| Điều khiển | chế độ chỉnh tay (kéo độ sáng từng đèn), ép chế độ đêm |
| Cài đặt | ngưỡng lux đêm/ngày, thời gian sáng dần/tối dần, giờ ban đêm, khôi phục mặc định |

Cài đặt được lưu vào NVS, giữ lại sau khi mất điện. Giới hạn và luật kiểm tra giống dashboard
(dùng chung `sl_codec_apply_config`).

## Cấu trúc

```
tune-by-wifi/
  main/main.c          vòng lặp 200 ms + log serial
  main/node_ctrl.c     cảm biến -> luật đèn -> PWM -> điện năng (có mutex)
  main/wifi_ap.c       phát WiFi AP
  main/web_server.c    HTTP: GET /, GET /api/status, POST /api/cmd/<name>, POST /api/time
  main/web/index.html  trang web (nhúng vào firmware)
  main/tune_config.h   SSID / mật khẩu AP
```

Tái dùng, không sao chép: `bh1750`, `ina219`, `lamp_pwm`, `sl_net` (codec) trong
`firmware/components`, và `lighting`, `energy_*`, `node_store`, `sensor_hub`, `time_sync` trong
`firmware/streetlight-node/main`. Sửa ở đó thì bản này cũng nhận.

## Lưu ý

- **Giờ:** board không có Internet nên không có SNTP. Trình duyệt gửi giờ của máy bạn khi mở trang;
  chưa mở trang thì board coi là chưa có giờ (đêm sáng theo luật chưa có giờ).
- **Pin/GPIO:** dùng `firmware/streetlight-node/main/app_config.h`, cùng bo mạch với node chính.
- **Không chạy cùng lúc** với `streetlight-node`: hai app dùng chung phần cứng, nạp cái nào thì chạy cái đó.
- **Bảo mật:** API không có đăng nhập, chỉ dựa vào mật khẩu WiFi AP. Đổi mật khẩu mặc định khi dùng thật.
- **Chưa build thử:** máy hiện tại không có ESP-IDF, cần build và kiểm tra trên board.
