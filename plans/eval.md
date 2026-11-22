# Đánh giá hệ thống đèn đường thông minh (Mức 1)

Mục đích: liệt kê các thông số cần đo, cách đo, mục tiêu và **bảng để điền kết quả** khi thử trên mô hình.
Kết quả ở đây dùng thẳng cho chương "Đánh giá" của báo cáo.

- Board: Freenove ESP32-S3 WROOM CAM N8R8, firmware `streetlight-node`
- Model: FOMO int8, 2 lớp `nguoi` / `xe`
- Mục tiêu trong các bảng là đề xuất, có thể chỉnh. Ghi rõ nếu đổi.

**Bắt buộc có trong báo cáo:** F1 của FOMO (1.1), nhận diện và tốc độ trên board (2.1, 2.2), độ trễ phản hồi (3.1),
sai số INA219 (4.2), **% tiết kiệm điện (4.3)** và độ trễ qua MQTT (6.3, 6.4). Các mục còn lại chứng minh hệ thống chạy ổn định.

## Tổng hợp kết quả

Điền sau khi đo xong từng mục.

| # | Thông số | Mục tiêu | Kết quả | Đạt? |
|---|---|---|---|---|
| **1** | **Model AI (Edge Impulse)** | | | |
| 1.1 | F1 của FOMO | ≥ 0,85 | 0,90 (validation, int8); 0,89 (test, float32) | ✅ |
| 1.2 | F1 giảm do int8 so với float | ≤ 3% | không giảm (90,3% so với 90,1%) | ✅ |
| **2** | **AI chạy trên board** | | | |
| 2.1 | Nhận diện trên board (quan sát, chỉ trong zone) | nhận đúng vật trong zone | Zone gần tốt; **zone 4 (xa) hay bỏ sót và nhận nhầm**; vật cùng lớp đứng sát nhau bị gộp thành 1 | ⚠️ |
| 2.2 | Tốc độ suy luận | ≥ 3 fps | **3,8 fps** khi chạy thật (log `vision`); inference **không đổi theo số vật** (0–5 vật: 241–255 ms); ai-stream có người xem ≈ 3,0 fps | ✅ |
| 2.3 | Tìm đường thành công lúc khởi động | ≥ 90% | **26/30 = 86,7%** (đèn phòng bật 8/10, tắt 9/10, có vật 9/10); hỏng do chói | ⚠️ thiếu 3,3% |
| **3** | **Điều khiển đèn** | | | |
| 3.1 | Độ trễ từ lúc vật vào zone đến lúc đèn sáng | < 1 s | TB **596 ms** từ khung ảnh (456–663 ms, 17 lần, log qua đêm) | ✅ |
| 3.2 | Chu kỳ điều khiển lớn nhất | < 200 ms | 54,7 ms đường trống; 66,0 ms tải nặng; **126,5 ms lớn nhất qua 9,7 giờ** (63%, 1 lần) | ✅ |
| 3.3 | Đúng luật bật đèn | 100% | **6/6 kịch bản = 100%** | ✅ |
| **4** | **Điện năng (INA219)** | | | |
| 4.1 | Công suất 4 bóng ở 100% / 20% | ghi số đo thật | **259,4 mW / 55,6 mW** (20% ≈ 21,4% của 100%) | ✅ |
| 4.2 | Sai số dòng INA219 | ≤ 5% | | |
| 4.3 | **% tiết kiệm điện** | ghi số đo thật | **74,4%** A/B 10 phút (43 → 11 mWh, ±5% do làm tròn); **76,8%** cả đêm 9,65 giờ (580 mWh so với 2503 mWh nếu sáng 100%); 78,8% lúc vắng, ≈ 49% lúc có xe | ✅ |
| **5** | **Cảnh báo và xử lý lỗi** | | | |
| 5.1 | Xử lý đúng khi cảm biến / đèn lỗi | 100% | Rút BH1750 → web "Không đọc được cảm biến … (mất kết nối)", theo giờ ✅; BH1750 tối ban ngày → cảnh báo `bh1750` ✅; mất WiFi → đèn vẫn chạy, web "Board ESP32 offline" ✅; rút camera (ép đêm) → web "Camera hoặc model nhận diện lỗi: đèn chạy theo giờ" ✅; rút bóng → báo sau 60 s, cắm lại hết báo sau 10 s ✅ | ✅ (5/5; rút camera ban ngày chưa thử lại) |
| 5.2 | Rút bóng → báo đèn hỏng | 60–65 s | **60 s** (cắm lại → hết báo sau **10 s**), 1 lần | ✅ |
| **6** | **Hệ thống và mạng** | | | |
| 6.1 | RAM / Flash đã dùng | RAM ≤ 80%, Flash ≤ 90% | RAM nội **86,7%** lúc cao nhất qua 9,7 giờ (77% bình thường), PSRAM 13,2%, Flash 40,6% | ⚠️ RAM vượt |
| 6.2 | Số lần tự khởi động lại khi chạy 12–24 giờ | 0 | **0** trong **9,7 giờ** (01:40–11:19), heap đi ngang | ✅ (chưa đủ 12 giờ) |
| 6.3 | Độ trễ từ board lên dashboard | < 2 s | **≈ 38 ms** (round trip TB 76 ms, trung vị 71 ms, 485 lần); lần trước 43 ms (18 lần) | ✅ |
| 6.4 | Độ trễ từ lúc chỉnh trên web đến lúc đèn đáp ứng | < 1 s | TB **478 ms** (379–575 ms, 15 lần) | ✅ |

---

## 1. Model AI (FOMO, đánh giá trên Edge Impulse)


### 1.1 Kết quả trên Edge Impulse

Ngày lấy số: 2026-11-21. Edge Impulse gọi lớp `xe` là CAR, `nguoi` là HUMAN; các chỉ số "non-background"
chỉ tính 2 lớp vật, không tính nền.

**a) Tập validation, lúc train (bản int8, quantized)**

Ma trận nhầm lẫn (hàng = nhãn thật, cột = model đoán):

| Thật \ Đoán | Nền | Xe | Người |
|---|---|---|---|
| Nền | 100,0% | 0,0% | 0,0% |
| Xe | 9,0% | **91,0%** | 0% |
| Người | 26,9% | 0% | **73,1%** |

| Lớp | Precision | Recall | F1 |
|---|---|---|---|
| xe | ≈ 1,00 | 0,91 | 0,95 |
| nguoi | ≈ 1,00 | 0,73 | 0,84 |
| **Chung (non-background)** | **1,00** | **0,83** | **0,90** (F1 score 90,3%) |

Precision từng lớp suy ra từ ma trận: cột Xe và cột Người ở hàng Nền đều 0%, và xe với người không bị nhầm
sang nhau, nên model gần như không báo vật khi không có.

**b) Tập test (trang *Model testing*)**

Ảnh chụp màn hình đang chọn **Model version: Unoptimized (float32)**, nên đây là số của bản float32.

| Chỉ số | Giá trị |
|---|---|
| Accuracy | 79,86% |
| Precision (non-background) | 1,00 |
| Recall (non-background) | 0,81 |
| **F1 (non-background)** | **0,89** |

> Cần bổ sung: chạy lại *Model testing* với **Quantized (int8)**, bản đang chạy trên board, rồi ghi
> vào đây.

**Nhận xét**

- Precision ≈ 1,00: model **gần như không báo nhầm** (không bật đèn khi không có vật). Với đèn đường, đây là
  điểm quan trọng vì báo nhầm làm tốn điện.
- Điểm yếu là **recall của người (0,73)**: khoảng 27% người bị bỏ sót (bị coi là nền). Người trong mô hình
  nhỏ hơn xe nhiều, nên chỉ chiếm 1–2 ô FOMO (≈ 20 px mỗi ô). Hướng cải thiện: thêm ảnh người cho dataset,
  tăng độ phân giải đầu vào, hoặc giữ đèn sáng thêm vài giây sau lần nhận diện cuối (firmware đang làm vậy)
  để bù cho những khung bị sót.
- Accuracy trên tập test (79,86%) thấp hơn F1 vì Edge Impulse tính accuracy theo từng ảnh: ảnh có nhiều vật
  mà sót 1 vật vẫn bị tính điểm thấp (ví dụ ảnh "car, human" chỉ được 66%).

### 1.2 Ảnh hưởng của lượng tử hoá int8

So trên **cùng tập validation** (số lúc train):

