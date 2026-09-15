---
description: Deploy model TFLite Micro lên board
---
Đọc board-config.md (RAM/Flash/FPU). Xác nhận quantization int8 theo skill tinyml-model-deployment.
Model của Mức 1: FOMO (Edge Impulse) nhận diện `nguoi` / `xe` → `firmware/streetlight-node` (chi tiết: plans/01-ai-models-and-dataset.md).
- Xuất từ Edge Impulse bản C++ library / ESP-IDF, int8 quantized. Không tự viết op resolver vì thư viện đã kèm.
- Kiểm tra pipeline tiền xử lý trên board (định dạng pixel, cắt ROI, resize, exposure/gain cố định) khớp 100% với lúc thu dataset.
- Báo cáo RAM (arena ở SRAM nội hay PSRAM), Flash, thời gian suy luận mỗi frame; lưu ý RAM còn lại cho WiFi + MQTT/TLS.
- So sánh kết quả trên board với kết quả test trong Edge Impulse Studio, và đo độ chính xác zone thực tế.
