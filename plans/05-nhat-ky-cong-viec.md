# 05 — Nhật ký công việc

Ghi lại việc đã làm, kết quả đo được và việc còn dở. Mỗi mục có ngày; thêm mục mới ở đầu file.

---

## 20/11 → rạng sáng 22/11/2026

**Mục tiêu của đợt này:** hoàn thiện các chức năng còn thiếu (lỗi đèn, cảm biến, cảnh báo xe dừng có ảnh),
rồi bắt đầu **đánh giá hệ thống** theo [eval.md](eval.md).

### Tóm tắt

| Việc | Kết quả |
|---|---|
| Board tự xét đèn hỏng (`lamp_fault`) | Xong, chỉ báo khi lỗi kéo dài **1 phút**, gỡ sau 10 s bình thường |
| BH1750: ban ngày mà tối bất thường (`lux_check`) | Xong, gửi cảnh báo rồi chạy theo giờ |
| INA219 đọc dòng âm khi đèn tắt (`ina_zero`) | Xong, tự hiệu chỉnh offset |
| Thanh chỉnh độ sáng nền 10–100 % | Xong (firmware + dashboard) |
| Cảnh báo xe dừng lâu kèm ảnh, bật/tắt, chỉnh thời gian | Xong, chỉ xe (bỏ người), chấm + số từng xe, thời gian dừng từng xe |
| Dashboard báo lỗi BH1750/camera cả khi board vẫn chạy tự động | Xong, đã push |
| Bảng đánh giá `plans/eval.md` | Đã tạo; đã có số cho mục 1.1, 1.3, 1.4, 3.1, 3.2, 4.0, 6.1, 6.4, 6.5 |
| Log đo đánh giá (tạm) + chọn log theo phím 0–6 | Xong, build sạch; **phải gỡ khi đánh giá xong** |
| Firmware | Build sạch, **chưa commit** |

---

### 1. Chức năng mới / sửa trong firmware `streetlight-node`

- **`lamp_fault.c`** — board tự xét lỗi từng đèn (hở mạch, chập, kẹt sáng, mất nguồn, INA219 mất), so với P100 đo thật
  của từng bóng thay cho mức "20 mA" đoán chung. Chỉ xét khi đèn đã ổn định 600 ms (không xét lúc đang tăng/giảm dần).
  Lỗi phải kéo dài **60 s** mới báo, bình thường **10 s** thì gỡ. Lý do: dashboard từng báo "Đèn 3 hở mạch" nhầm sau 2 gói tin.
- **`lux_check.c`** — theo giờ SNTP là ban ngày mà BH1750 đọc tối liên tục 2 phút → không tin BH1750, gửi cảnh báo
  `bh1750`, ngày/đêm theo giờ.
- **`ina_zero.c`** — dòng đo khi đèn tắt bị âm do offset của INA219 → tự hiệu chỉnh offset lúc đèn tắt.
- **`base_pct`** — độ sáng nền ban đêm chỉnh được 10–100 % từ dashboard, lưu NVS (`CONFIG_VERSION 3`).
- **`stop_detect.c`** — theo dõi xe đứng yên (8 track, khớp trong 45 px, mất dấu 3 s), cảnh báo `vat_dung` kèm ảnh JPEG.
  Chỉ xe trên đường/vỉa hè (bỏ người, bỏ xe ngoài zone). Gửi danh sách mọi xe trong ảnh, đánh số trái → phải,
  kèm `still_s` (giây đã dừng) nên vẫn chạy khi không có SNTP. Xe đã "Đã xử lý" không báo lại trừ khi nó đi rồi dừng lại.
- Ảnh cảnh báo **chỉ chấm xe trong zone** (đã thử bỏ rồi bật lại theo yêu cầu).
- Thêm vào `.gitignore`: `node_modules/`, `nvs.bin`, `*.exe`.

### 2. Dashboard (đã push lên GitHub Pages)

