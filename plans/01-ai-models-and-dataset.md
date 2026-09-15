# 01 — AI, luật chiếu sáng, đo điện năng và dataset (Mức 1)

> Trạng thái: **đã chốt** (2026-09-15). Một model AI: **FOMO (Edge Impulse) nhận diện `nguoi` / `xe`** chạy trên ESP32-S3.
> Mô hình **4 đèn = 4 zone**; điện năng từng bóng **đo thật bằng INA219**.
> Liên quan: [00-project-setup-plan.md](00-project-setup-plan.md)

## 1. Tổng quan

| Giai đoạn | Vai trò | Cách làm |
|---|---|---|
| **1 — Nhận biết** | Vật thể là gì, ở zone nào, đang đi hay đứng yên; ngày hay đêm; mỗi bóng đang tiêu thụ bao nhiêu điện | **AI** (FOMO) + BH1750 + INA219 |
| **2 — Quyết định** | Độ sáng từng đèn, khi nào cảnh báo | **Luật (state machine)**, không train |

**Vì sao giai đoạn 2 dùng luật:** muốn train model ra quyết định thì cần nhãn "đáng lẽ phải bật zone nào",
mà nhãn đó lại lấy từ chính luật mình đặt ra. Dùng luật thì chắc chắn, dễ giải thích khi bảo vệ và an toàn hơn.

**Vì sao cần AI mà không dùng cảm biến vật cản:** cảm biến vật cản chỉ biết *có vật tại 1 điểm*.
Camera + AI biết **vật đó là người hay xe**, **ở đâu trên cả đoạn đường**, **đang đi hay đứng yên**,
và có **ảnh** để người quản lý đánh giá. Ngoài đời, 1 camera phủ được cả đoạn đường thay cho nhiều cảm biến.

## 2. Model 1 — FOMO trên ESP32-S3

**Vì sao chọn FOMO:** FOMO trả **tâm vật thể** trên một lưới (vd ảnh 96×96 → lưới 12×12) thay vì bounding box.
Hệ thống chỉ cần biết vật ở zone nào, nên tâm là đủ. Đổi lại model rất nhẹ (vài trăm KB RAM), chạy nhanh trên MCU.

```
OV2640 chụp ảnh thô (RGB565/GRAYSCALE, không dùng JPEG để khỏi tốn thời gian giải nén)
 → cắt ROI chỉ gồm mặt đường → thu nhỏ về input model (96×96 hoặc 160×160)
 → FOMO → danh sách tâm vật thể {class, x, y, score}
 → theo dõi qua các frame (ghép tâm gần nhất) → zone, hướng, đứng yên hay di chuyển
 → luật chiếu sáng
 → khi có cảnh báo vật dừng: chụp toàn khung hình, nén JPEG → MQTT lên dashboard
```

- **Cắt ROI là bắt buộc.** Thu nhỏ cả khung hình xuống 96×96 thì đồ chơi chỉ còn vài pixel và model không thấy.
- **Zone:** sau khi cắt ROI dọc theo đường, mỗi zone là **một khoảng toạ độ x** (4 zone = 4 đèn), hiệu chỉnh một lần.
- **Lắp camera cao, nhìn gần như thẳng xuống.** FOMO trả tâm vật thể chứ không trả điểm chạm đường, nên camera càng nghiêng thì zone càng lệch.
- **Tắt AEC/AGC** (tự động phơi sáng/tăng sáng) của OV2640, đặt exposure/gain cố định, để ảnh khi chạy thật giống ảnh lúc thu dataset.
- **Tốc độ:** mục tiêu ≥ 3–5 lần suy luận/giây, phải **đo thực tế**.
- **Hạn chế:** 2 vật quá sát nhau có thể bị gộp thành 1. Không ảnh hưởng nhiều vì cả hai cùng zone.

## 3. Luật chiếu sáng

Ba mức độ sáng: **TẮT (0%)**, **NỀN (`BASE_DIM` = 20%)**, **SÁNG (`FULL` = 100%)**.