| Bản model | F1 | Precision | Recall | Xe đúng | Người đúng |
|---|---|---|---|---|---|
| float32 (unoptimized) | 90,1% | 0,99 | 0,83 | 90,6% | 73,4% |
| int8 (quantized) | 90,3% | 1,00 | 0,83 | 91,0% | 73,1% |
| **Chênh lệch** | **+0,2%** | +0,01 | 0 | +0,4% | −0,3% |

**Nhận xét:** lượng tử hoá int8 **không làm giảm độ chính xác** (chênh lệch ±0,4%, trong mức dao động
ngẫu nhiên), trong khi model nhỏ hơn khoảng 4 lần và chạy nhanh hơn trên ESP32-S3. Vì vậy board dùng bản int8.

Muốn so trên tập test thì chạy *Model testing* cho cả 2 bản và điền thêm.

---

## 2. AI chạy trên board


### 2.1 Nhận diện trên board (quan sát thực tế)

Quan sát trên trang ai-stream (nhấn IO0 hai lần) và log của board. **Chỉ xét vật trong zone** (mặt đường + vỉa
hè); vật nhận nhầm bên ngoài zone bỏ qua vì không làm bật đèn.

Board: model FOMO int8, đầu vào 128×128 (cắt vùng mặt đường từ khung camera 320×240 rồi thu nhỏ), FOMO chia ảnh
thành lưới 16×16 ô, mỗi ô ≈ 20 px trên khung camera.

**Kết quả:**

| Hiện tượng | Quan sát | Nguyên nhân |
|---|---|---|
| **Zone càng xa càng khó nhận diện** (rõ nhất zone 4) | Vật ở zone 4 hay bị bỏ sót, và nền ở zone 4 hay bị nhận nhầm thành vật | Camera nhìn chéo nên vật ở xa nhỏ hơn nhiều, chỉ chiếm khoảng 1 ô FOMO hoặc ít hơn; nền phía xa rối, nhiều chi tiết dồn vào ít điểm ảnh |
| **Người đứng sát nhau / xe sát nhau chỉ nhận 1** | 2 vật cùng lớp cách nhau chưa tới khoảng 1 ô → 1 chấm tâm nằm giữa 2 vật | FOMO không vẽ khung từng vật mà chỉ đánh dấu ô; thư viện Edge Impulse gộp các ô liền nhau cùng lớp thành 1 vật |

**Bằng chứng trong log qua đêm 2026-11-22** (`log.streetlight-node.20261122014008.txt`):

- 3/5 cảnh báo xe dừng là nhầm, cả 3 đều ở **zone 4** (độ tin 53–62%, ảnh lúc chụp không có xe).
- Lúc đường vắng 02:00–11:00 có 5 lần đèn nháy sáng do nhận nhầm thoáng qua (xem 6.2).

**Ảnh hưởng tới hệ thống:**

| Chức năng | Ảnh hưởng |
|---|---|
| Bật đèn theo zone | Vật sát nhau: **không ảnh hưởng** (luật bật theo zone có vật, 1 hay 2 vật trong zone như nhau). Zone xa: bỏ sót → đèn zone 3–4 có thể bật trễ; nhận nhầm → đèn 3–4 nháy sáng vô ích |
| Đếm người/xe (dashboard, log `vision`) | Đếm thiếu khi vật sát nhau |
| Cảnh báo xe dừng | 2 xe đậu sát nhau thành 1 "Xe"; nhận nhầm ở zone 4 gây báo nhầm |

**Hướng cải thiện** (chưa làm):

| Cách | Ưu điểm | Nhược điểm |
|---|---|---|
| Thêm ảnh xe/người ở xa và đứng sát nhau vào dataset, train lại | Sửa đúng gốc, không tốn tài nguyên board | Tốn công chụp, gán nhãn |
| Tăng đầu vào lên 160×160 (lưới 20×20) | Vật xa nhiều điểm ảnh hơn, tách vật sát nhau tốt hơn | Suy luận chậm hơn (đang 208 ms ở 128×128), phải train lại |
| Firmware: vật phải xuất hiện ≥ 2 khung liên tiếp, hoặc ngưỡng độ tin cao hơn cho zone 4 | Giảm nhận nhầm ngay, không cần train | Không giảm bỏ sót; đèn bật chậm thêm 1 khung (≈ 0,26 s) |
| Model có khung bao (SSD, YOLO nhỏ) | Tách được vật sát nhau | Nặng hơn FOMO nhiều lần, ESP32-S3 có thể dưới 1 fps |

**Kết luận:** FOMO ưu tiên tốc độ (3,8 fps trên ESP32-S3), đổi lại khó với **vật nhỏ ở xa** và **vật đứng sát
nhau**. Với luật bật đèn theo zone thì giới hạn thứ hai gần như không ảnh hưởng; giới hạn thứ nhất làm zone 4 kém
tin cậy hơn các zone gần.

### 2.2 Tốc độ

Nguồn: log `ai_pipeline` trên board (ESP32-S3, 240 MHz, model int8), 1 vật trong khung:

```
I (9151) ai_pipeline: cycle 277 ms (resize 4821 us, dsp 5282 us, inference 208573 us), 1 object(s)
```

**Thời gian 1 vòng xử lý: 277 ms → 1000 / 277 ≈ 3,6 fps**

| Bước | Thời gian (ms) | % của 1 vòng |
|---|---|---|
| Resize (ảnh camera → ảnh đầu vào model) | 4,8 | 1,7% |
| DSP (chuẩn hoá ảnh, đưa vào tensor) | 5,3 | 1,9% |
| **Inference (FOMO int8)** | **208,6** | **75,3%** |
| Phần còn lại: chụp ảnh camera, hậu xử lý (tìm tâm vật, gán zone) | 58,3 | 21,0% |
| **Tổng 1 vòng** | **277,0** | 100% |

"Phần còn lại" = 277 − (4,8 + 5,3 + 208,6). Log không đo riêng từng việc trong đó.

**Tốc độ khi chạy thật (firmware `streetlight-node`, chế độ bình thường):** log `vision` in fps trung bình
mỗi 2 s, ổn định ở **3,8 fps** (dao động 3,8–3,9), tức khoảng **263 ms / khung**:

```
I (319103) node: vision: 3.8 fps, road found, 0 person(s) in zones 0x0, 0 vehicle(s) in zones 0x0
I (325103) node: vision: 3.9 fps, road found, 0 person(s) in zones 0x0, 0 vehicle(s) in zones 0x0
```

Con số 3,6 fps ở trên lấy từ 1 vòng lẻ (277 ms, có 1 vật). 3,8 fps là trung bình nhiều vòng, nên dùng
số này trong báo cáo.

**Nhận xét:** inference chiếm khoảng 3/4 thời gian, nên muốn nhanh hơn phải giảm kích thước đầu vào hoặc
dùng model nhẹ hơn. 3,8 fps nghĩa là board thấy vật chậm nhất khoảng 0,26 s sau khi vật xuất hiện, đủ cho
mô hình xe đồ chơi (đèn còn tăng sáng mất khoảng 200 ms).

**Thời gian có đổi theo số vật không?** Đo 2026-11-22 ở chế độ ai-stream (nhấn IO0 hai lần), điện thoại mở trang
ai-stream, đặt lần lượt 5 → 0 vật. Mỗi dòng `cycle` in sau 20 khung:

```
I (25921) ai_pipeline: cycle 276 ms (resize 4886 us, dsp 5267 us, inference 208768 us), 3 object(s)
I (32505) ai_pipeline: cycle 342 ms (resize 7077 us, dsp 5261 us, inference 253263 us), 5 object(s)
I (39262) ai_pipeline: cycle 342 ms (resize 4808 us, dsp 6509 us, inference 252923 us), 5 object(s)
I (45994) ai_pipeline: cycle 330 ms (resize 4902 us, dsp 6008 us, inference 245964 us), 4 object(s)
I (52726) ai_pipeline: cycle 331 ms (resize 5962 us, dsp 5263 us, inference 240731 us), 3 object(s)
I (59513) ai_pipeline: cycle 344 ms (resize 4906 us, dsp 7739 us, inference 249673 us), 2 object(s)
I (66250) ai_pipeline: cycle 346 ms (resize 4811 us, dsp 7609 us, inference 254653 us), 2 object(s)
I (72812) ai_pipeline: cycle 327 ms (resize 8376 us, dsp 6342 us, inference 240618 us), 1 object(s)
I (79411) ai_pipeline: cycle 338 ms (resize 4809 us, dsp 7782 us, inference 246651 us), 0 object(s)
```