- Trang ai-stream: chỉ chấm tâm vật + nhãn (lớp · zone · %), không vẽ khung.
- Cảnh báo xe dừng: chấm có số trên ảnh, vòng đỏ cho xe đang bị báo, danh sách "Xe N · Vùng k · đã dừng …".
- Cài đặt: thanh độ sáng nền, công tắc + thời gian cảnh báo xe dừng.
- Lỗi BH1750/camera hiện ở bảng điều khiển và Lịch sử kể cả khi board ở chế độ tự động.
- Tự đoán lỗi đèn (cho firmware cũ) cũng đổi sang 60 s / 10 s.

### 3. Đánh giá (chi tiết trong [eval.md](eval.md))

| Mục | Kết quả |
|---|---|
| 1.1 F1 FOMO | 0,90 (validation, int8); 0,89 (test, float32). Recall người chỉ **0,73** |
| 1.3 int8 so với float32 | Không giảm (90,3 % / 90,1 %) |
| 1.4 Tốc độ | **3,8 fps**; 1 vòng 277 ms, inference 208,6 ms (75 %) |
| 3.1 Vật vào zone → lệnh PWM | TB **528 ms** từ lúc chụp khung (2 lần) |
| 3.2 Chu kỳ điều khiển | 54,7 ms bình thường, **66,0 ms** lúc tải nặng (cảnh báo + ảnh) / 200 ms |
| 4.0 Công suất | 100 %: 4 bóng 259,4 mW; 20 %: 55,6 mW → tiết kiệm tối đa lý thuyết **78,6 %** |
| 6.1 Bộ nhớ | Flash 40,6 %, PSRAM 13,2 %, RAM nội lúc cao nhất **81,7 %** (vượt nhẹ 80 %) |
| 6.4 Board → web | ≈ **43 ms** một chiều (round trip TB 86 ms, 18 lần) |
| 6.5 Web → đèn | TB **478 ms** (379–575 ms, 15 lần) |

### 4. Log đo đánh giá (TẠM — gỡ khi xong, `APP_EVAL_LOGS` trong `app_config.h`)

- `latency:` — vật mới làm đèn bật: FOMO + chờ vòng điều khiển + ra lệnh PWM.
- `cmd latency:` / `web latency:` — dashboard gửi kèm giờ bấm (`t`), board gửi giờ trong `state`, dashboard gửi lại
  `cmd/latency`; board tự trừ độ lệch đồng hồ máy tính (máy tính đang chậm hơn board ~312 ms).
- `health:` mỗi phút (uptime, lý do reset, RAM/PSRAM), `control cycle max … (since boot …)`, `MQTT back after …`.
- **Chọn log theo phím** (`log_view.c`): gõ 0–6 trong `idf.py monitor`; 0 = tất cả, 1–6 = mục tương ứng của eval.md.
- Giờ nhận lệnh được ghi ngay trong callback esp-mqtt (trước đó ghi ở task mạng, bị trễ 0–100 ms làm sai số đo).

### 5. Phát hiện khi đo

- **Đèn 3, 4 tự nháy:** AI nhận nhầm thoáng qua ở zone 4 (cuối đường, nền rối) → đèn lên 100 % ~2 s rồi tắt dần.
- **Thứ tự code trong vòng điều khiển:** đặt độ sáng **trước** rồi mới đọc kết quả AI và lệnh → mọi thay đổi chờ thêm
  1 vòng (200 ms). Chiếm phần lớn độ trễ ở 3.1 và 6.5. **Chưa sửa** (để có số "trước/sau").
- Có lúc dòng đo đèn 1 và đèn 4 bị đảo (đèn 1 ghi 20 % mà đo dòng mức 100 %); lần đo sau thì khớp. Chưa rõ nguyên nhân.
- Board không có giờ SNTP ở một mạng WiFi (có thể chặn UDP 123); mạng khác thì có.
- Con số "Tiết kiệm" trên dashboard cộng dồn từ lần nạp đầu (gồm cả lúc thử 100 %) → chưa dùng cho báo cáo.
- NVS bị mất secrets → nạp lại bằng `nvs_partition_gen.py` + `esptool write_flash 0x9000` (CMD dùng `%IDF_PATH%`).