```
Dashboard đang chỉnh tay (và chưa quá MANUAL_TIMEOUT)?  → dùng lệnh tay

CHẾ ĐỘ THEO GIỜ — khi BH1750 lỗi HOẶC camera/FOMO lỗi:
    Luôn gửi cảnh báo (1 lần khi phát hiện, sau đó nhắc lại mỗi ALERT_REPEAT)
    Chưa có giờ (chưa đồng bộ SNTP)                → SÁNG tất cả 100%
    Giờ hiện tại từ NIGHT_END đến trước NIGHT_START
      (mặc định 06:00 → 18:00)                     → TẮT tất cả, chỉ cảnh báo
    Giờ hiện tại từ NIGHT_START đến trước NIGHT_END
      (mặc định 18:00 → 06:00)                     → SÁNG tất cả 100%
    Cảm biến/camera chạy lại bình thường           → thoát chế độ theo giờ, gửi "đã khôi phục"

BÌNH THƯỜNG — BH1750 và camera/FOMO đều chạy tốt:
BAN NGÀY (lux > LUX_DAY liên tục DAY_NIGHT_HOLD):
    → TẮT tất cả đèn, dừng FOMO (camera chỉ tự kiểm tra mỗi CAMERA_CHECK_INTERVAL)

BAN ĐÊM (lux < LUX_NIGHT liên tục DAY_NIGHT_HOLD):
    Mặc định: tất cả đèn NỀN 20%
    Xét TỪNG vật thể, lấy HỢP các zone cần sáng:
      nguoi ở zone K               → SÁNG K−1, K, K+1, giữ HOLD_PERSON sau khi rời zone
      xe ở zone K, đã biết hướng   → SÁNG K và 1 zone phía trước, giữ HOLD_VEHICLE
      xe ở zone K, chưa biết hướng → SÁNG K−1, K, K+1
      vật đứng yên > STOP_TIME     → SÁNG K−1, K, K+1 + CẢNH BÁO VẬT DỪNG kèm ảnh
```

- **Người sáng rộng và giữ lâu** vì đi chậm, cần thấy xung quanh. **Xe sáng về phía trước và tắt nhanh** vì đi nhanh.
- Chỉ có 4 zone nên xe chỉ sáng thêm **1** zone phía trước; sáng 2 zone phía trước thì gần như bật cả đường, mất ý nghĩa tiết kiệm.
- **Hướng di chuyển:** so zone của vật ở 2 lần suy luận liên tiếp. Zone đầu/cuối (1 và 4) không có zone phía trước thì bỏ qua.

### Chuyển độ sáng: tăng nhanh, giảm từ từ

| Hướng | Thời gian chuyển | Lý do |
|---|---|---|
| **Tăng** (vd 20% → 100%, 0% → 100%) | `FADE_UP_MS` ≈ **200 ms** | Người/xe vừa vào zone là có đủ ánh sáng ngay; vẫn mềm hơn bật phụt |
| **Giảm** (vd 100% → 20%, 20% → 0%) | `FADE_DOWN_MS` ≈ **3 s** | Không làm mắt người đi đường bị "tối sầm" đột ngột; nếu FOMO lỡ 1–2 frame thì đèn cũng không chớp tắt |

- Áp dụng cho **mọi thay đổi độ sáng**: luật zone, ngày/đêm, chế độ theo giờ, và cả lệnh chỉnh tay từ dashboard.
- Dùng tính năng fade phần cứng của LEDC (`ledc_set_fade_with_time` + `ledc_fade_start` không chờ), CPU không phải tự tăng giảm từng bước.
- **Đang giảm dần mà có vật thể mới vào** (cần tăng) → **dừng ngay lần giảm đang chạy** (`ledc_fade_stop`), đọc độ sáng hiện tại (`ledc_get_duty`) rồi **tăng nhanh từ mức đó**, không đợi giảm xong.
- Đang tăng mà có lệnh giảm → cho tăng xong (chỉ 200 ms) rồi mới giảm dần, tránh đèn giật.
- Điện năng lúc giảm dần tốn thêm rất ít (vài giây mỗi lần); INA219 đo điện năng thật nên đã tính đủ phần này.
- Dataset ảnh nên có một ít ảnh **đang giảm sáng dở dang** (mức giữa 20% và 100%); augmentation độ sáng của Edge Impulse cũng giúp phần này.

