# 02 — Danh sách linh kiện cần mua (Mức 1)

> Cập nhật: 2026-09-15. Dựa trên [00-project-setup-plan.md](00-project-setup-plan.md).
> Quy mô: mô hình **4 đèn**, mỗi đèn **1 INA219** đo điện năng. Nguồn: **sạc dự phòng cho ESP32**, **adapter 5 V cho đèn**, nối chung GND.
> **Giá chỉ để tham khảo** (VNĐ, hàng phổ thông ở các shop linh kiện), thực tế có thể chênh lệch.
> Cột "Dự phòng" là số lượng mua thêm để thay khi hỏng hoặc hàn lỗi.

## A. Vi điều khiển và camera

| # | Linh kiện | SL | Dự phòng | Giá tham khảo | Lưu ý khi mua |
|---|---|---|---|---|---|
| A1 | Board ESP32-S3 có khe camera, **N16R8**, kèm OV2640 (vd Freenove ESP32-S3-WROOM CAM) | 1 | 0 | 250–350k | Hỏi rõ có kèm camera; **xem sơ đồ chân còn trống** (cần 4 PWM + 2 I2C) |
| A2 | Ống kính OV2640 góc rộng (120°–160°) | 1 | 0 | 50–100k | **Tuỳ chọn**, mua nếu camera thường không nhìn hết 4 zone |
| A3 | Cáp USB-C **có truyền dữ liệu** | 1 | 0 | 20–40k | Dùng cả để nạp code và cấp nguồn từ sạc dự phòng |
| A4 | Module **PCA9685** (16 kênh PWM, I2C) | 1 | 0 | 50–80k | **Chỉ mua nếu board không đủ 4 chân PWM** |

## B. Đèn và mạch đóng cắt

| # | Linh kiện | SL | Dự phòng | Giá tham khảo | Lưu ý khi mua |
|---|---|---|---|---|---|
| B1 | LED trắng siêu sáng 5 mm | 4 | 3 | 1–2k/con | Mua cùng một lô để độ sáng đồng đều |
| B2 | IC **ULN2003A** (DIP-16, 7 kênh) | 1 | 1 | 5–10k/con | **Mua con IC dạng DIP-16**, không mua nhầm "module ULN2003 điều khiển động cơ bước" |
| B3 | Đế IC DIP-16 | 1 | 1 | 2–5k | Hàn đế, cắm IC để thay dễ |
| B4 | Bộ điện trở các giá trị (kit) | 1 | — | 30–50k | Hạn dòng LED **56–68 Ω** (tính lại theo Vf thực tế) |

## C. Cảm biến

| # | Linh kiện | SL | Dự phòng | Giá tham khảo | Lưu ý khi mua |
|---|---|---|---|---|---|
| C1 | Module **BH1750** (cảm biến ánh sáng, I2C) | 1 | 1 | 35–50k/con | Lắp hướng lên, có tấm che, **không nhìn thấy LED mô hình** |
| C2 | Module **INA219** (đo dòng/áp, I2C, shunt 0.1 Ω) | 4 | 1 | 35–50k/con | Chọn loại **có pad A0, A1** để đặt 4 địa chỉ `0x40–0x43`; có sẵn cầu đấu VIN+/VIN− |

**Vì sao không dùng ACS712:** ACS712 (bản 5 A) cho 185 mV/A, nên LED 20 mA chỉ làm điện áp ra thay đổi khoảng 3.7 mV,
trong khi nhiễu của nó khoảng 21 mV. Ngoài ra nó chỉ đo dòng, và tốn 4 chân ADC. INA219 đo được cả dòng lẫn áp với độ phân giải ~0.1 mA, dùng I2C.

## D. Nguồn

| # | Linh kiện | SL | Giá tham khảo | Lưu ý |
|---|---|---|---|---|
| D1 | Sạc dự phòng **5 V ≥ 2 A** | 1 | có sẵn (mua mới 150–300k) | Nguồn cho ESP32 qua cáp A3 |
| D2 | Adapter **5 V ≥ 1 A**, đầu jack 5.5×2.1 mm | 1 | 40–80k | Nguồn cho 4 đèn (4 × 20 mA ≈ 80 mA, dư nhiều) |
| D3 | Jack DC cái 5.5×2.1 mm | 1 | 5–10k | Nối adapter vào mạch đèn |
| D4 | Tụ hoá 470 µF 16 V + tụ gốm 100 nF | 2 + 2 | 1–2k/con | Gần chân nguồn ESP32 |
| D5 | Tụ hoá 100 µF 16 V | 1 | 1–2k | Gần ULN2003 trên đường 5 V của đèn |
| D6 | Công tắc gạt | 1 | 3–5k | Tắt/mở nguồn đèn |