### Việc còn dở / bước tiếp theo

1. **Nạp firmware mới nhất** (có lọc log theo phím) và **chạy qua đêm** cho 6.2 + 5.4 (ép đêm, chế độ tự động, ghi log
   Ctrl+T Ctrl+L, sáng lọc `health:` / `starting` / `fault`).
2. Đo các mục còn thiếu của eval.md: 1.2, 2.1–2.3, 3.3, 4.1 (đồng hồ vạn năng), **4.3 % tiết kiệm (A/B 10 phút)**, 5.1–5.3, 5.5, 6.3.
3. Model Testing của Edge Impulse cho bản **int8** (số test hiện có là float32).
4. Quyết định: đổi thứ tự vòng điều khiển (−200 ms), cho mbedTLS dùng PSRAM (RAM nội về dưới 80 %),
   lọc nhận nhầm (cần 2–3 khung liên tiếp), nút "Bắt đầu đo" cho 4.3.
5. **Commit firmware** (lọc `nvs.bin`, `secrets.csv`, file build).
6. Đánh giá xong: đặt `APP_EVAL_LOGS 0` / gỡ `log_view.c`, trường `t` trong `state` và `cmd/lamp`, topic `cmd/latency`.

---

## 18/11 → rạng sáng 20/11/2026

**Mục tiêu của đợt này:** chia vùng chiếu sáng (zone) theo hình dạng mặt đường thay vì chia 4 phần đều theo chiều ngang ảnh,
rồi đưa camera + AI vào app chính `firmware/streetlight-node`.

### Tóm tắt

| Việc | Kết quả |
|---|---|
| Thuật toán tìm khung đường + chia 4 zone | Xong, thử trên PC với ảnh thật, **chạy được trên board** |
| Bản C của thuật toán (dùng chung) | Xong, khớp bản Python 10/10 ảnh |
| Ghép camera + AI vào app chính | Xong, **đã nạp và chạy trên board** |
| Model FOMO: độ chính xác và thời gian xử lý | Có số đo trên board (mục 7) |
| Dataset: thêm phiên 009, chia train/val/test | Xong |
| Sơ đồ lưu đồ / kiến trúc (23 sơ đồ) | Xong, file HTML |
| Lỗi board (flash 8 MB, I2C, camera) | Đã tìm ra nguyên nhân; BH1750 **chưa xử lý được** |
| Dashboard: thanh thống kê đè lên đèn | Đã sửa trong mã nguồn và build lại; **chưa đẩy lên GitHub Pages** |

---

### 1. Thuật toán tìm đường và chia zone

**Vì sao:** camera nhìn xiên nên đường chạy chéo và có phối cảnh. Chia ảnh thành 4 phần đều theo chiều ngang làm
zone lệch xa vị trí đèn thật, và vật ngoài đường (giàn sắt, người đứng trên lề) cũng làm sáng đèn.

**Cách làm** (`ai_stream/tools/zone_calib.py`, bản thử trên PC):

1. Quét từng cột ảnh, tìm **cạnh** sáng→tối (mép trên của đường) và tối→sáng (mép dưới). Không dùng ngưỡng sáng cố định
   vì mặt đường dưới đèn sáng gần bằng lề.
2. Khớp đường thẳng `y = a·x + b` bằng **RANSAC thử mọi cặp điểm**, chịu được khoảng nửa số cột sai (giàn sắt, hai đầu đường),
   rồi 3 vòng "dự đoán → bắt cạnh gần nhất" để kéo dài tới hai đầu.
3. Tìm **đầu đường bên trái** (xiên do góc camera và méo ống kính) bằng cách quét theo hàng.
4. Tìm **mép ngoài của lề** và dùng **tỷ lệ thật của mô hình** làm phương án phụ: lề không đèn = 6/13, lề có đèn = 5/13 bề rộng đường.
5. **Chia zone cách D:** hai mép đường gặp nhau ở điểm tụ V. Khoảng cách trên ảnh `V − x` tỉ lệ với 1/độ sâu nên 4 đoạn bằng nhau
   ngoài thực tế là 4 bước đều của `1/(V − x)`. Zone gần camera rộng, zone xa hẹp.

