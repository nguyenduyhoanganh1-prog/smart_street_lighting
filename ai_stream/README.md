# ai_stream

Xem **stream camera + kết quả model FOMO (Edge Impulse)** ngay trên board và chỉnh 4 đèn từ điện thoại/laptop.
Dùng để kiểm tra model chạy có ổn không trước khi ghép vào `firmware/streetlight-node`.
ESP32-S3 tự phát WiFi, không cần router/Internet/MQTT.

## Dùng

```
cd ai_stream
idf.py set-target esp32s3
idf.py -p COMx build flash monitor
```

Nạp qua cổng **UART (COM)**: đèn 3 và 4 dùng GPIO 20/19 (chân USB D+/D−) nên cổng USB native bị tắt.

1. Lúc khởi động cả 4 đèn sáng max khoảng 2 giây: board đang tìm mặt đường (xem mục *Chia zone theo đường*).
2. Kết nối WiFi **`StreetLight-AI`**, mật khẩu **`12345678`** (đổi trong `main/tune_config.h`).
3. Mở **http://192.168.4.1**.

Lần build đầu cần Internet một lần để tải `esp32-camera` (IDF Component Manager) và biên dịch lâu
(thư viện Edge Impulse có hơn 1500 file).

## Trang web

- Ảnh camera 320×240 xám, vẽ hộp quanh vật thể model thấy (xe = cam, người = xanh).
- **Khung xanh** = mặt đường tìm được lúc khởi động, **viền cam** = mép ngoài của hai lề, **vạch đỏ** chia 4 vùng
  và kéo dài qua cả lề, số 1–4 là vùng (vùng k ↔ đèn k).
- Mỗi vật là **1 chấm ở tâm** (FOMO chỉ cho tâm, kích thước hộp không có ý nghĩa), cạnh chấm là nhãn
  "Loại · vùng · độ tin cậy". Nhãn và danh sách vật thể ghi vùng của từng vật: "vùng 2", "vùng 2 (lề)", hoặc "ngoài đường"
  (vật đó không bật đèn). Dòng tổng hợp phía trên: "Người: vùng …", "Xe: vùng …".
  Không tìm được đường: 4 vạch trắng chia đều chiều ngang như trước. Dòng "Đường:" cho biết trạng thái.
- Nút **"Tìm lại đường"**: chạy lại bước hiệu chỉnh mà không cần khởi động lại (4 đèn sáng tối đa ~2 s,
  xong thì đèn trở về như trước khi bấm). Dùng khi vừa dời camera hoặc mô hình.
- FPS, thời gian suy luận, RAM nội / PSRAM còn trống.
- 4 thanh trượt chỉnh độ sáng từng đèn (kéo là tắt chế độ tự động).
- Ô **"Đèn tự động theo model"**: vật ở zone k thì đèn k−1, k, k+1 sáng 100 %, giữ 2 s sau khi vật rời đi, các đèn còn lại 20 %.
  Luật rút gọn của plans/01 mục 3 (chưa có hướng di chuyển / tracking).
- Ô **"Khoá phơi sáng"**: giữ exposure/gain cố định như lúc thu dataset.

## Chia zone theo đường (hiệu chỉnh lúc khởi động)

**Mục đích.** Camera nhìn xiên nên đường chạy chéo và có phối cảnh. Chia 4 phần đều theo chiều ngang ảnh
lệch xa vị trí đèn thật, và vật ngoài khu vực đường (giàn sắt, đồ đạc quanh mô hình) cũng bật đèn.
Vì 4 đèn đặt **cách đều** dọc đường, chỉ cần biết khung đường là tính được zone (cách "D").
Người đi trên **lề** (vỉa hè) cũng cần đèn, nên khu vực được tính là mặt đường + hai lề.

**Cách hoạt động** (`firmware/components/road_zones/road_zones.c`, dùng chung với app chính; gọi trong
`ai_pipeline.c` → `calibrate_road()` lúc khởi động và mỗi lần bấm "Tìm lại đường"):

1. Bật 4 đèn 100 %, chờ `APP_CALIB_SETTLE_MS` cho auto exposure, bỏ `APP_CALIB_SKIP_FRAMES` khung, chụp 1 khung.
2. **Mép đường:** quét từng cột, tìm *cạnh* sáng→tối (mép trên) và tối→sáng (mép dưới). Không dùng ngưỡng
   sáng cố định vì mặt đường dưới đèn sáng gần bằng lề. Lề trắng bên trong khoảng chọn (đoạn sáng > 25 hàng)
   bị loại, vạch kẻ đứt ngắn thì được giữ.
3. **Khớp đường thẳng** `y = a·x + b` bằng RANSAC thử mọi cặp điểm (chịu được ~50 % cột sai do giàn sắt,
   hai đầu đường), rồi 3 vòng "dự đoán → bắt cạnh gần nhất" để kéo dài tới hai đầu.
4. **Đầu đường bên trái** (xiên do góc camera): mỗi hàng tìm bước nền tối → mặt đường mà độ sáng *giữ*
   ít nhất 20 cột, khớp `x = a·y + b`.
5. **Lề** (`find_kerbs`): ở mỗi cột của đường, cạnh gần nhất cách mép đường 6–70 hàng là mép ngoài của lề
   (phía trên: tối→sáng, phía dưới: sáng→tối); khớp một đường thẳng cho mỗi bên. Bên nào không tìm thấy
   (ví dụ lề chạy ra khỏi ảnh) thì bên đó giới hạn ở mép đường như cũ.