| Số vật | Số dòng | cycle (ms) | inference (ms) |
|---|---|---|---|
| 0 | 1 | 338 | 246,7 |
| 1 | 1 | 327 | 240,6 |
| 2 | 2 | 345 | 252,2 |
| 3 | 1 | 331 | 240,7 |
| 4 | 1 | 330 | 246,0 |
| 5 | 2 | 342 | 253,1 |
| **0–5 vật** | 8 | **327–346** (TB 338) | **240,6–254,7** (TB 248,1) |
| 3 vật, trang ai-stream chưa nhận ảnh (dòng đầu) | 1 | 276 | 208,8 |

**Nhận xét:**

- **Thời gian không tăng theo số vật.** 0 vật (246,7 ms) và 5 vật (253,1 ms) chênh chỉ khoảng 6 ms, nằm trong mức dao
  động ngẫu nhiên (1 vật lại thấp nhất 240,6 ms, 2 vật cao nhất 254,7 ms). Đúng như lý thuyết: CNN của FOMO luôn tính
  trên toàn bộ ảnh 128×128 với số phép tính cố định; số vật chỉ ảnh hưởng phần hậu xử lý (gộp ô), rất nhỏ.
- **Cái làm chậm là WiFi, không phải số vật.** Dòng đầu (lúc điện thoại vừa kết nối, chưa nhận luồng ảnh) inference
  208,8 ms; khi trang bắt đầu nhận ảnh liên tục thì lên khoảng 248 ms (+40 ms, +19%) dù số vật bất kỳ. Lý do khả dĩ:
  WiFi và việc gửi ảnh JPEG chiếm CPU và băng thông PSRAM (tensor arena của model nằm ở PSRAM) cùng lúc với
  inference.
- Ở chế độ bình thường (không stream ảnh) board vẫn đạt 3,8 fps như ở trên; ở ai-stream khi có người xem trang thì
  khoảng 1000 / 338 ≈ **3,0 fps**.
- Có 1 dòng `cam_hal: EV-VSYNC-OVF` (mất 1 khung), không ảnh hưởng kết quả.

### 2.3 Tìm đường lúc khởi động

Cách đo: khởi động lại board 10 lần ở mỗi điều kiện sáng, đọc log `road found` / `no road`.

| Điều kiện sáng | Số lần | road found | Ghi chú |
|---|---|---|---|
| Đèn phòng bật | 10 | 8 | 2 lần hỏng: đèn phòng chói trên mặt đường nên không tìm được cạnh của zone |
| Đèn phòng tắt (chỉ đèn mô hình) | 10 | 9 | 1 lần hỏng: mất 1 phần zone 1 |
| Có vật trên đường | 10 | 9 | giống trường hợp đèn phòng tắt |
| **Tổng** | **30** | **26 (86,7%)** | |

**Nhận xét:** tổng 26/30 = **86,7%**, thấp hơn mục tiêu 90% một chút. Lỗi chủ yếu do **ánh sáng chói**: đèn phòng
phản chiếu trên mặt đường làm mất cạnh vạch giới hạn, nên thuật toán không tách được zone. Có vật trên đường không
làm kết quả kém đi (9/10, bằng lúc tắt đèn phòng). Hướng cải thiện: hiệu chỉnh đường lúc tắt đèn phòng / tránh
nguồn sáng chiếu thẳng xuống mặt đường, hoặc lưu kết quả hiệu chỉnh tốt vào NVS để dùng lại khi lần tìm sau thất bại.

---

## 3. Điều khiển đèn


### 3.1 Độ trễ từ lúc vật vào zone đến lúc đèn sáng

**Từ log của board** (dòng `latency`, in mỗi khi một đèn từ mức nền bật lên 100%), ngày 2026-11-22:

```
I (205795) node: latency: lamp 2 on, 4 object(s) | FOMO 252 ms + wait control 81 ms + control->PWM 206 ms = 539 ms from frame
I (435795) node: latency: lamp 2 on, 3 object(s) | FOMO 252 ms + wait control 57 ms + control->PWM 207 ms = 516 ms from frame
```

| Lần | Đèn | FOMO (ms) | Chờ vòng điều khiển (ms) | Điều khiển → PWM (ms) | Tổng từ khung ảnh (ms) |
|---|---|---|---|---|---|
| 1 | 2 | 252 | 81 | 206 | 539 |
| 2 | 2 | 252 | 57 | 207 | 516 |
| **TB** | | **252** | **69** | **207** | **528** |

**Lần đo 2 — log chạy qua đêm 2026-11-22 01:40–11:19** (`log.streetlight-node.20261122014008.txt`), 17 lần.
DÙNG SỐ NÀY (nhiều lần hơn). Giờ là giờ board (SNTP).

| Lần | Giờ | Đèn bật | Số vật | FOMO (ms) | Chờ vòng điều khiển (ms) | Điều khiển → PWM (ms) | Tổng từ khung ảnh (ms) |
|---|---|---|---|---|---|---|---|
| 1 | 01:44 | 2,3,4 | 4 | 303 | 63 | 206 | 572 |
| 2 | 01:45 | 2 | 4 | 322 | 17 | 206 | 545 |
| 3 | 01:45 | 3,4 | 3 | 320 | 115 | 207 | 642 |
| 4 | 01:46 | 3,4 | 3 | 254 | 163 | 206 | 623 |
| 5 | 01:46 | 3,4 | 3 | 255 | 148 | 206 | 609 |
| 6 | 01:46 | 3,4 | 3 | 254 | 183 | 206 | 643 |
| 7 | 01:46 | 3,4 | 3 | 258 | 170 | 206 | 634 |
| 8 | 01:46 | 3,4 | 3 | 255 | 155 | 206 | 616 |
| 9 | 01:54 | 3,4 | 3 | 252 | −2 | 206 | 456 |
| 10 | 02:40 | 1,2 | 1 | 255 | 156 | 206 | 617 |
| 11 | 05:40 | 1,2 | 1 | 308 | 44 | 206 | 558 |
| 12 | 05:49 | 1,2 | 2 | 254 | 164 | 206 | 624 |
| 13 | 06:11 | 3,4 | 1 | 252 | 117 | 206 | 575 |
| 14 | 10:40 | 1,2,3 | 1 | 313 | 144 | 206 | 663 |
| 15 | 11:11 | 3,4 | 3 | 314 | 117 | 206 | 637 |
| 16 | 11:11 | 2 | 3 | 263 | 53 | 207 | 523 |
| 17 | 11:11 | 1 | 7 | 256 | 134 | 206 | 596 |
| **TB** | | | | **276** | **114** | **206** | **596** |
| Nhỏ nhất / lớn nhất | | | | 252 / 322 | −2 / 183 | 206 / 207 | 456 / 663 |

- "Chờ vòng điều khiển" TB 114 ms, khớp với lý thuyết (đều trong 0–200 ms → TB ≈ 100 ms). Lần 1 ở trên (TB 69 ms)
  chỉ có 2 mẫu nên thấp hơn. Số −2 ms ở lần 9 là sai số làm tròn mốc giờ (kết quả tới đúng lúc vòng điều khiển chạy).
- "Điều khiển → PWM" luôn 206–207 ms = đúng 1 vòng 200 ms, xác nhận nguyên nhân ở trên (thứ tự `update_lighting`
  / `update_vision`).
- FOMO 252–322 ms: những lần > 300 ms rơi vào khung có nhiều vật hoặc lúc board đang gửi ảnh cảnh báo.
- Trường hợp xấu nhất: 663 + 263 (vật vào ngay sau lúc chụp) + 200 (tăng sáng) ≈ **1,13 s** đến lúc đèn sáng hẳn;
  đèn **bắt đầu** sáng vẫn dưới 1 s (≤ 926 ms). Đổi thứ tự 2 hàm sẽ bớt 200 ms.
- Các lần 10–14 (02:40–10:40, 1 vật) xảy ra lúc không ai thử: nhiều khả năng là **nhận nhầm** (xem 6.2).

- **FOMO:** từ lúc lấy khung khỏi camera đến lúc có kết quả nhận diện.
- **Chờ vòng điều khiển:** kết quả nằm chờ đến khi vòng điều khiển 200 ms đọc (0–200 ms tuỳ lúc).
- **Điều khiển → PWM ≈ 1 vòng (200 ms):** trong vòng điều khiển, `update_lighting` chạy trước
  `update_vision`, nên phát hiện mới phải đợi vòng sau mới ra lệnh PWM. Đảo thứ tự 2 hàm này sẽ bớt khoảng
  200 ms (tổng còn khoảng 330 ms).