**Quyết định đã chốt:**
- 4 đèn đặt **cách đều** nên cách D hợp lệ và dùng zone cố định.
- Thứ tự dự phòng: **A** (bật từng đèn, tìm vùng sáng) → hỏi người dùng chọn trên web → **D**. Hiện mới làm D.
- **Đèn 1 = zone bên trái ảnh** (Z1 = L1 … Z4 = L4).

**Kết quả đo:**

| Chỉ số | Kết quả |
|---|---|
| Tìm được đường trên ảnh đường trống (phiên 008, cấu hình camera hiện tại) | 6/6 |
| Ảnh đường trống ở các phiên cũ 003 / 004 / 005 | 19/19, 8/8, 6/7 |
| Bản C so với bản Python, 10 ảnh thật | **10/10 khớp**, lệch tối đa 0,4 pixel |
| Thời gian chạy bản C trên ESP32-S3 | **430 ms** mỗi lần hiệu chỉnh |

**Hạn chế đã biết:** đầu đường bị méo ống kính (cong) nhưng đang xấp xỉ bằng đường thẳng, sai số vài pixel. Cách chữa tận gốc là
hiệu chỉnh ống kính bằng ảnh bàn cờ (chưa làm). Cách A (bật từng đèn) chưa kiểm chứng: với dữ liệu có sẵn, đèn 3 không thấy vùng sáng.

**Công cụ:** `python ai_stream/tools/zone_calib.py -o <thư mục> image|offline|live|from-dir …` lưu **ảnh từng bước** vào `steps/`.

### 2. Bản C và kiểm tra

- `firmware/components/road_zones/` (**component dùng chung**): `road_zones.c/.h`, C thuần, `float` cho FPU của S3.
- Test trên PC: `ai_stream/test/host/test_road_zones.c` (ảnh tổng hợp, ảnh rỗng, tham số sai).
- So sánh bản C với bản Python: `python ai_stream/tools/check_c_port.py <ảnh…>`.
- `firmware/streetlight-node/main/zone_rule.c` (luật đèn theo zone) có 9 nhóm test: `test/host/test_zone_rule.c`, đều đạt.

### 3. Ghép camera + AI vào app chính `firmware/streetlight-node`

**Kiến trúc: 3 task** (tách để chu kỳ 200 ms luôn đúng, vì một lần suy luận mất cả 200–300 ms):

| Task | Ưu tiên | Làm gì |
|---|---|---|
| `control` | 6 (cao nhất) | Đúng mỗi 200 ms: cảm biến → giờ → hộp thư nhận diện → luật đèn → PWM → điện năng → NVS. Không bao giờ chờ mạng hay camera |
| `vision` | 4 | Camera + FOMO + tìm đường; đặt kết quả vào hộp thư |
| `network` | 3 | WiFi, SNTP, MQTT; nhận lệnh qua hàng đợi, gửi những gì `control` chuẩn bị |

**Luật đèn ban đêm:** nền 20 %; **người** ở zone K → đèn K−1, K, K+1 sáng 100 % và giữ 5 giây; **xe** → cũng K−1..K+1, giữ 2 giây
(chưa có hướng đi); vật ngoài đường bị bỏ qua.

**Xử lý lỗi** (đã sửa lại theo góp ý của bạn):

| Tình huống | Ngày/đêm theo | Đèn ban đêm |
|---|---|---|
| Mọi thứ tốt | BH1750 | nền 20 % + sáng theo zone |
| **Chỉ BH1750 hỏng** | đồng hồ (SNTP; chưa có giờ thì coi là đêm) | **nền 20 % + sáng theo zone** |
| Chỉ camera hỏng | BH1750 | 100 % (không còn AI) |
| Cả hai hỏng | đồng hồ | 100 % |

