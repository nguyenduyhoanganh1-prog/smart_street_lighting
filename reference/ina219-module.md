# Module INA219 — cảm biến dòng/áp DC (I2C)

> Dùng trong project: **4 module**, mỗi đèn 1 module, đo dòng/áp để tính điện năng tiêu thụ từng bóng.
> Liên quan: [plans/00](../plans/00-project-setup-plan.md) (sơ đồ nguồn), [plans/01](../plans/01-ai-models-and-dataset.md) mục 4 (cách tính điện năng).

## 1. Thông số (theo mô tả của shop)

| Thông số | Giá trị | Ghi chú cho project |
|---|---|---|
| Chip | TI INA219, ADC 12-bit | |
| Điện trở shunt | `R100` = 0.1 Ω | In trên module |
| Dòng đo tối đa | 3.2 A | Là dải **mặc định** (±320 mV trên shunt). Project cấu hình lại dải **±400 mA** để đo LED 4–20 mA chính xác hơn |
| Điện áp tải đo được (bus) | 0–26 V DC | Project đo 5 V |
| Điện áp nuôi module (VCC) | 3–5 V DC | **Project cấp 3.3 V** (xem mục 4) |
| Sai số | ±1% (gain), lệch offset tối đa 100 µV trong dải −40…85 °C | Với shunt 0.1 Ω, 100 µV ≈ 1 mA sai lệch tối đa → cần hiệu chỉnh/kiểm tra bằng đồng hồ vạn năng |
| Giao tiếp | I2C | Địa chỉ `0x40`, `0x41`, `0x44`, `0x45` bằng pad A0/A1 |
| Kích thước | 25.5 × 22.3 mm | |

## 2. Các chân trên module

```
        Cầu đấu (terminal xanh)
        ┌───────────┬───────────┐
        │   Vin+    │   Vin−    │   ← nối vào ĐƯỜNG DÂY cần đo (nối tiếp)
        └───────────┴───────────┘
   ┌──────────────────────────────────┐
   │  R100 (shunt 0.1 Ω)   [A0][A1]   │ ← pad "I2C Address"
   │           INA219                 │
   │                                  │
   │  Vcc  Gnd  Scl  Sda  Vin−  Vin+  │ ← hàng header 6 chân
   └──────────────────────────────────┘
```

| Chân | Chức năng |
|---|---|
| **Vcc** | Nguồn nuôi chip (3–5 V) |
| **Gnd** | Mass chung |
| **Scl** | Xung clock I2C |
| **Sda** | Dữ liệu I2C |
| **Vin+** | Đầu vào dòng cần đo — phía **nguồn** |
| **Vin−** | Đầu ra dòng cần đo — phía **tải** |

- **Vin+ / Vin− trên header và trên cầu đấu là cùng một điểm**, chỉ cần dùng một trong hai. Nên dùng **cầu đấu** cho dây đi tới đèn vì chắc hơn.
- Dòng cần đo phải **chạy xuyên qua module**: vào Vin+, qua shunt, ra Vin−.

## 3. Đặt địa chỉ I2C (hàn pad A0/A1)

Hàn nối (bridge) 2 nửa của pad bằng một giọt thiếc:

| Module (bóng) | A0 | A1 | Địa chỉ |
|---|---|---|---|
| Bóng 1 | không hàn | không hàn | `0x40` |
| Bóng 2 | **hàn** | không hàn | `0x41` |
| Bóng 3 | không hàn | **hàn** | **`0x44`** |
| Bóng 4 | **hàn** | **hàn** | **`0x45`** |

> Địa chỉ = `0x40 + (A1 × 4) + (A0 × 1)`. Vì vậy **`0x42` và `0x43` không dùng được bằng cách hàn pad**
> (hai địa chỉ đó chỉ có khi nối A0 vào SDA hoặc SCL). Đây là lỗi trong bản tài liệu trước, đã sửa.

- **Dán nhãn số 1–4 lên từng module ngay sau khi hàn** để khỏi lẫn.
- Sau khi hàn, chạy chương trình **quét I2C** trên ESP32. Phải thấy đủ `0x23` (BH1750), `0x40`, `0x41`, `0x44`, `0x45`.
  Phải thấy đủ `0x23` (BH1750), `0x40`, `0x41`, `0x44`, `0x45`. Thiếu hoặc trùng thì kiểm tra lại mối hàn pad.

## 4. Cách nối trong project (4 module)

```
ESP32-S3 3.3V ──┬─────────┬─────────┬─────────┬───────── Vcc (INA219 #1..#4, BH1750)
ESP32-S3 GND ───┼─────────┼─────────┼─────────┼───────── Gnd (INA219 #1..#4, BH1750) ─── GND adapter (−) ─── GND ULN2003
ESP32 GPIO SDA ─┼─────────┼─────────┼─────────┼───────── Sda (tất cả, nối song song)
ESP32 GPIO SCL ─┴─────────┴─────────┴─────────┴───────── Scl (tất cả, nối song song)

Mỗi bóng i (1..4):
Adapter +5V ──► Vin+ [INA219 #i] Vin− ──► điện trở 56–68 Ω ──► LED i (anode → cathode) ──► OUTi ULN2003
```