### Chế độ theo giờ (khi cảm biến hoặc camera lỗi)

- **Quy tắc được xét liên tục theo giờ hiện tại**, không phải theo giờ lúc hỏng. Ví dụ camera hỏng lúc 15h: từ 15h đến 18h chỉ gửi cảnh báo, đèn tắt; **đến 18h mà vẫn chưa sửa thì tự bật 100%**; đến 06h sáng thì tắt.
- **Phát hiện lỗi:**
  - BH1750: không đọc được sau vài lần thử liên tiếp.
  - Camera/FOMO ban đêm: không có kết quả quá `VISION_TIMEOUT`.
  - Camera ban ngày: FOMO không chạy ban ngày, nên camera **tự kiểm tra** mỗi `CAMERA_CHECK_INTERVAL` (chụp 1 ảnh + chạy FOMO 1 lần). Nhờ vậy hỏng trước 18h vẫn biết để gửi cảnh báo.
- **Nguồn giờ:** đồng bộ **SNTP** qua Internet khi khởi động, múi giờ Việt Nam (UTC+7). ESP32 tự giữ giờ trong lúc còn cấp điện, kể cả khi sau đó mất Internet.
  - Nếu ESP32 **khởi động lại mà không có Internet** thì không biết giờ → **SÁNG tất cả 100%** (an toàn) + cảnh báo khi có mạng lại.
  - *Tuỳ chọn:* thêm module đồng hồ thời gian thực **DS3231** (có pin nuôi, I2C, dùng chung bus) để giữ giờ khi mất cả điện lẫn mạng.
- **Lưu ý:** mùa đông trời có thể tối trước 18h. Khi cảm biến/camera lỗi thì hệ thống không còn cách nào biết trời tối sớm, nên `NIGHT_START` là tham số, **chỉnh được từ dashboard** (vd 17:30).

### Chế độ ngày/đêm (BH1750)
- **Hai ngưỡng có trễ (hysteresis):** `LUX_NIGHT` < `LUX_DAY`, và phải giữ liên tục `DAY_NIGHT_HOLD` mới đổi chế độ, tránh đèn bật tắt liên tục khi ánh sáng dao động.
  - Đang **ngày** → chỉ chuyển **đêm** khi lux < `LUX_NIGHT` đủ lâu.
  - Đang **đêm** → chỉ chuyển **ngày** khi lux > `LUX_DAY` đủ lâu.
  - Lux **nằm giữa** hai ngưỡng → **giữ nguyên chế độ hiện tại**.
- **Lúc vừa khởi động** (chưa có chế độ trước đó) mà lux nằm giữa hai ngưỡng:
  - Đã có giờ (SNTP) → chọn theo giờ: trong `NIGHT_START`–`NIGHT_END` là **đêm**, ngoài khoảng đó là **ngày**.
  - Chưa có giờ → chọn **đêm** (thà sáng thừa còn hơn tối).
  - Lux < `LUX_NIGHT` hoặc > `LUX_DAY` thì chọn theo lux như bình thường.
- **BH1750 không được nhìn thấy LED của mô hình.** Nếu không sẽ lặp: tối → bật đèn → cảm biến thấy sáng → tưởng ngày → tắt đèn...
  Lắp hướng lên trên, có tấm che phía dưới, đặt cao nhất trên mô hình.
- **Hiệu chỉnh:** đo lux lúc bật đèn phòng ("ngày") và tắt đèn phòng ("đêm"), đặt ngưỡng ở khoảng giữa.
- Dashboard có nút **"ép chế độ đêm"** để demo mà không phải tắt đèn phòng.

### Cảnh báo (hiện trên dashboard)