- Tổng này **chưa tính** khoảng vật đã vào zone trước khi camera chụp (0 đến 1 khung ≈ 263 ms) và thời gian
  đèn tăng sáng (200 ms). Trường hợp xấu nhất ≈ 528 + 263 + 200 ≈ **1,0 s** đến lúc đèn sáng hẳn.

### 3.2 Chu kỳ điều khiển

Log in mỗi 2 s:

```
I (...) node: control cycle max 54109 us (since boot 58368 us) of 200 ms
```

- `max …` = lớn nhất trong 2 s vừa qua (đặt lại sau mỗi lần in) → xem từng lúc.
- `since boot …` = **lớn nhất từ lúc khởi động** → chạy hết kịch bản rồi đọc 1 dòng là có kết quả cả phiên.

| Trường hợp | Lớn nhất (ms) | % của 200 ms |
|---|---|---|
| Đường trống (log 2026-11-21) | 54,7 | 27% |
| Có 3 xe trên đường (log 2026-11-21, ngay sau khởi động) | 58,4 | 29% |
| Tải nặng: 3 xe + 1 người + cảnh báo xe dừng + gửi ảnh JPEG 9 KB qua MQTT (log 2026-11-22) | **66,0** | **33%** |

Log lúc tải nặng (chu kỳ lớn nhất rơi vào 2 s board phát cảnh báo, chụp và gửi ảnh):

```
W (82960) node: xe stopped in zone 1 for 78 s (99%) -> alert + picture
I (82966) node: vision: 3.7 fps, road found, 1 person(s) in zones 0x2, 3 vehicle(s) in zones 0xB
I (83161) node: alert picture: 3 vehicle(s) numbered left to right
I (83332) node: MQTT -> streetlight/alert/image (9081 B)
I (84961) node: control cycle max 65981 us (since boot 84790 us) of 200 ms
I (86961) node: control cycle max 55005 us (since boot 84790 us) of 200 ms
```

**Nhận xét:** lúc tải nặng nhất chu kỳ chỉ tăng thêm khoảng 11 ms (55 → 66 ms), vẫn dưới 1/3 thời gian
cho phép, và trở lại 55 ms ngay sau đó. Vì mã hoá JPEG chạy ở task camera và gửi MQTT ở task mạng, không chạy
trong vòng điều khiển, nên tải nặng ở các phần khác gần như không làm chậm việc điều khiển đèn.

Lưu ý: số `since boot` gồm cả lúc khởi động (kết nối WiFi, hiệu chỉnh đường). Muốn số riêng cho tải nặng
thì đọc các dòng `max …` trong lúc có cảnh báo.

**Chạy qua đêm 9,7 giờ (2026-11-22 01:40–11:19, 17 356 dòng `control cycle max`):**

| | Giá trị (ms) | % của 200 ms |
|---|---|---|
| Trung bình các dòng `max` | 61,6 | 31% |
| Trung vị | 63,5 | 32% |
| 99% số dòng dưới | 77,1 | 39% |
| **Lớn nhất cả phiên (`since boot`)** | **126,5** | **63%** |

- 126,5 ms chỉ xảy ra **1 lần** (lúc 03:00, uptime 4927 s), ngay sau khi board ghi điện năng vào NVS (`energy
  saved to NVS (new hour)`, uptime 4925 s). Ghi flash làm tạm dừng cache nên các task đều chậm lại (đây là giả
  thuyết, chưa đo riêng). 9 lần ghi "new hour" khác không gây đỉnh như vậy. Ngoài lần này, chỉ 2 dòng vượt 85 ms
  (89,4 ms lúc 11:02 và 11:03).
- Vẫn còn cách giới hạn 200 ms khá xa → **đạt**, không có vòng nào trễ hạn trong cả đêm.

### 3.3 Đúng luật bật đèn

Luật (theo code `zone_rule.c`): ban đêm nền theo `base_pct`; người hoặc xe ở zone K → đèn K−1..K+1 100%
(xe giữ sáng lâu hơn người). Ban ngày tắt hết.
Lưu ý: kế hoạch ban đầu ghi "xe → K + 1 zone phía trước", code hiện bật cả 2 bên vì camera không biết chắc hướng xe.

| Kịch bản | Đèn phải sáng 100% | Đèn thực tế | Đúng? |
|---|---|---|---|
| Người ở zone 1 | 1, 2 | 1, 2 | ✅ |
| Người ở zone 3 | 2, 3, 4 | 2, 3, 4 | ✅ |
| Xe ở zone 2 | 1, 2, 3 | 1, 2, 3 | ✅ |
| Xe ở zone 4 | 3, 4 | 3, 4 | ✅ |
| Người zone 1 + xe zone 3 | 1, 2, 3, 4 | 1, 2, 3, 4 | ✅ |
| Ban ngày, có xe | không đèn nào | không đèn nào | ✅ |
| **Tổng** | | | **6/6 = 100%** |

**Nhận xét:** cả 6 kịch bản đèn sáng đúng luật, kể cả trường hợp 2 vật ở 2 zone (đèn bật là hợp của 2 vùng) và
ban ngày có xe (AI dừng, đèn tắt).

---

## 4. Điện năng (INA219)


### 4.1 Công suất từng bóng ở 100% (P100) và 20%

Số đo trên dashboard, ngày 2026-11-21, chỉnh tay cả 4 đèn 100%, nguồn đèn 5 V riêng. Đây là mức so sánh
(sáng 100% suốt đêm) cho phép tính % tiết kiệm ở 4.3.

| Bóng | Điện áp (V) | Dòng (mA) | Công suất (mW) |
|---|---|---|---|
| 1 | 5,13 | 12,4 | 63,6 |
| 2 | 5,12 | 12,6 | 64,6 |
| 3 | 5,13 | 13,0 | 66,7 |
| 4 | 5,12 | 12,6 | 64,5 |
| **Tổng 4 bóng** | | **50,6** | **259,4** |

- 4 bóng chênh nhau tối đa khoảng 5% (63,6 → 66,7 mW), do sai khác giữa các LED.
- Sáng 100% suốt **1 giờ**, 4 bóng tốn khoảng **259 mWh**.
- Board tự đo P100 và lưu vào NVS (`p100_mw`), dùng để báo đèn hỏng và tính "Tiết kiệm" trên dashboard.

**Khi chạy ở mức nền 20%** (cùng ngày, cả 4 đèn 20%):

| Bóng | Điện áp (V) | Dòng (mA) | Công suất (mW) | So với P100 |
|---|---|---|---|---|
| 1 | 5,15 | 2,6 | 13,4 | 21,1% |
| 2 | 5,15 | 3,2 | 16,5 | 25,5% |
| 3 | 5,15 | 2,6 | 13,4 | 20,1% |
| 4 | 5,14 | 2,4 | 12,3 | 19,1% |
| **Tổng 4 bóng** | | **10,8** | **55,6** | **21,4%** |

- Công suất ở duty 20% xấp xỉ 20% công suất lúc 100%: với PWM, công suất tỉ lệ gần tuyến tính với duty.
- Nếu đường vắng suốt đêm (cả 4 bóng chỉ ở mức nền 20%), mức tiết kiệm tối đa ≈ 1 − 55,6 / 259,4 ≈ **78,6%**.
  Có xe/người thì đèn lên 100% một lúc, nên % tiết kiệm thực tế thấp hơn mức này.
- Bóng 2 tốn hơn các bóng còn lại (25,5% so với khoảng 20%); có thể do LED hoặc do sai số đo ở dòng nhỏ.

### 4.2 Sai số dòng và áp

Cách đo: mắc đồng hồ vạn năng nối tiếp với từng bóng, chế độ chỉnh tay trên dashboard, đọc INA219
trên dashboard hoặc log. Sai số = |INA − đồng hồ| / đồng hồ × 100%.

| Bóng | Duty | I đồng hồ (mA) | I INA219 (mA) | Sai số I | V đồng hồ (V) | V INA219 (V) | Sai số V |
|---|---|---|---|---|---|---|---|
| 1 | 100% | | | | | | |
| 1 | 50% | | | | | | |
| 1 | 20% | | | | | | |
| 2 | 100% | | | | | | |
| 3 | 100% | | | | | | |
| 4 | 100% | | | | | | |

Lưu ý: ở duty thấp, đồng hồ vạn năng đọc giá trị trung bình của PWM 2 kHz, còn INA219 lấy trung bình
128 mẫu, nên sai số ở 20% thường lớn hơn.