| Chân INA219 | Nối tới | Ghi chú |
|---|---|---|
| Vcc | **3.3 V của ESP32** | Không cấp 5 V — xem lưu ý bên dưới |
| Gnd | GND chung | Chung với GND ESP32 và cực (−) adapter |
| Sda | Chân SDA của ESP32 | 4 module + BH1750 nối song song |
| Scl | Chân SCL của ESP32 | 4 module + BH1750 nối song song |
| Vin+ | **+5 V adapter** | Phía nguồn |
| Vin− | Điện trở hạn dòng của bóng i | Phía tải |

### Lưu ý quan trọng
- **Cấp Vcc = 3.3 V, không cấp 5 V.** Điện trở kéo lên của I2C trên module nối vào Vcc; nếu Vcc = 5 V thì chân SDA/SCL sẽ bị kéo lên 5 V,
  trong khi **chân GPIO của ESP32-S3 chỉ chịu được 3.3 V** → có thể hỏng board. Vcc chỉ nuôi chip, không liên quan tới điện áp đo ở Vin+ (5 V vẫn đo bình thường).
- **GND phải chung** giữa module, ESP32 và adapter. INA219 đo điện áp bus (Vin−) so với chân Gnd của nó.
- **Không đảo Vin+ / Vin−.** Đảo thì dòng đọc ra số âm.
- **Điện trở kéo lên I2C:** mỗi module thường có 10 kΩ trên SDA/SCL. 4 INA219 + BH1750 mắc song song còn khoảng 2 kΩ, vẫn ổn ở 3.3 V.
  Nếu dây I2C dài > 50 cm hoặc giao tiếp chập chờn, rút ngắn dây trước, sau đó mới tính tới việc tháo bớt điện trở kéo lên.
- **Tách dây:** dây I2C (tín hiệu yếu) đi xa dây 5 V của đèn (có PWM) để giảm nhiễu.

## 5. Cấu hình cho project

| Mục | Giá trị | Lý do |
|---|---|---|
| Dải đo shunt (PGA) | ±40 mV (gain /1) → tối đa 400 mA | LED chỉ 4–20 mA; dải nhỏ cho độ phân giải tốt nhất |
| Dải điện áp bus | 16 V | Nguồn đèn 5 V |
| ADC shunt | 12-bit, **trung bình 128 mẫu** (~68 ms) | Đèn chạy PWM 5 kHz → lấy trung bình để ra dòng trung bình đúng |
| ADC bus | 12-bit, trung bình 16 mẫu | |
| Chế độ | Đo liên tục shunt + bus | |
| Chu kỳ đọc | 100 ms/bóng | |
| Calibration | Theo shunt 0.1 Ω; kiểm tra bằng đồng hồ vạn năng | |

Công thức dùng trong firmware: `P = V_bus × I`, `E += P × Δt` (chi tiết: plans/01 mục 4).

## 6. Thử module từng bước

1. Hàn header và pad địa chỉ, dán nhãn.
2. Nối **1 module** (Vcc 3.3 V, Gnd, Sda, Scl) → chạy quét I2C, thấy đúng địa chỉ.
3. Nối Vin+/Vin− nối tiếp với **1 bóng**, bật bóng 100% (không PWM) → so dòng INA219 với **đồng hồ vạn năng** mắc nối tiếp. Lệch > 10% thì kiểm tra lại calibration.
4. Bật PWM 20% → số đo phải ổn định quanh khoảng 20% dòng lúc 100%, không nhảy loạn. Nhảy loạn → kiểm tra cấu hình lấy trung bình.
5. Làm tương tự cho 3 module còn lại, rồi chạy cả 4 cùng lúc.

## 7. Lỗi hay gặp

| Hiện tượng | Nguyên nhân thường gặp |
|---|---|
| Quét I2C không thấy module | Chưa nối Gnd chung; đảo Sda/Scl; chưa hàn header chắc |
| 2 module cùng địa chỉ | Hàn pad sai hoặc mối hàn không dính |
| Dòng đọc ra âm | Đảo Vin+ và Vin− |
| Dòng nhảy loạn khi đèn chạy PWM | Chưa bật lấy trung bình 128 mẫu |
| Dòng = 0 dù đèn sáng | Dòng không chạy qua module (Vin+/Vin− không nằm trên đường nối tiếp của bóng) |
| ESP32 hỏng chân / reset lạ | Cấp Vcc module 5 V làm I2C bị kéo lên 5 V |

## Tài liệu gốc

- Datasheet TI INA219: tìm "INA219 datasheet" trên trang ti.com.
- Driver ESP-IDF tham khảo: thư viện `esp-idf-lib` (có driver `ina219`).