| Cảnh báo | Khi nào | Đèn | Dashboard hiện |
|---|---|---|---|
| **Vật dừng lâu** | Tâm vật di chuyển < `STOP_EPS` trong `STOP_TIME` | SÁNG K−1, K, K+1 | Loại, zone, thời gian dừng, **ảnh**, nút "Đã xử lý" |
| Camera/FOMO lỗi | Ban đêm: không có kết quả quá `VISION_TIMEOUT`. Ban ngày: tự kiểm tra thất bại | **Theo giờ:** 06:00–18:00 tắt (chỉ cảnh báo); 18:00–06:00 sáng 100% | Loại lỗi, thời điểm, "đang chạy chế độ theo giờ" |
| BH1750 lỗi | Không đọc được cảm biến | **Theo giờ** (như trên) | Loại lỗi, thời điểm, "đang chạy chế độ theo giờ" |
| Mất giờ | Khởi động lại mà chưa đồng bộ được SNTP | Sáng 100% | Gửi khi có mạng lại |
| INA219 lỗi | Không đọc được module của bóng i | Giữ nguyên luật | `Bóng i: không đo được điện năng` |

- **Ảnh chỉ gửi khi có cảnh báo vật dừng**: chụp toàn khung hình, vẽ khung đánh dấu vật thể, nén JPEG, publish retained lên `streetlight/alert/image`. Đèn đang sáng 100% nên ảnh rõ.
- **Vì sao người quản lý tự đánh giá:** từ ảnh, AI khó phân biệt xe hỏng/tai nạn với xe dừng tạm và cũng không có dữ liệu để train việc đó.
  AI phát hiện "có vật dừng", con người quyết định mức độ nghiêm trọng.
- Cảnh báo đóng khi vật rời đi hoặc người quản lý bấm **"Đã xử lý"**; khi đóng thì xoá ảnh retained.

## 4. Đo điện năng từng bóng (INA219, không phải AI)

### Nguyên lý
- Mỗi bóng có 1 INA219 nối tiếp phía +5 V. INA219 đo **sụt áp trên điện trở shunt 0.1 Ω** → suy ra **dòng I**, và đo **điện áp V** tại chân VIN−.
- **Công suất:** `P_i = V_i × I_i` (mW). Đây là công suất bóng i lấy từ adapter (gồm LED + điện trở + sụt áp ULN2003).
- **Điện năng:** cứ mỗi lần đọc, cộng dồn `E_i += P_i × Δt`. `Δt` lấy từ đồng hồ thật của ESP32 (`esp_timer`), không giả định chu kỳ cố định.

### Vì sao phải lấy trung bình (đèn chạy PWM)
Dòng qua LED bật tắt 5000 lần/giây. Nếu INA219 đọc một mẫu nhanh, lúc trúng xung bật (20 mA), lúc trúng xung tắt (0 mA), số liệu nhảy loạn.
→ Cấu hình INA219 **lấy trung bình 128 mẫu cho kênh shunt** (mỗi lần đo khoảng 68 ms, phủ khoảng 340 chu kỳ PWM), nên kết quả là **dòng trung bình đúng**.

### Cấu hình INA219

| Mục | Giá trị | Lý do |
|---|---|---|
| Dải đo shunt (PGA) | ±40 mV (gain /1) | Dòng tối đa 400 mA với shunt 0.1 Ω, dư cho LED 20 mA, độ phân giải tốt nhất |
| Dải điện áp bus | 16 V | Nguồn đèn 5 V |
| ADC shunt | 12-bit, trung bình 128 mẫu | Lọc PWM |
| ADC bus | 12-bit, trung bình 16 mẫu | Điện áp gần như không đổi |
| Chế độ | Đo liên tục (shunt + bus) | |
| Chu kỳ đọc | `ENERGY_SAMPLE_INTERVAL` = 100 ms mỗi bóng | Khớp thời gian chuyển đổi của INA219 |
| Calibration | Theo shunt 0.1 Ω thực tế; kiểm tra lại bằng đồng hồ vạn năng | Độ phân giải hiệu dụng khoảng 0.1 mA |

### Số liệu tính trên board