### 4.3 % tiết kiệm điện

Cách đo: chạy cùng một kịch bản 2 lần, mỗi lần 10 phút, ép chế độ đêm:

- **Lần A (so sánh):** chỉnh tay cả 4 đèn 100%.
- **Lần B (thông minh):** chế độ tự động, nền `base_pct` = 20%.

Kịch bản cố định: N lượt xe, M lượt người đi qua theo cùng thứ tự, cùng thời điểm (ghi lại).
Điện năng lấy từ `today` (mWh) trên dashboard: ghi lúc bắt đầu và lúc kết thúc, lấy hiệu.

| Bóng | E lần A (mWh) | E lần B (mWh) | Tiết kiệm từng bóng |
|---|---|---|---|
| 1 | 10 | 2 | 80,0% |
| 2 | 11 | 3 | 72,7% |
| 3 | 11 | 3 | 72,7% |
| 4 | 11 | 3 | 72,7% |
| **Tổng** | **43** | **11** | **74,4%** |

% tiết kiệm = (E_A − E_B) / E_A × 100% = (43 − 11) / 43 × 100% ≈ **74,4%**

- Kiểm tra lần A: 259,4 mW (P100 ở 4.1) × 10 phút = 259,4 × 1/6 ≈ **43,2 mWh**, khớp 43 mWh đo được.
- Lần B: 11 mWh / (1/6 h) ≈ **66 mW** trung bình, cao hơn mức nền 55,6 mW vì có lúc đèn lên 100% khi có vật.
- **Sai số làm tròn:** dashboard hiện `today` đến 1 mWh, mỗi bóng sai tới ±0,5 mWh → tổng sai tới ±2 mWh. Vì vậy
  % tiết kiệm nằm trong khoảng **≈ 70–79%**; khác biệt giữa các bóng (80% so với 72,7%) chủ yếu do làm tròn, không
  phải bóng 1 tiết kiệm hơn. Muốn chính xác hơn: chạy lâu hơn (30–60 phút) hoặc lấy `today` từ log (1 chữ số thập phân).
- Kết quả gần với lúc đường vắng qua đêm (78,8%), nghĩa là kịch bản lần này lưu lượng thấp.
- Cần ghi thêm: số lượt xe/người (N, M) trong 10 phút để điền bảng mật độ bên dưới.

Nên làm thêm 1 lần với mật độ xe cao hơn để thấy % tiết kiệm phụ thuộc lưu lượng.

| Mật độ | Số lượt xe + người / 10 phút | % tiết kiệm |
|---|---|---|
| Thấp | | |
| Cao | | |

**Kết quả chạy qua đêm (cách đo khác, 2026-11-22 01:40–11:19).** Chế độ tự động, ép đêm, nền 20%. Lần B là điện
năng đo thật (`today` của 4 bóng, log mỗi 2 s); lần A **không chạy thật** mà tính từ P100 đo ở 4.1
(259,4 mW × số giờ). Cách này hợp lệ vì ở 100% công suất mỗi bóng gần như không đổi (cuối log, 4 bóng 100%:
63,6 / 64,6 / 66,7 / 65,0 mW, khớp 4.1).

| Bóng | `today` lúc 01:40 (mWh) | `today` lúc 11:19 (mWh) | E lần B (mWh) |
|---|---|---|---|
| 1 | 33,5 | 165,6 | 132,1 |
| 2 | 42,1 | 207,5 | 165,4 |
| 3 | 50,0 | 198,3 | 148,3 |
| 4 | 48,3 | 183,4 | 135,1 |
| **Tổng** | 173,9 | 754,8 | **580,9** |

Chia theo đoạn:

| Đoạn | Thời gian (h) | Tình huống | E lần B (mWh) | P trung bình (mW) | E lần A = 259,4 mW × t (mWh) | **% tiết kiệm** |
|---|---|---|---|---|---|---|
| 01:40–02:00 | 0,33 | đang thử xe/người, có xe dừng ở zone 4 | 42,8 | 129,4 | 85,9 | **50,1%** |
| 02:00–11:00 | 9,00 | đường vắng (5 lần đèn bật thoáng qua, xem 6.2) | 494,7 | 55,0 | 2334,6 | **78,8%** |
| 11:00–11:19 | 0,32 | đặt lại xe/người trên đường | 42,4 | 134,0 | 82,2 | **48,4%** |
| **Cả phiên** | **9,65** | | **579,9** | **60,1** | **2503,5** | **76,8%** |

- Kiểm tra chéo: trung bình cộng công suất 4 bóng từ mọi dòng `Lamp` = **60,2 mW**, khớp 60,1 mW tính từ `today`.
- Mỗi giờ lúc vắng tốn 54,7–55,1 mWh (rất đều), khớp công suất nền 55,6 mW ở 4.1.
- Lúc vắng đạt 78,8%, gần đúng mức tối đa lý thuyết 78,6% (chênh nhỏ do sai số đo ở dòng thấp).
- Khi có xe/người liên tục, tiết kiệm còn ≈ 48–50% vì các đèn quanh vật sáng 100%.
- **Giới hạn:** đây không phải A/B cùng kịch bản như cách đo ở trên; lưu lượng ban đêm gần như bằng 0 nên
  76,8% là con số của đường **rất vắng**. Vẫn chưa tính điện của ESP32.

Ghi chú: con số này chỉ tính 4 đèn, **không tính điện của ESP32** (camera + AI, khoảng vài trăm mA ở 5 V).
Ghi rõ điều này trong báo cáo.

---

## 5. Cảnh báo và xử lý lỗi


### 5.1 Khi cảm biến, camera, WiFi hoặc đèn lỗi

| Lỗi tạo ra | Hành vi mong đợi | Hành vi thực tế | Web có báo? | Đúng? |
|---|---|---|---|---|
| Rút BH1750 | Ngày/đêm theo giờ SNTP, camera vẫn bật đèn theo zone; web báo "Không đọc được cảm biến ánh sáng BH1750 (mất kết nối)" | 2026-11-22 ban ngày, rút BH1750: board chuyển sang ngày/đêm theo giờ, mục Điều khiển trên web hiện **"Không đọc được cảm biến ánh sáng BH1750 (mất kết nối): ngày/đêm lấy theo giờ. Camera vẫn bật đèn theo vùng có người/xe."** (dashboard bản mới, commit `7ebccf2`) | ✅ | ✅ |
| Che BH1750 ban ngày > 1 phút | Đèn **không bật**; sau 1 phút cảnh báo `bh1750`; web báo "BH1750 báo giá trị bất thường (tối giữa ban ngày…)" | Không cố ý che: phòng tối lúc sáng sớm (lux 1,7). 06:01 board in `BH1750 dark ... for 2 min by day`, gửi `alert (bh1750)`, các dòng sau ghi `not trusted, day/night from the clock` (log 2026-11-22, firmware cũ 2 phút) | 13:02 web báo 1 thông báo chung cho BH1750 (firmware cũ) → đã tách thành 2 thông báo, chưa thử lại | ✅ phía board |
| Rút camera | Chế độ theo giờ: 18–06h sáng 100%, 06–18h tắt; web báo "Camera hoặc model nhận diện lỗi" (ban ngày trong ≤ 5 phút, ban đêm ≈ 3 s) | Lần 1 (firmware cũ, ban ngày): web không báo gì — cần 5 lần kiểm tra × 5 phút ≈ 25 phút mới báo, và lỗi BH1750 đang có che mất lỗi camera → đã sửa. **Lần 2** (2026-11-22, bật "Ép chế độ đêm"): rút camera → mục Điều khiển trên web hiện **"Camera hoặc model nhận diện lỗi: đèn chạy theo giờ"** | ✅ (lần 2) | ✅ (ban đêm); ban ngày chưa thử lại |
| Mất WiFi | Đèn vẫn tự chạy, web báo board offline | Tắt hotspot ≈ 30 s (12:04): đèn và luật vẫn chạy, chu kỳ điều khiển vẫn ≈ 55 ms, board tự nối lại WiFi + MQTT. Lần thử 13:07: web hiện **"Board ESP32 offline"** ở đầu trang và **"Đang chờ dữ liệu từ board…"** trên khung đường phố | ✅ "Board ESP32 offline" | ✅ |
| Rút dây 1 bóng đèn | Sau ~60 s báo "Hở mạch" đúng bóng đó, cắm lại ~10 s thì hết báo; các đèn khác chạy bình thường (thời gian đo ở 5.2) | 2026-11-22: rút dây 1 bóng → sau **60 s** web báo lỗi bóng đó; cắm lại → **10 s** sau hết báo, đèn hiện bình thường | ✅ | ✅ |