6. **Zone:** hai mép gặp nhau tại điểm tụ V. Khoảng cách trên ảnh `V − x` tỉ lệ với 1/độ sâu, nên 4 đoạn
   bằng nhau ngoài thực tế = 4 bước bằng nhau của `1/(V − x)`. Zone gần camera rộng, zone xa hẹp.
   Vạch chia là đường dọc nên tự phủ cả lề: người trên lề thuộc vùng của đoạn đường bên cạnh.
7. `lamp_ctrl.c`: tâm vật thể ngoài khu vực đường + lề (cho phép lệch 8 px) bị bỏ qua; zone k bật đèn
   k−1..k+1, **đèn 1 = zone bên trái ảnh**. Không tìm được đường → 4 phần đều theo chiều ngang, stream vẫn chạy.

Log serial khi thành công: `road found in … ms: x …`, `zone boundaries x = …`. Trang web lấy kết quả
qua `GET /api/road` (đường, đầu trái, `kerb_top` / `kerb_bottom`, vạch chia) và gửi `POST /api/recalibrate`
khi bấm nút.

**Kiểm tra trên PC (không cần board):**

| Việc | Lệnh (từ `ai_stream`) |
|---|---|
| Thử thuật toán trên ảnh, xem ảnh từng bước | `python tools/zone_calib.py -o tools/calib_out/x image <ảnh>` (hoặc `offline <phiên dataset>`) |
| Unit test bản C | `cd test/host && gcc -std=c11 -O2 -Wall -Wextra -Werror -I../../../firmware/components/road_zones/include test_road_zones.c ../../../firmware/components/road_zones/road_zones.c -lm -o test_road_zones && ./test_road_zones` |
| So bản C với bản Python | `python tools/check_c_port.py <ảnh...>` |

**Cần lưu ý khi sửa:**

- `road_zones.c` là bản chép của `tools/zone_calib.py` (cùng tên hàm, cùng hằng số). Sửa một bên thì sửa
  bên kia và chạy `check_c_port.py`. Lần cuối (20/11/2026, có so cả lề): 5/5 ảnh của phiên 002 và 008 khớp,
  mép lề lệch ≤ 0.5 px.
- Giả định: camera **không bị dời** sau khi hiệu chỉnh, và đèn đầu/cuối nằm ở hai đầu đoạn đường nhìn thấy.
  Dời camera thì bấm **"Tìm lại đường"** (hoặc reset board).
- Đầu đường bị méo ống kính (cong) nhưng đang khớp bằng đường thẳng, sai số vài pixel.
- Vùng nhớ tạm ~25 KB cấp trong PSRAM rồi giải phóng ngay sau mỗi lần hiệu chỉnh.
- Các file `ai_pipeline.*`, `lamp_ctrl.*`, `web_server.*`, `wifi_ap.*`, `tune_config.h`, `web/index.html` có
  bản chép giống hệt trong `firmware/streetlight-node/main` (chế độ ai-stream của app chính, vào bằng nhấn
  đôi IO0). Sửa bên nào thì chép sang bên kia.
- Chưa làm: cách A (bật từng đèn để tìm vị trí đèn) và bước hỏi người dùng chọn trên web; xem
  `tools/zone_calib.py` cho bản thử cách A.

## Model

`components/edge_impulse/` = bản C++ library xuất từ Studio (project TKHTN, Impulse #1):
FOMO int8, input **128×128 xám**, nhãn `car`, `human`, arena TFLite ≈ 307 KB.
Thay model mới: giải nén zip mới đè lên `model-parameters/`, `tflite-model/`, `edge-impulse-sdk/`
(**không** đè `CMakeLists.txt` của component). `detector.cpp` có `static_assert` báo lỗi build nếu
kích thước input / kiểu lượng tử hoá / chế độ resize đổi.

## Tiền xử lý (phải khớp lúc huấn luyện)

Ảnh thu dataset: 320×240 xám 8-bit (thu_dataset) → Studio resize **squash** về 128×128.
Trên board: cùng khung 320×240 xám → `resize_image_using_mode(..., SQUASH)` của chính SDK → 128×128.

## Cần kiểm tra trên board (chưa đo)

- Dòng log `detector` khi khởi động: heap trước/sau suy luận đầu, thời gian dsp/inference.
- Arena TFLite được cấp phát **mỗi lần suy luận** (cách xuất EON của Edge Impulse), mặc định rơi vào PSRAM
  (lớn hơn `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`).
- So kết quả với Live classification / Model testing trong Edge Impulse Studio trên cùng cảnh.

## Lưu ý

- Build được với ESP-IDF v5.5.4 (không cảnh báo). Phần hiệu chỉnh đường **chưa chạy thử trên board thật**:
  thời gian hiệu chỉnh và kết quả trên cảnh thật xem ở log serial lần chạy đầu.
- Không chạy cùng lúc với `streetlight-node` / `thu_dataset`: chung phần cứng.
- Thư mục `components/edge_impulse/` nặng ≈ 26 MB, 1500+ file. Cân nhắc `.gitignore` và giải nén lại từ
  `ml/vision/model_edge_impulse/*.zip`.