**Nhớ:** nối **GND của ESP32 với cực (−) của adapter**; **không** nối cực (+) 5 V của adapter vào chân 5 V của ESP32.

## E. Lắp ráp

| # | Linh kiện | SL | Giá tham khảo |
|---|---|---|---|
| E1 | Breadboard 830 lỗ | 1 | 25–40k |
| E2 | Dây cắm đực-đực, đực-cái (bó 40 sợi) | 2 bó | 20–30k/bó |
| E3 | Board mạch lỗ 7×9 cm | 1 | 10–20k |
| E4 | Header đực/cái 2.54 mm | 1 bộ | 10–20k |
| E5 | Dây điện mềm 22–24 AWG | 1 cuộn | 20–40k |
| E6 | Ống co nhiệt | 1 bộ | 20–30k |

## F. Mô hình

| # | Vật liệu | SL | Giá tham khảo | Lưu ý |
|---|---|---|---|---|
| F1 | Tấm fomex/mica làm mặt đường (khoảng 60×30 cm) | 1 | 30–50k | Đủ 4 zone |
| F2 | Que gỗ / ống nhựa làm cột đèn | 4 | 15–40k | Chừa lỗ đi dây |
| F3 | Giá treo camera | 1 | 20–50k | **Cao, nhìn gần như thẳng xuống**, cố định chắc, không xê dịch sau khi chụp dataset |
| F4 | Xe đồ chơi + người đồ chơi | 3–5 | 50–150k | Mua xong **không đổi** vì dataset chụp theo đúng các món này |
| F5 | Keo nến, băng keo 2 mặt, decal vạch kẻ đường | — | 30–60k | |

## G. Dụng cụ (nếu chưa có)

| Dụng cụ | Vì sao cần |
|---|---|
| Đồng hồ vạn năng | Đo Vf LED để chọn điện trở; **kiểm tra chéo số đo INA219**; kiểm tra GND đã nối chung |
| Mỏ hàn, thiếc | Hàn header, board lỗ, đế IC, **pad địa chỉ A0/A1 của INA219** |
| Kìm tuốt dây, kìm cắt | |

## H. Dịch vụ miễn phí (không cần mua)

- **Edge Impulse** — train FOMO (kiểm tra giới hạn gói miễn phí).
- **HiveMQ Cloud** — MQTT broker (kiểm tra số kết nối, số tin nhắn, kích thước tin nhắn tối đa).
- **GitHub Pages** — đặt trang dashboard.

## Tổng chi phí ước tính

| Nhóm | Ước tính |
|---|---|
| A. Board + camera + cáp (chưa tính A2, A4) | 270–390k |
| B. Đèn + ULN2003 + điện trở | 50–95k |
| C. BH1750 + 4 INA219 (tính cả dự phòng) | 245–350k |
| D. Nguồn (sạc dự phòng có sẵn) | 55–105k |
| E. Lắp ráp | 125–210k |
| F. Mô hình | 145–350k |
| **Tổng** | **khoảng 0.9–1.5 triệu** (chưa tính sạc dự phòng nếu phải mua mới, dụng cụ, món tuỳ chọn) |

## Thứ tự mua gợi ý

1. **Đợt 1:** A1, A3, 2 × B1, B2, B3, B4, C1, **1 × C2**, D2, D3, E1, E2 → làm giai đoạn 1–3 (camera + FOMO mẫu, PWM qua ULN2003, BH1750, thử đo 1 bóng bằng INA219). **Kiểm tra số chân GPIO trống** để quyết định có cần A4.
2. **Đợt 2:** phần còn lại của B, C2, D, E.
3. **Đợt 3:** mô hình F. **Chốt đồ chơi và vị trí camera trước khi chụp dataset.**