| Số liệu | Cách tính |
|---|---|
| Công suất hiện tại `P_i` (mW) | `V_i × I_i` |
| Điện năng hôm nay `E_today_i` (mWh) | Cộng dồn từ 00:00 (giờ Việt Nam, đồng bộ SNTP); về 0 lúc nửa đêm |
| Điện năng tổng `E_total_i` (mWh) | Cộng dồn từ lúc bắt đầu, không reset |
| **% tiết kiệm bóng i** | `1 − E_đêm_thực_i / (P100_i × thời_gian_đêm)`. `P100_i` = công suất **đo thật** của bóng i ở 100%, hiệu chỉnh 1 lần và lưu NVS |
| Tổng 4 bóng | Cộng 4 bóng |
| Lịch sử 24 giờ | Mỗi giờ lưu 1 giá trị điện năng/bóng vào mảng vòng 24 × 4 trong RAM, publish retained `streetlight/energy/history` |

- **Lưu NVS** `E_total_i`, `E_today_i` mỗi `ENERGY_SAVE_INTERVAL` (10 phút) và lịch sử 24 giờ mỗi giờ, để mất điện ESP32 không mất số liệu. Không ghi liên tục vì flash có giới hạn số lần ghi.
- Số liệu mô hình rất nhỏ (mỗi bóng khoảng 100 mW) nên dashboard hiển thị đơn vị **mW, mWh**.

### Hiệu chỉnh `P100_i` (giai đoạn 3)
1. Tắt đèn phòng, ép chế độ tay, bật bóng i ở 100% trong 10 s, lấy trung bình công suất → `P100_i`, lưu NVS.
2. Kiểm tra chéo: đo dòng bóng i bằng **đồng hồ vạn năng** (không PWM, 100%), so với INA219; lệch > 10% thì xem lại calibration/shunt.

### Chỉnh tham số từ dashboard

| Tham số | Điều khiển trên dashboard | Giới hạn | Bước | Mặc định |
|---|---|---|---|---|
| `LUX_NIGHT` — dưới mức này đủ lâu thì chuyển **đêm** | Slider | 0–1000 lux | 5 lux | ~30 *(hiệu chỉnh)* |
| `LUX_DAY` — trên mức này đủ lâu thì về **ngày** | Slider | 0–1000 lux | 5 lux | ~100 *(hiệu chỉnh)* |
| `FADE_UP_MS` — thời gian **sáng dần** | Slider | 0–2000 ms | 50 ms | 200 ms |
| `FADE_DOWN_MS` — thời gian **tối dần** | Slider | 0–10000 ms | 250 ms | 3000 ms |
| `NIGHT_START` / `NIGHT_END` | Ô chọn giờ (HH:MM) | 00:00–23:59 | 1 phút | 18:00 / 06:00 |

**Quy tắc kiểm tra trên board** (không tin hoàn toàn dữ liệu từ web):
- Giá trị ngoài giới hạn → **kẹp về giới hạn** gần nhất.
- **`LUX_DAY` phải lớn hơn `LUX_NIGHT` ít nhất `LUX_MIN_GAP` (20 lux)**, để giữ vùng trễ (hysteresis) chống đèn bật tắt liên tục.
  Người dùng kéo vi phạm → board tự đẩy ngưỡng còn lại ra cho đủ khoảng cách, publish giá trị thật, dashboard hiện thông báo
  "Ngưỡng về ngày đã được chỉnh thành X lux để cách ngưỡng đêm ít nhất 20 lux".
- `NIGHT_START` không được trùng `NIGHT_END`.

**Cách áp dụng:**
- Ngưỡng lux: áp dụng **ngay**. Chế độ ngày/đêm vẫn phải giữ `DAY_NIGHT_HOLD` mới đổi, nên kéo slider không làm đèn giật.
- Thời gian sáng dần/tối dần: áp dụng cho **lần chuyển độ sáng tiếp theo**; lần đang chạy dở giữ nguyên.
- Dashboard **chỉ gửi lệnh khi thả tay khỏi slider** (sự kiện `change`), không gửi liên tục lúc đang kéo (`input`),
  để tránh gửi hàng trăm tin MQTT và ghi NVS quá nhiều lần (flash có giới hạn số lần ghi).
- Board **chỉ ghi NVS khi giá trị thật sự thay đổi**.
- Nút **"Khôi phục mặc định"** gửi `{"reset": true}` → board nạp lại giá trị mặc định.
- Bên cạnh slider lux luôn hiển thị **lux hiện tại**, để người dùng biết nên đặt ngưỡng ở đâu (vd đang tối đo được 12 lux, đang sáng đo được 300 lux → đặt ngưỡng đêm ~40, ngày ~120).