**Tìm đường mỗi đêm:** thử tối đa 3 lần cách nhau 5 giây; không được thì chia zone theo phần tư ảnh. Khi đang tìm đường, 4 đèn tự sáng 100 %.

**Thay đổi cấu hình/phần cứng đi kèm:**
- Model: **v2 128×128** dùng chung từ `ai_stream/components/edge_impulse` (không nhân đôi 26 MB).
- Phân vùng app **3 MB** (`partitions.csv`), CPU **240 MHz**, camera chạy core 1.
- **Thứ tự chân đèn 47 / 21 / 20 / 19, đèn đảo mức** (giống `ai_stream`, đèn 1 = zone trái ảnh). `tune-by-wifi` dùng chung nên cũng đổi theo.
- `lamp_pwm` của `firmware/` nâng lên bản có tham số đèn đảo mức.

**Lỗi gặp và đã sửa:** `stack overflow in task main` do hạ ngăn xếp `app_main` xuống 4 KB. `detector_init()` chạy lần suy luận khởi động
ngay trong `app_main`, nên cần 8 KB.

### 4. Sơ đồ lưu đồ và kiến trúc

File `plans/04-luu-do-va-so-do.html`: **23 sơ đồ** (Mermaid), tải được SVG/PNG để chèn vào báo cáo, ô màu theo trạng thái
(xanh = đã có code, vàng = có một phần, cam nét đứt = mới là thiết kế). Gồm kiến trúc, phần cứng, 3 task, các luật đèn,
điện năng, MQTT, dashboard, quy trình dữ liệu AI.

Theo góp ý: bỏ phần mã hoá AES-256-GCM (chuyển sang mục **"Hướng phát triển"** cuối trang), thêm cơ chế hiệu chỉnh lại khi không tìm được đường,
thêm phương án dùng tỷ lệ 5/13 – 6/13.

**Chưa cập nhật:** sơ đồ 7 và `plans/01` mục 3 vẫn ghi "BH1750 hỏng → chế độ theo giờ 100 %", đã khác với luật mới ở mục 3 trên.

### 5. Dataset

- Thêm **phiên 009** (325 ảnh nhãn `xe`) vào `E:\TKHTN\dataset_split_v2`.
- Chia **theo vị trí** (11 vị trí, không chia theo từng ảnh để tránh rò rỉ): train 199, val 58, test 58, **10 ảnh quá tối** → `khong_dat/qua_toi`.
- `manifest.csv` đã cập nhật, bản cũ lưu ở `manifest.before_p009.csv`.

Tỉ lệ các tập sau khi thêm:

| Lớp | train | val | test |
|---|---|---|---|
| car | 1107 | 210 | 229 |
| car_person | 346 | 58 | 58 |
| person | 375 | 87 | 87 |
| background | 459 | 10 | 27 |
| person_mixed | 178 | 0 | 0 |
| **Tổng** | **2465** | **365** | **401** |

Hai điểm yếu: `background` ở val/test rất ít (10 và 27), và `person_mixed` (178 ảnh) vẫn chưa xếp tay sang `person` hoặc `car_person`.

### 6. Lỗi phần cứng/cấu hình đã xử lý

| Triệu chứng | Nguyên nhân | Xử lý |
|---|---|---|
| Board **khởi động lại liên tục** | Board là **N8R8** (flash **8 MB**), nhưng 3 app build cho 16 MB | Đổi sang 8 MB cho `streetlight-wifi`, `streetlight-node`, `tune-by-wifi`; ghi vào `board-config.md` |
| Cả BH1750 lẫn 4 INA219 offline | App `streetlight-wifi` đang để I2C ở GPIO 41/42 | Đổi sang **SDA = 40, SCL = 39** (bạn chỉnh theo dây thật). 4 INA219 đã đọc được; có thêm bước tự dò đảo SDA/SCL trong `streetlight-wifi` |
| `Detected camera not supported` | Lỗi phần cứng camera (cả `thu_dataset` cũng lỗi) | Bạn xử lý phần cứng; sau đó camera nhận **OV2640** |
| Dòng đèn ≈ 0 mA ở 100 % | Dây đèn chưa nối đúng | Sau khi nối lại: 12–13 mA mỗi bóng |

