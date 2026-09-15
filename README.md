# smart-street-lighting

Hệ thống chiếu sáng công cộng thông minh, tiết kiệm năng lượng, có sử dụng AI (đồ án môn học, mô hình thu nhỏ với 6 đèn và xe/người đồ chơi).

**Mức 1:** một ESP32-S3 có camera chạy FOMO (Edge Impulse) nhận diện người/xe ngay trên board và điều khiển đèn theo zone:
ban ngày tắt, ban đêm nền 20%, có người/xe thì sáng 100% vùng cần thiết. Vật dừng lâu thì cảnh báo kèm ảnh.
Dashboard web tĩnh (GitHub Pages) kết nối HiveMQ Cloud qua MQTT để xem thông số và điều khiển từ xa.

```
ESP32-S3 CAM (FOMO + luật + BH1750 + 6 LED) ──MQTT/TLS──► HiveMQ Cloud ◄──WebSocket/TLS── Dashboard web (GitHub Pages)
```

| Thư mục | Nội dung |
|---|---|
| `firmware/streetlight-node/` | App ESP-IDF duy nhất |
| `firmware/components/` | Driver BH1750, PWM đèn, logic zone, MQTT |
| `ml/vision/` | Dataset + project Edge Impulse |
| `dashboard/` | Trang web tĩnh HTML/JS |
| `plans/` | `00` tổng quan, `01` AI + luật + dataset, `02` linh kiện |
| `.claude/rules/board-config.md` | Thông số board |