### 5.2 Rút bóng đèn (phát hiện đèn hỏng)

Cách đo: đèn đang sáng, rút dây 1 bóng, bấm giờ đến lúc dashboard báo "Hở mạch"; cắm lại, bấm giờ đến
lúc hết báo.

Board (`lamp_fault.c`) chỉ báo khi lỗi kéo dài **60 s** và gỡ sau **10 s** bình thường, nên mong đợi
rút → báo khoảng 60–65 s (cộng chu kỳ gửi `state` 5 s), cắm lại → hết báo khoảng 10–15 s.

| Lần | Rút dây → báo (s) | Cắm lại → hết báo (s) |
|---|---|---|
| 1 (2026-11-22) | **60** | **10** |
| 2 | | |

**Nhận xét:** đúng thời gian thiết kế (lỗi phải kéo dài 60 s mới báo, bình thường 10 s mới gỡ). Báo chậm 60 s là cố ý:
trước đây dashboard từng báo "Đèn 3 hở mạch" nhầm chỉ sau 2 gói tin, nên board chờ đủ 1 phút để chắc là hỏng thật,
không phải nhiễu đo hay đèn đang tăng/giảm sáng. Gỡ báo nhanh hơn (10 s) để người quản lý thấy ngay là đã sửa xong.

---

## 6. Hệ thống và mạng


### 6.1 Bộ nhớ

| Mục | Đã dùng | Tổng | % |
|---|---|---|---|
| Flash (app) | 1,22 MB (0x1382B0 B) | 3 MB (partition) | 40,6% |
| RAM nội (heap), lúc cao nhất | 237 KB (còn trống thấp nhất 53 KB) | 290 KB | **81,7%** |
| RAM nội (heap), lúc chạy bình thường | 218 KB (còn trống 72 KB) | 290 KB | 75,2% |
| PSRAM, lúc cao nhất | 1081 KB (còn trống thấp nhất 7111 KB) | 8192 KB | 13,2% |
| **RAM nội, lúc cao nhất qua 9,7 giờ** (log 2026-11-22 01:40–11:19) | 248 KB (còn trống thấp nhất **38 KB**) | 286 KB | **86,7%** |
| RAM nội, lúc chạy bình thường (cùng log) | 221 KB (còn trống 65 KB) | 286 KB | 77,3% |
| PSRAM, lúc cao nhất (cùng log) | 1082 KB (còn trống thấp nhất 7110 KB) | 8192 KB | 13,2% |

Firmware mới (bản có lọc log theo phím) báo tổng heap nội 286 KB thay vì 290 KB. Qua 9,7 giờ, `min` giảm dần
50 → 38 KB trong khoảng 3 giờ đầu rồi đứng yên, còn `free` luôn 62–65 KB: đây là **đỉnh tạm thời** cao hơn
(nhiều khả năng lúc TLS gửi ảnh cảnh báo cùng lúc với gói khác), không phải rò bộ nhớ. 38 KB vẫn chạy được nhưng
vượt mục tiêu 80% rõ hơn → nên làm `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`.

Nguồn: log ngày 2026-11-22, board đã chạy 8 phút, có WiFi + MQTT/TLS + AI:

```
I (483195) node: health: up 482 s, last reset: power-on | internal RAM free 72 KB (min 53 KB) of 290 KB | PSRAM free 7129 KB (min 7111 KB) of 8192 KB
```