### 7. Model FOMO: độ chính xác và thời gian xử lý

**Độ chính xác trên Edge Impulse Studio** (ảnh chụp màn hình Results, phiên bản *Unoptimized (float32)*, bài toán Object detection):

| Chỉ số | Giá trị |
|---|---|
| Accuracy | **79,86 %** |
| Precision (không tính nền) | **1,00** |
| Recall (không tính nền) | **0,81** |
| F1 | **0,89** |

Cách đọc: precision 1,00 nghĩa là khi model báo có vật thì đều đúng (không báo nhầm); recall 0,81 nghĩa là còn bỏ sót khoảng 19 % vật thật.
Với đèn đường thì bỏ sót đáng ngại hơn báo nhầm (bỏ sót = đèn không sáng lên), nên cần nhìn kỹ các ảnh bị bỏ sót.
Ảnh chụp không ghi rõ đây là model v2 (128×128) hay v3 (96×96), và là bản **float32**; model chạy trên board là **int8**, nên số trên board có thể thấp hơn.
Nên đối chiếu thêm bản int8 trong Studio (chọn "Model version" = quantized).

**Thời gian xử lý đo trên board** (ESP32-S3 240 MHz, model **v2 128×128, int8**, khung ảnh 320×240 xám, log khởi động):

| Bước | Thời gian |
|---|---|
| Thu nhỏ khung 320×240 → 128×128 (squash) | 3,8 ms |
| Xử lý ảnh của Edge Impulse (xám → int8) | 5,3 ms |
| **Suy luận mạng nơ-ron** | **206 – 207 ms** (hai lần đo: 206 827 µs và 206 448 µs) |
| Tổng một khung (chưa tính lấy ảnh từ camera) | khoảng 216 ms |
| **Tốc độ nhận diện thực tế** (có lấy ảnh) | **3,9 khung/giây** (≈ 256 ms mỗi khung) |
| Hiệu chỉnh đường (`road_zones`) | 430 ms, một lần mỗi đêm |
| Chu kỳ điều khiển đèn (task `control`) | tối đa 5,3 ms khi không in log; 53 ms khi in log, trên mốc 200 ms |

**Bộ nhớ khi chạy suy luận:**

| | Giá trị |
|---|---|
| Arena TFLite của model v2 | 314 060 byte (đặt ở PSRAM, cấp mỗi lần suy luận) |
| RAM trong còn trống | 242 263 → 241 843 byte sau suy luận đầu |
| PSRAM còn trống | 8,23 MB → thấp nhất 7,49 MB (dùng tối đa khoảng 740 KB) |
| Firmware app chính | 1,19 MB trên phân vùng 3 MB (còn trống 62 %) |

**So sánh hai model:**

| | v2 (đang chạy) | v3 (đã thử nạp) |
|---|---|---|
| Input | 128×128 | 96×96 |
| Arena TFLite | 314 060 byte | 185 036 byte |
| Thời gian suy luận trên board | **206 ms** | **chưa đo** (lúc nạp, camera đang lỗi) |

- Model v3 vẫn còn trong `C:\Users\HOANG ANH\Downloads\tkhtn-cpp-mcu-v3-impulse-#1.zip`. Model nhỏ hơn thì nhanh hơn, đáng đo lại nếu muốn tăng fps.
- Đã **khôi phục model v2** theo yêu cầu. Bản sao lưu model đã chạy: `ml/vision/model_edge_impulse/backup_deployed_v2_128x128_2026-11-19/`.

### 8. Dashboard

- Sửa `dashboard/src/components/street.tsx`: **thanh "Ban đêm / Đang sáng / Tổng công suất" nằm dưới khung đường phố** thay vì đè lên ô "Vùng 1";
  nới rộng nhãn "CÓ NGƯỜI/XE" bị cắt chữ.
