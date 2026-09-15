---
description: Thu thập dữ liệu cho training
---
Mức 1 chỉ có dataset ảnh cho FOMO (chi tiết: mục 5 trong plans/01-ai-models-and-dataset.md):
- Ảnh OV2640 cùng định dạng pixel, exposure/gain cố định (tắt AEC/AGC) và cách cắt ROI như firmware.
- 2 class `nguoi` / `xe`, đúng các món đồ chơi mang đi demo; vật ở mọi zone, cả đang đi và đứng yên lâu.
- ≥ 70% ảnh ở điều kiện đêm (đèn 20% và 100%); có ảnh mô hình trống và ảnh tay đẩy xe không gán nhãn.
- Chia train/test theo buổi chụp; lưu metadata.json (ngày giờ, lux, mức đèn, cấu hình camera) vào `ml/data/`.
Hiệu chỉnh kèm theo (không phải dataset): lux ngày/đêm của BH1750, vùng ROI và khoảng x từng zone, `P100` từng bóng bằng INA219 (kiểm tra chéo với đồng hồ vạn năng).