### Tham số

| Tham số | Đề xuất | Ghi chú |
|---|---|---|
| `BASE_DIM` | 20% | Kiểm tra camera nhận diện được ở mức này; tối quá thì tăng lên 30% |
| `FULL` | 100% | |
| `LUX_NIGHT` / `LUX_DAY` | ~30 / ~100 lux *(hiệu chỉnh)* | **Chỉnh từ dashboard** |
| `LUX_MIN_GAP` | 20 lux | Khoảng cách tối thiểu giữa 2 ngưỡng |
| `DAY_NIGHT_HOLD` | 10 s (demo) | Ngoài đời nên 5–10 phút |
| `HOLD_PERSON` | 5 s | |
| `HOLD_VEHICLE` | 2 s | |
| `STOP_TIME` | 10 s (demo) | |
| `STOP_EPS` | 1 ô lưới FOMO | |
| `FOMO_SCORE_MIN` | 0.5 *(hiệu chỉnh)* | Bỏ tâm vật thể có score thấp |
| `VISION_TIMEOUT` | 2 s | |
| `NIGHT_START` / `NIGHT_END` | 18:00 / 06:00 | Chế độ theo giờ; chỉnh được từ dashboard |
| `TIMEZONE` | UTC+7 (giờ Việt Nam) | Cho SNTP |
| `CAMERA_CHECK_INTERVAL` | 5 phút | Tự kiểm tra camera ban ngày |
| `ALERT_REPEAT` | 30 phút | Nhắc lại cảnh báo lỗi chưa được sửa |
| `MANUAL_TIMEOUT` | 10 phút | Chế độ tay tự quay về tự động |
| `FADE_UP_MS` | 200 ms | Thời gian tăng sáng — **chỉnh từ dashboard** |
| `FADE_DOWN_MS` | 3000 ms | Thời gian giảm sáng — **chỉnh từ dashboard** |
| `PWM_FREQ` | 5 kHz | Tránh sọc trên ảnh camera; INA219 lấy trung bình đủ nhiều chu kỳ |
| `ENERGY_SAMPLE_INTERVAL` | 100 ms | Chu kỳ đọc INA219 |
| `ENERGY_SAVE_INTERVAL` | 10 phút | Chu kỳ lưu NVS |
| `TELEMETRY_INTERVAL` | 5 s | Chu kỳ gửi `streetlight/state` và `streetlight/energy` |
| `ALERT_JPEG_QUALITY` | khoảng 20–30 (esp32-camera, số nhỏ = đẹp hơn) | Cân đối với giới hạn kích thước tin nhắn MQTT |

## 5. Dataset

### 5.1 Ảnh cho Model 1 (Edge Impulse FOMO)

| Hạng mục | Yêu cầu |
|---|---|
| Nguồn ảnh | **Chính OV2640 lắp ở vị trí demo**, cùng định dạng pixel, **exposure/gain cố định**, **cùng cách cắt ROI** như firmware |
| Cách thu | Firmware có "chế độ thu ảnh" gửi ảnh ROI về laptop qua USB → upload lên Edge Impulse |
| Class | **`nguoi`, `xe`** — dùng đúng các món đồ chơi sẽ mang đi demo |
| Vị trí | Vật ở **cả 4 zone**, cả 2 chiều di chuyển, xoay nhiều góc, ở giữa và ở ranh giới 2 zone |
| Nhiều vật thể | 2–3 vật cùng lúc, sát nhau và cách xa |
| Tình huống | Vật **đang di chuyển** và **đứng yên lâu** |
| **Ánh sáng (ưu tiên)** | **≥ 70% ảnh khi tắt đèn phòng**: tất cả đèn 20%; 1–3 đèn 100% còn lại 20%; vật ở ranh giới vùng sáng/tối |
| Ánh sáng (phụ) | Một ít ảnh lúc bật đèn phòng |
| Negative | Mô hình trống ở 20% và 100%; **tay người đẩy xe** (để trong ảnh, **không gán nhãn**) |
| Kích thước vật | Sau khi cắt ROI và thu nhỏ, vật chiếm **≥ 8–10 px**; nhỏ hơn thì cắt ROI chặt hơn hoặc tăng input lên 160×160 |
| Số lượng | **≥ 150–300 vật thể mỗi class** |
| Gán nhãn | Vẽ bounding box trong Edge Impulse Studio (FOMO tự đổi thành tâm) |
| Chia tập | Theo **buổi chụp**, không chia ngẫu nhiên từng ảnh |
| Deploy | Xuất **C++ library / ESP-IDF, int8** |

