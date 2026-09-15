---
description: Phân tích memory usage (.text/.data/.bss, stack high-water mark)
---
Chạy `idf.py size` và `idf.py size-components` cho app cần phân tích, so sánh với budget trong board-config.md.
Báo cáo riêng SRAM nội và PSRAM (runtime: `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` / `MALLOC_CAP_SPIRAM`).
Kiểm tra stack high-water mark của từng FreeRTOS task (`uxTaskGetStackHighWaterMark`).
Nếu có TinyML: tách riêng tensor arena ra khỏi .bss khi báo cáo, ghi rõ arena đặt ở SRAM nội hay PSRAM.
Đề xuất tối ưu nếu gần vượt ngưỡng.