**Nhận xét:** RAM nội lúc cao nhất dùng 81,7%, **vượt nhẹ mục tiêu 80%**; lúc bình thường 75%. Lúc cao
nhất thường rơi vào khi bắt tay TLS với broker MQTT (mbedTLS cần vài chục KB RAM nội). Còn 53 KB trống nên
board vẫn chạy ổn. Muốn có thêm chỗ: cho mbedTLS dùng PSRAM (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`).
PSRAM còn rất nhiều (chỉ dùng 13%): frame camera và tensor arena của FOMO nằm ở đây.

Cách lấy:

- **Flash:** dòng cuối của `idf.py build`: `streetlight-node.bin binary size 0x1382b0 bytes. Smallest app
  partition is 0x300000 bytes. 0x1c7d50 bytes (59%) free.` → đã dùng 100% − 59% ≈ 41%.
- **RAM:** log `health` in mỗi phút (số bên dưới chỉ là ví dụ):
  ```
  I (...) node: health: up 600 s, last reset: power-on | internal RAM free 190 KB (min 185 KB) of 400 KB | PSRAM free 7300 KB (min 7290 KB) of 8192 KB
  ```
  % RAM đã dùng = (tổng − **min**) / tổng. Dùng `min` (lúc còn ít nhất từ khi khởi động), không dùng `free`.
- Chi tiết từng vùng (.text/.data/.bss) nếu cần: `idf.py size` hoặc `idf.py size-components`.

### 6.2 Chạy lâu

Chạy liên tục **9,7 giờ** (để qua đêm), lưu log ra file:

```
idf.py -p COMx monitor | tee soak.log        (Git Bash)
```

hoặc dùng PuTTY/Tera Term ghi log ra file. Sau đó tìm các dòng `health:`:

- `up … s` tăng liên tục, **không bị về 0** → không có lần khởi động lại nào. Nếu có thì `last reset:` cho
  biết lý do (`PANIC`, `task watchdog`, `BROWNOUT` = nguồn yếu…).
- `internal RAM free` / `PSRAM free` **đi ngang** sau vài phút đầu → không rò bộ nhớ. Nếu giảm đều theo
  thời gian thì có rò.

Kết quả 2026-11-22 01:40–11:19 (9,7 giờ), log `log.streetlight-node.20261122014008.txt`, chế độ tự động, ép đêm,
nền 20%, WiFi nhà, broker EMQX Cloud. Số lấy từ dòng `health:` gần mốc nhất; trong ngoặc là `min` (còn trống thấp
nhất từ lúc khởi động).

| Thời điểm | Heap còn trống (KB) | PSRAM còn trống (KB) | Số lần reset |
|---|---|---|---|
| Bắt đầu (01:41, up 182 s) | 65 (min 50) | 7127 (min 7110) | 0 |
| Sau 3 giờ (04:41, up 10 992 s) | 65 (min 38) | 7127 (min 7110) | 0 |
| Sau 6 giờ (07:41, up 21 802 s) | 65 (min 38) | 7127 (min 7110) | 0 |
| Sau 8 giờ (09:41, up 29 009 s) | 65 (min 38) | 7127 (min 7110) | 0 |

- **0 lần khởi động lại**: 578 dòng `health:` liên tiếp, `up` tăng đều từ 182 → 34 834 s, `last reset: power-on`
  ở tất cả. Không có `PANIC`, `task watchdog`, `BROWNOUT`.
- **Không rò bộ nhớ**: heap còn trống 65 KB ở 572/578 dòng (62–64 KB ở 6 dòng còn lại); PSRAM còn trống 7127 KB
  gần như mọi lúc (thỉnh thoảng 7123–7846 KB khi frame buffer được trả).
- **Mạng**: không có dòng `MQTT disconnected` nào; mọi dòng trạng thái đều `WiFi up | MQTT connected`. Board gửi
  `state + energy` đều đặn, 10 lần `energy/history` (mỗi giờ 1 lần).
- **AI**: 3,8 fps ở 95% số dòng `vision` (3,6–3,9 fps), `road found` suốt phiên.
- **Camera**: 189 dòng `cam_hal: FB-OVF` / `FB-SIZE ... != 76800` rải rác cả đêm = mất khung lẻ (DMA tràn hoặc
  khung thiếu dữ liệu). Driver tự bỏ khung đó; fps không bị ảnh hưởng. Đáng xem lại nếu số này tăng.
- **Đèn bật thoáng qua lúc đường vắng** (02:00–11:00, giả sử không có ai đi qua): 5 lần, mỗi lần 1–2 vật, đèn
  lên 100% khoảng 2 s rồi giảm dần — 02:40 (đèn 1,2), 05:40 và 05:49 (đèn 1,2), 06:11 (đèn 3,4), 10:40 (đèn 1,2,3).
  Các dòng `vision` (in mỗi 2 s) không bắt được vật nào trong cả 9 giờ này → vật chỉ xuất hiện ở 1–2 khung lẻ,
  đúng kiểu **nhận nhầm** (FP). Tần suất ≈ 0,55 lần/giờ, tốn rất ít điện (mỗi giờ vẫn ≈ 55 mWh, xem 4.3).
  Hướng sửa: chỉ bật khi vật xuất hiện ≥ 2 khung liên tiếp.
- **Không báo nhầm đèn hỏng** lần nào (không có dòng `lamp k: fault`), dù đèn đổi 20% ↔ 100% nhiều lần.
- **Chưa đủ 12 giờ** theo mục tiêu; nên chạy thêm 1 lần dài hơn.

### 6.3 Độ trễ từ board lên dashboard

Đo tự động (log tạm, `APP_EVAL_LOGS`): mỗi gói `streetlight/state` (5 s một lần) có thêm `"t"` = giờ board
gửi. Dashboard nhận được thì gửi lại `{"t":…, "rx":<giờ web nhận>}` lên `streetlight/cmd/latency`, board in:

```
I (...) node: web latency: board->web 180 ms, web->board 150 ms, round trip 330 ms (exact)
```

- **board->web:** board gửi → dashboard nhận (đây là số của 6.3).
- **web->board:** dashboard gửi → board nhận (cùng đường với lệnh của 6.4).
- **round trip:** đi + về, chỉ dùng đồng hồ của board nên **chính xác**, không bị lệch giờ.
- 2 số một chiều phụ thuộc giờ máy tính khớp giờ board (cả hai lấy từ NTP). Trước khi đo bấm *Sync now*
  trong *Settings → Time & language → Date & time*. Nếu một số âm hoặc chênh nhau nhiều → giờ máy lệch;
  khi đó dùng round trip / 2 làm độ trễ một chiều.

Cách làm: mở dashboard, để chạy vài phút, chép 5 dòng `web latency` liên tiếp.

Kết quả 2026-11-22, 01:07–01:08, WiFi nhà, broker EMQX Cloud (asia-southeast1):

| Lần | board→web (ms) | web→board (ms) | Round trip (ms) |
|---|---|---|---|
| 1 | −253 | 361 | 108 |
| 2 | −267 | 375 | 108 |
| 3 | −265 | 372 | 107 |
| 4 | −265 | 374 | 109 |
| 5 | −255 | 363 | 108 |
| 6 | −263 | 371 | 108 |
| 7 | −268 | 376 | 108 |
| **TB** | −262 | 370 | **108** |

**Đọc kết quả:**

- Round trip rất ổn định: **107–109 ms**. Đây là số chính xác (chỉ dùng đồng hồ board).
- board→web bị **âm** → đồng hồ máy tính đang **chậm hơn board khoảng 316 ms**
  ((370 − (−262)) / 2 ≈ 316). Hai số một chiều vì vậy không dùng trực tiếp được.
- Độ trễ một chiều ≈ round trip / 2 ≈ **54 ms** (coi đường đi và về như nhau). Đạt mục tiêu < 2 s.
- Lưu ý: dashboard chỉ nhận trạng thái mới mỗi **5 s** (chu kỳ gửi của board), nên người xem thấy thay
  đổi chậm nhất khoảng 5 s + 54 ms. Muốn nhanh hơn thì giảm `APP_TELEMETRY_PERIOD_MS` (đổi lại tốn mạng hơn).

**Lần đo 2 — firmware đã sửa (ghi giờ ngay lúc gói tin tới), 2026-11-22 01:29. DÙNG SỐ NÀY.**
Bảng lần 1 ở trên bị làm tròn lên theo chu kỳ 100 ms của task mạng, chỉ để tham khảo.

| | Round trip (ms) | Một chiều ≈ RT/2 (ms) |
|---|---|---|
| 18 lần | 68, 152, 68, 98, 74, 81, 86, 152, 78, 105, 72, 71, 67, 76, 77, 80, 77, 64 | |
| **Trung bình** | **86** | **43** |
| Trung vị | 77 | 38 |
| Nhỏ nhất / lớn nhất | 64 / 152 | 32 / 76 |

- Phần lớn các lần 64–86 ms; 2 lần 152 ms rơi đúng lúc vừa có lệnh (mạng bận hơn).
- Đồng hồ máy tính chậm hơn board khoảng **−312 ms** (dao động −267 … −319 ms).

**Lần đo 3 — log qua đêm 2026-11-22 01:40–11:19, 485 lần** (dashboard chỉ mở lúc 01:40–02:10 và 11:00–11:19
nên chỉ có số ở 2 khoảng này). DÙNG SỐ NÀY (nhiều mẫu nhất).

| | Round trip (ms) | Một chiều ≈ RT/2 (ms) |
|---|---|---|
| **Trung bình** | **76** | **38** |
| Trung vị | 71 | 36 |
| 95% số lần dưới | 85 | 43 |
| Nhỏ nhất / lớn nhất | 58 / 1222 | 29 / 611 |

- Chỉ 6/485 lần trên 150 ms (163–366 ms lúc 01:46–01:51, đang có cảnh báo gửi ảnh; 1 lần 1222 ms lúc 11:02). Lần
  lớn nhất vẫn dưới mục tiêu 2 s.
- Lệch đồng hồ máy tính so với board dao động −288 … −731 ms (đồng hồ máy tính trôi so với board qua đêm), không
  ảnh hưởng round trip.

**Cách tính (ghi chú để đọc lại):**

```
Đồng hồ BOARD                                 Đồng hồ MÁY TÍNH (chậm hơn board ~316 ms)
① Board gửi gói state, kèm t        ──────►
                                               ② Web nhận, ghi rx, gửi lại {t, rx} ngay
③ Board nhận lại, đọc giờ           ◄──────
```

- board→web = ② − ① và web→board = ③ − ② đều lấy **giờ máy tính trừ giờ board** nên bị lệch theo
  độ lệch 2 đồng hồ (một số bị âm). Cộng 2 số thì độ lệch tự triệt tiêu: (② − ①) + (③ − ②) = ③ − ①.
- **round trip = ③ − ①**: cả 2 mốc cùng đồng hồ board → chính xác. Là thời gian gửi đi rồi nhận lại.
- Độ trễ một chiều ≈ round trip / 2, **giả sử** chiều đi và chiều về mất như nhau (cùng đường WiFi →
  Internet → broker).
- Độ lệch đồng hồ ≈ (board→web − web→board) / 2. Firmware dùng số này để sửa `cmd latency` ở 6.4.
- Ví dụ minh hoạ với số thật: đặt ① = 1000 → ② = 1000 + (−262) = 738 (giờ máy tính), ③ = 1000 + 108 = 1108.

**Giới hạn của phép đo:**

- Round trip gồm cả thời gian web giữ gói tin trước khi gửi lại. Trong code ([station.ts](../dashboard/src/lib/station.ts))
  web gửi lại **ngay sau khi parse JSON**, trước khi vẽ lại màn hình, nên phần này < 1 ms (ước lượng, chưa
  đo). Muốn loại hẳn: web gửi thêm `tx` (lúc gửi lại), board lấy round trip − (tx − rx) — **chưa làm**.
- Số đo là đến lúc **web nhận gói tin**, chưa tính lúc **màn hình hiện số mới** (React vẽ lại thêm vài ms
  đến vài chục ms).
- Nếu chiều đi và về không đều (ví dụ upload chậm hơn download), round trip / 2 chỉ là trung bình.

### 6.4 Độ trễ từ lúc chỉnh trên web đến lúc đèn đáp ứng

Chiều ngược lại với 6.3: người quản lý ra lệnh trên dashboard → broker MQTT → board → đèn đổi độ sáng.

Đường đi của lệnh:

```
Dashboard (trình duyệt) → broker EMQX Cloud (TLS) → WiFi → board (task mạng) → hàng đợi lệnh
→ vòng điều khiển 200 ms → PWM → đèn tăng/giảm sáng (fade-up khoảng 200 ms)
```

**Cách đo tự động (log tạm, `APP_EVAL_LOGS`):** dashboard gửi kèm giờ bấm `"t"` trong lệnh đèn, board ghi
giờ nhận rồi in sau khi đặt PWM:

```
I (...) node: cmd latency: lamp 3 -> 100% | web->board 180 ms + board->PWM 150 ms = 330 ms (+ fade 200 ms; ...)
```

Bật chỉnh tay, kéo 1 đèn 5 lần, chép 5 dòng `cmd latency`. Cũng cần giờ máy tính khớp (xem 6.3).

**Kết quả 2026-11-22** (đồng hồ máy tính chậm hơn board ~316–323 ms, lấy từ dòng `web latency` gần nhất):

```
I (...)    node: cmd latency: lamp 3 -> 85% | web->board 495 ms + board->PWM 283 ms = 778 ms
I (680609) node: cmd latency: lamp 1 -> 35% | web->board 444 ms + board->PWM 281 ms = 725 ms
```

| Lần | web→board (log) | Lệch đồng hồ | web→board (đã sửa) | board→PWM | **Tổng** |
|---|---|---|---|---|---|
| 1 | 495 | −316 | 179 | 283 | **462 ms** |
| 2 | 444 | −323 | 121 | 281 | **402 ms** |

- web→board (đã sửa) = số trong log + độ lệch đồng hồ (máy tính chậm làm số trong log dài ra).
- Firmware mới tự sửa và in `PC clock corrected`; phải có ít nhất 1 dòng `web latency` sau khi khởi động.
- **board→PWM (~280 ms) là phần lớn nhất**: lệnh chờ tới vòng điều khiển (0–200 ms), rồi vì board đặt độ
  sáng **trước** khi đọc lệnh nên chờ thêm 1 vòng (200 ms). Đổi thứ tự trong vòng điều khiển sẽ bớt ~200 ms.
- Trước đó, 11 lệnh với dashboard cũ (không có giờ gửi) cho board→PWM: 201–394 ms, TB **296 ms**.

**Lần đo 2 (10 lệnh, 01:25, firmware cũ — chỉ tham khảo)** — độ lệch đồng hồ lấy từ dòng `web latency` ngay sau mỗi lệnh,
lệch = (board→web − web→board) / 2:

| Lần | Lệnh | web→board (log) | Lệch | web→board (đã sửa) | board→PWM | **Tổng** |
|---|---|---|---|---|---|---|
| 1 | đèn 4 → 10% | 402 | −240 | 162 | 245 | **407** |
| 2 | đèn 3 → 20% | 373 | −203 | 170 | 237 | **407** |
| 3 | đèn 1 → 70% | 399 | −239 | 160 | 331 | **491** |
| 4 | đèn 2 → 15% | 330 | −242 | 88 | 323 | **411** |
| 5 | đèn 4 → 80% | 484 | −241 | 243 | 214 | **457** |
| 6 | đèn 3 → 55% | 484 | −237 | 248 | 206 | **454** |
| 7 | đèn 3 → 95% | 351 | −242 | 110 | 298 | **408** |
| 8 | đèn 1 → 90% | 329 | −242 | 88 | 389 | **477** |
| 9 | đèn 2 → 95% | 408 | −244 | 165 | 382 | **547** |
| 10 | đèn 4 → 25% | 430 | −236 | 194 | 275 | **469** |
| **TB** | | | | **163** | **290** | **453 ms** |
| Nhỏ nhất / lớn nhất | | | | 88 / 248 | 206 / 389 | 407 / 547 |

**Kết luận:** web → đèn đổi độ sáng trung bình **453 ms**, lớn nhất 547 ms (+ 200 ms đèn tăng/giảm dần)
→ **đạt < 1 s**.

**Sai số đã biết (ghi chú):** ở bản firmware dùng cho lần đo này, board ghi giờ nhận lệnh trong task mạng,
mà task này chỉ kiểm tra hàng đợi **mỗi 100 ms**. Hậu quả:

- Giờ nhận bị trễ 0–100 ms → chia giữa "web→board" và "board→PWM" chưa chính xác (một phần thời gian chờ
  của board bị tính sang web→board).
- Round trip ở 6.3 cũng bị làm tròn lên khoảng 100 ms (luôn ≈ 108 ms) → độ trễ thật có thể **nhỏ hơn**
  54 ms; độ lệch đồng hồ ước lượng sai khoảng vài chục ms → tổng có sai số khoảng **±50 ms**.
- Kết luận "< 1 s" không đổi vì tổng lớn nhất 547 ms còn cách xa 1 s.
- Đã sửa trong firmware (ghi giờ ngay trong callback của esp-mqtt, lúc gói tin vừa tới). Muốn có số chính
  xác từng chặng thì nạp bản mới và đo lại cả 6.3 và 6.4.

**Lần đo 3 — firmware đã sửa, 2026-11-22 01:29, 15 lệnh. DÙNG SỐ NÀY.** Board tự trừ lệch đồng hồ
(`PC clock corrected`).

| Lần | Lệnh | web→board (đã sửa) | board→PWM | **Tổng** |
|---|---|---|---|---|
| 1 | đèn 2 → 55% | 91 | 288 | **379** |
| 2 | đèn 1 → 65% | 157 | 266 | **423** |
| 3 | đèn 4 → 60% | 136 | 329 | **465** |
| 4 | đèn 3 → 65% | 102 | 305 | **407** |
| 5 | đèn 1 → 15% | 110 | 378 | **488** |
| 6 | đèn 2 → 20% | 127 | 343 | **470** |
| 7 | đèn 2 → 90% | 111 | 433 | **544** |
| 8 | đèn 4 → 90% | 116 | 403 | **519** |
| 9 | đèn 3 → 30% | 117 | 281 | **398** |
| 10 | đèn 1 → 70% | 141 | 345 | **486** |
| 11 | đèn 2 → 35% | 184 | 323 | **507** |
| 12 | đèn 2 → 5% | 164 | 402 | **566** |
| 13 | đèn 4 → 30% | 205 | 370 | **575** |
| 14 | đèn 3 → 20% | 77 | 350 | **427** |
| 15 | đèn 3 → 85% | 197 | 324 | **521** |
| **TB** | | **136** | **343** | **478 ms** |
| Nhỏ nhất / lớn nhất | | 77 / 205 | 266 / 433 | 379 / 575 |

**Kết luận:** từ lúc chỉnh trên web đến lúc đèn bắt đầu đổi độ sáng trung bình **478 ms**, lâu nhất 575 ms
(+ 200 ms đèn tăng/giảm dần) → **đạt < 1 s**.

**board→PWM (TB 343 ms) chiếm ~70%** và khớp đúng với cấu trúc firmware:

| Phần chờ trong board | Khoảng | TB |
|---|---|---|
| Task mạng lấy lệnh khỏi hàng đợi (kiểm tra mỗi 100 ms) | 0–100 ms | ~50 |
| Chờ tới vòng điều khiển kế tiếp (mỗi 200 ms) | 0–200 ms | ~100 |
| Đặt độ sáng **trước** khi đọc lệnh → chờ thêm 1 vòng | 200 ms | 200 |
| **Cộng** | 200–500 ms | **~350** |

Giảm được: đổi thứ tự trong vòng điều khiển (−200 ms), và cho lệnh vào thẳng hàng đợi của vòng điều khiển
thay vì qua task mạng (−~50 ms) → còn khoảng 100 ms trong board, tổng khoảng 230 ms.

**Chưa giải thích được:** web→board của lệnh (TB 136 ms) lớn hơn một chiều đo bằng round trip ở 6.3
(TB 43 ms). Khả năng: (1) chiều đi và về không đều nhau nên độ lệch đồng hồ ước lượng bị sai (công thức giả
sử đều); (2) lệnh gửi QoS 1, còn gói phản hồi của 6.3 gửi QoS 0; (3) thời gian trình duyệt xử lý trước khi gửi
lệnh. Tổng 478 ms không bị ảnh hưởng nhiều vì chỉ phụ thuộc độ lệch đồng hồ (sai số khoảng vài chục ms).

Lưu ý: độ trễ phụ thuộc mạng (WiFi, khoảng cách tới broker cloud ở Singapore). Ghi lại đang dùng mạng nào
(WiFi nhà, hotspot điện thoại…) ở bảng Điều kiện thử nghiệm.

---

## Điều kiện thử nghiệm

Ghi một lần, áp dụng cho mọi bảng ở trên.

| Mục | Giá trị |
|---|---|
| Ngày đo | 2026-11-21 → 2026-11-22 (chạy qua đêm 22/11 01:40–11:19) |
| Phiên bản firmware (commit) | `dd79205` + thay đổi chưa commit (có `APP_EVAL_LOGS`, lọc log theo phím) |
| Phiên bản model Edge Impulse | v2, input 128×128, int8 (theo nhật ký 20/11) |
| Ánh sáng phòng | Ban đêm tối (BH1750 0 lux), từ 06:00 đến 11:19 khoảng 1–43 lux; ép chế độ đêm suốt phiên |
| Nguồn đèn (adapter, V) | 5 V riêng; INA219 đo 5,12–5,15 V |
| Nguồn ESP32 | |
| `base_pct` / `stop_time_s` | 20% / 10 s |