**Thử sớm (giai đoạn 1):** chụp vài ảnh ở đèn 20%, train thử FOMO nhỏ, xem còn nhận ra đồ chơi không.
Quá tối thì tăng `BASE_DIM` **trước khi** chụp dataset thật. **Kiểm tra giới hạn gói miễn phí Edge Impulse** trước khi bắt đầu.

### 5.2 Hiệu chỉnh (không phải dataset)
- **BH1750:** đo lux ngày/đêm trên mô hình → `LUX_NIGHT`, `LUX_DAY`.
- **ROI và zone:** chụp 1 ảnh mô hình trống → chốt vùng cắt và khoảng x của 4 zone.
- **INA219:** `P100_i` từng bóng, kiểm tra chéo với đồng hồ vạn năng (mục 4).

## 6. Đánh giá (đưa vào báo cáo)

| Chỉ số | Cách đo |
|---|---|
| Độ chính xác FOMO (precision/recall, F1) | Tập test trong Edge Impulse |
| Độ chính xác zone trên board | Đặt vật ở từng zone N lần, đếm số lần board báo đúng zone |
| Tốc độ suy luận (fps), RAM, Flash | Log trên board, `/profile-memory` |
| Độ trễ từ lúc vật vào zone đến lúc đèn sáng | Quay video mô hình, đếm frame |
| **Sai số đo dòng INA219** | So với đồng hồ vạn năng ở 100% cho từng bóng |
| **% điện năng tiết kiệm (đo thật)** | Kịch bản demo cố định (vd 10 phút đêm, N lượt xe/người), so với sáng 100% |

## 7. Câu hỏi còn mở

- [ ] Danh sách đồ chơi cụ thể.
- [ ] Board camera còn đủ 4 chân PWM + 2 chân I2C không, hay cần PCA9685.
- [ ] Đo lux, fps FOMO, thử camera ở đèn 20% → chốt các tham số.

## Lịch sử thay đổi

- 2026-09-14 → 2026-09-15: bản đầy đủ gồm 2 model, FC-51, Telegram, ESP-NOW... (đã bỏ).
- 2026-09-15: **viết lại cho Mức 1**: chỉ FOMO `nguoi`/`xe`, luật theo loại đối tượng, ngày/đêm, cảnh báo vật dừng kèm ảnh trên dashboard.
- 2026-09-15: nguồn đèn là adapter 5 V riêng.
- 2026-09-15: **giảm còn 4 đèn / 4 zone**; xe sáng K + 1 zone phía trước; **đo điện năng thật từng bóng bằng INA219**
  thay cho ước tính, thêm lịch sử 24 giờ.
- 2026-09-15: camera/FOMO hoặc BH1750 lỗi → **chế độ theo giờ**: 06:00–18:00 chỉ cảnh báo (đèn tắt), 18:00–06:00 sáng 100%;
  thêm tự kiểm tra camera ban ngày; mất giờ thì sáng 100%.
- 2026-09-15: chuyển độ sáng **tăng nhanh (~200 ms), giảm từ từ (~3 s)**, áp dụng cho mọi thay đổi độ sáng.
- 2026-09-15: ngưỡng lux, thời gian sáng dần/tối dần, giờ ban đêm **chỉnh được từ dashboard** (kiểm tra giới hạn, lưu NVS).
- 2026-09-15: làm rõ vùng trễ (lux giữa 2 ngưỡng giữ nguyên chế độ) và quy tắc chọn chế độ lúc khởi động.