- Đã build lại `docs/index.html` (xoá file rỗng `docs/.gitkeep`) và kiểm tra bằng Chrome ở chế độ giả lập.
- **Chưa đẩy lên GitHub Pages:** trang công khai vẫn là bản cũ cho tới khi commit và push `docs/`.

### 9. Kiểm tra trước khi push lên git

- Tổng dung lượng git sẽ đẩy khoảng **46 MB**, không file nào quá 20 MB: **không có gì bị GitHub chặn**.
- Không phát hiện mật khẩu: `secrets.csv` đã được `.gitignore`, `nvs.bin` rỗng, các `sdkconfig` không có mật khẩu.
- Nên thêm vào `.gitignore`: `ai_stream/tools/calib_out/` (ảnh do tool sinh ra, khoảng 6 MB) và `*.exe` (file test biên dịch trên máy).

---

## Việc còn dở / bước tiếp theo

1. **Nạp bản firmware mới nhất** lên board. Bản vừa sửa luật lỗi (BH1750 hỏng thì vẫn 20 % + zone) đã build xong nhưng tại thời điểm viết
   chưa được xác nhận đã nạp: cửa sổ `idf.py monitor` đang giữ cổng COM13. Bấm Ctrl+] trong cửa sổ đó rồi nạp lại.
2. **Kiểm tra luật zone ban đêm trên board thật** (chưa từng chạy): để xe/người trong khung đường, đèn quanh vật phải lên 100 %.
3. **Recall 0,81 còn thấp**: xem các ảnh bị bỏ sót trong Studio, bổ sung dữ liệu (nhất là ảnh nền và ảnh người) rồi huấn luyện lại; đo thời gian của model v3.
4. **BH1750 vẫn offline**: đã thử cả hai địa chỉ (0x23 và 0x5C) nhưng không thấy. Cần đo điện áp VCC–GND và thông mạch SDA/SCL của module,
   hoặc thay module.
5. **Commit và push** `docs/` để trang GitHub Pages có bản sửa dashboard; thêm `calib_out/` và `*.exe` vào `.gitignore`.
6. **Nạp `secrets.csv` vào NVS** nếu chưa (WiFi, MQTT). Việc này ghi đè cả phân vùng NVS nên mất điện năng và P100 đang lưu.
7. Cập nhật tài liệu cho khớp: `plans/01` mục 3, sơ đồ 7 trong `plans/04`, `firmware/streetlight-node/README.md` (còn ghi "khởi tạo khi bắt đầu code"),
   và README gốc (còn ghi app chính "chưa có camera/AI").
8. **Chưa làm** (đã có thiết kế): lệnh `cmd/recalibrate` từ dashboard, hướng đi của xe, cảnh báo vật dừng kèm ảnh, cách A (bật từng đèn để tìm vị trí đèn),
   hiệu chỉnh ống kính bằng ảnh bàn cờ, xếp tay `person_mixed`, thêm ảnh nền cho val/test.

## Các điểm cần nhớ khi làm tiếp

- Board thật là **N8R8**: flash 8 MB, PSRAM 8 MB. Sdkconfig để 16 MB thì board khởi động lại liên tục.
- Dây I2C thật: **SDA = GPIO 40, SCL = GPIO 39** (schematic ghi ngược lại).
- Chân đèn của app chính, `ai_stream`, `tune-by-wifi`: **47 / 21 / 20 / 19**. `streetlight-wifi` và `thu_dataset` vẫn dùng 19 / 20 / 21 / 47.
- Nạp firmware qua cổng **UART (COM)**, không dùng cổng USB native (đèn 3 và 4 dùng GPIO 20/19 là chân USB).
- Mỗi lần chỉ nạp một app: các app dùng chung phần cứng.
- Bản C (`road_zones.c`) và bản Python (`zone_calib.py`) phải giữ khớp; sửa một bên thì chạy `check_c_port.py`.
- ESP-IDF v5.5.4 ở `E:\ESP32_IDF\ENV\Espressif\frameworks\esp-idf-v5.5.4` (đặt `IDF_TOOLS_PATH=E:\ESP32_IDF\ENV\Espressif`, chạy `export.ps1`).
