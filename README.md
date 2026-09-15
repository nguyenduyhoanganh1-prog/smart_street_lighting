# smart-street-lighting

Hệ thống chiếu sáng công cộng thông minh, tiết kiệm năng lượng, có sử dụng AI (đồ án môn học, mô hình thu nhỏ với 4 đèn và xe/người đồ chơi).

**Mức 1:** một ESP32-S3 có camera chạy FOMO (Edge Impulse) nhận diện người/xe ngay trên board và điều khiển đèn theo zone:
ban ngày tắt, ban đêm nền 20%, có người/xe thì sáng 100% vùng cần thiết. Vật dừng lâu thì cảnh báo kèm ảnh.
Mỗi bóng có 1 INA219 đo dòng/áp để tính **điện năng tiêu thụ thật** của từng bóng.
Dashboard web tĩnh (GitHub Pages) kết nối HiveMQ Cloud qua MQTT để xem thông số, điện năng từng bóng và điều khiển từ xa.

```
ESP32-S3 CAM (FOMO + luật + BH1750 + 4 INA219 + 4 LED) ──MQTT/TLS──► HiveMQ Cloud ◄──WebSocket/TLS── Dashboard web (GitHub Pages)
```

| Thư mục | Nội dung |
|---|---|
| `firmware/streetlight-node/` | App ESP-IDF duy nhất |
| `firmware/components/` | Driver BH1750, INA219, PWM đèn, logic zone, đo điện năng, MQTT |
| `ml/vision/` | Dataset + project Edge Impulse |
| `dashboard/` | Trang web tĩnh HTML/JS |
| `plans/` | `00` tổng quan, `01` AI + luật + điện năng + dataset, `02` linh kiện, `03` sơ đồ nối dây |
| `.claude/rules/board-config.md` | Thông số board |
