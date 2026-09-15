---
description: Build và flash firmware lên board hiện tại
---
Đọc board-config.md để biết toolchain (ESP-IDF).
App cần build: `firmware/streetlight-node`. Hỏi người dùng cổng COM.
Chạy `idf.py build` trong thư mục app.
Chạy `idf.py size`, báo cáo Flash và RAM (SRAM nội, PSRAM) so với budget trong board-config.md TRƯỚC khi flash — cảnh báo nếu vượt 90%.
Flash và mở monitor: `idf.py -p <COMx> flash monitor`.
