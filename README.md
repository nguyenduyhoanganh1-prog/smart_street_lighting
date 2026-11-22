# smart-street-lighting

Hệ thống chiếu sáng công cộng thông minh, tiết kiệm năng lượng, có sử dụng AI (đồ án môn học, mô hình thu
nhỏ với 4 đèn và xe/người đồ chơi).

**Mức 1:** một ESP32-S3 có camera chạy FOMO (Edge Impulse) nhận diện người/xe ngay trên board và điều khiển
đèn theo zone: ban ngày tắt, ban đêm nền 20 %, có người/xe thì sáng 100 % vùng cần thiết. Vật dừng lâu thì
cảnh báo kèm ảnh (dashboard vẽ chấm vị trí, loại vật, độ tin cậy; bật/tắt và thời gian chỉnh trên dashboard). Mỗi bóng có 1 INA219 đo dòng/áp để tính **điện năng tiêu thụ thật** của từng bóng;
board dùng chính số đo này để **tự phát hiện đèn hỏng**, và cảnh báo khi cảm biến ánh sáng báo tối bất thường
giữa ban ngày (nghi bị che/kẹt).
Dashboard web tĩnh (GitHub Pages) kết nối HiveMQ Cloud qua MQTT để xem thông số, điện năng từng bóng, ảnh
vùng camera và điều khiển từ xa: **https://nguyenduyhoanganh1-prog.github.io/smart_street_lighting/**
(có nút "Dữ liệu giả lập" để xem thử không cần board).

```
ESP32-S3 CAM (FOMO + luật + BH1750 + 4 INA219 + 4 LED) ──MQTT/TLS──► HiveMQ Cloud ◄──WebSocket/TLS── Dashboard web (GitHub Pages)
```

## Các app ESP32 và trạng thái

Tất cả là project ESP-IDF cho cùng một board (ESP32-S3 có camera, xem
[board-config](.claude/rules/board-config.md)). **Mỗi lần chỉ nạp một app**: chúng dùng chung phần cứng.

| App | Làm gì | Kết nối | Trạng thái |
|---|---|---|---|
| [`firmware/streetlight-node/`](firmware/streetlight-node/) | **App chính**: camera + FOMO, 4 đèn theo vùng (đường + lề), BH1750, 4 INA219, điện năng, MQTT lên HiveMQ. Nhấn đôi **IO0** để chuyển sang chế độ ai-stream (xem trực tiếp qua WiFi của board) và ngược lại | WiFi nhà + MQTT/TLS; chế độ ai-stream: board phát `StreetLight-AI` | Đã chạy trên board (FOMO ~3,9 khung/s). Chưa thử trên board: chuyển chế độ IO0, vùng lề, ảnh tìm đường lên dashboard, đèn hỏng do board tự xét, cảnh báo BH1750 tối bất thường ban ngày, bù offset INA219, cảnh báo vật dừng lâu kèm ảnh |
| [`ai_stream/`](ai_stream/) | Camera + FOMO trên board, đèn theo vùng; trang web xem trực tiếp, vẽ đường/lề/vùng, mỗi người/xe là 1 chấm ở tâm kèm vùng và độ tin cậy, nút "Tìm lại đường" | Board phát WiFi `StreetLight-AI` | Build được; dùng để thử model và chia vùng |
| [`streetlight-wifi/`](streetlight-wifi/) | Như app chính nhưng **không camera, không MQTT**: xem số đo và chỉnh đèn qua trang web trên board | Board phát WiFi `StreetLight-Tune` | Build được, chưa thử trên board |
| [`tune-by-wifi/`](tune-by-wifi/) | Bản cũ hơn của `streetlight-wifi`, dùng lại code trong `firmware/` | Board phát WiFi `StreetLight-Tune` | Bản cũ; nên dùng `streetlight-wifi` |
| [`thu_dataset/`](thu_dataset/) | Thu ảnh dataset: ảnh xám 320×240, gắn nhãn, đổi mức đèn theo lô | Board phát WiFi `StreetLight-Dataset` | Đang dùng; thẻ SD lỗi CRC nên ảnh được tải về PC qua WiFi |

Trang web của các app chạy tại **http://192.168.4.1** sau khi nối vào WiFi của board (mật khẩu mặc định
`12345678`, đổi trong `main/tune_config.h` của từng app).

## Thư mục khác

| Thư mục | Nội dung |
|---|---|
| [`firmware/components/`](firmware/components/) | Component ESP-IDF dùng chung: `bh1750`, `ina219`, `lamp_pwm`, `sl_net` (WiFi, MQTT, codec JSON, đọc secrets), `road_zones` (tìm đường, lề, chia 4 vùng) |
| [`dashboard/`](dashboard/) | Mã nguồn dashboard (React + Vite + TypeScript); `npm run build` xuất ra `docs/` |
| [`docs/`](docs/) | Bản build của dashboard (một file HTML) cho GitHub Pages |
| [`ml/vision/`](ml/vision/) | Notebook train FOMO (Kaggle), model đã train, bản C++ library xuất từ Edge Impulse |
| [`plans/`](plans/) | `00` tổng quan, `01` AI + luật + điện năng + dataset, `02` linh kiện, `03` sơ đồ nối dây |
| [`reference/`](reference/) | Datasheet, ghi chú module (INA219...), sơ đồ nguyên lý |

## Quy trình AI (từ ảnh đến board)

```
thu_dataset (board)  ──WiFi──►  thu_dataset/tools/pull_dataset.py   ảnh xám 320x240 + index.csv (mức đèn từng ảnh)
                               thu_dataset/tools/convert_raw.py     PGM -> PNG
                               thu_dataset/tools/split_dataset.py   chia train/val/test THEO VỊ TRÍ vật (tránh rò rỉ)
                               ml/vision/*.ipynb, Edge Impulse      train FOMO int8, xuất C++ library
                               ai_stream/components/edge_impulse/   thử model trên board (stream + hộp vật thể)
                               firmware/streetlight-node            app chính (dùng chung model, không chép thêm bản)
```

- Dataset ảnh **không nằm trong repo** (vài trăm MB): thư mục `dataset/` (ảnh gốc theo phiên `phien_NNN`) và
  `dataset_split_v2/` (train/val/test) nằm cạnh thư mục repo.
- Model đang chạy trong `ai_stream`: FOMO int8, input 128×128 xám, nhãn `car` / `human`.
- Chia vùng theo đường: bật 4 đèn tối đa, tìm mép đường và **mép ngoài hai lề**, chia đường thành 4 đoạn
  bằng nhau ngoài thực tế. Người/xe trên đường **hoặc trên lề** bật đèn, nền ngoài lề bị bỏ qua. Thuật toán
  thử trên PC bằng `ai_stream/tools/zone_calib.py`, bản C trong `firmware/components/road_zones/road_zones.c`
  (dùng chung cho cả hai app); `ai_stream/tools/check_c_port.py` kiểm tra bản C ra cùng kết quả với Python.
  Chi tiết trong [ai_stream/README.md](ai_stream/README.md).
- Kiểm tra vùng bằng mắt: sau mỗi lần tìm đường, streetlight-node gửi ảnh lên dashboard (panel "Vùng camera"),
  ai-stream vẽ trực tiếp trên trang của board. Cả hai có nút **"Tìm lại đường"**.

## Build và nạp

Cần ESP-IDF v5.5 (đã build với v5.5.4), target `esp32s3`:

```
cd <app>
idf.py set-target esp32s3
idf.py -p COMx build flash monitor
```

Nạp qua cổng **UART (COM)**, không phải cổng USB native: đèn dùng GPIO 19/20 (chân USB D−/D+) nên PWM tắt
cổng USB native. Lần build đầu cần Internet để IDF Component Manager tải `esp32-camera`.

Phần logic thuần C (luật vùng, nhấn đôi nút, codec MQTT, tìm đường, phát hiện đèn hỏng, kiểm tra BH1750)
có **test chạy trên PC** bằng `gcc`,
không cần board: `firmware/streetlight-node/test/host/` và `ai_stream/test/host/`, lệnh build ở đầu mỗi file
và trong [README của streetlight-node](firmware/streetlight-node/README.md).

## Lưu ý

- **Thứ tự chân đèn chưa thống nhất:** `streetlight-node` và `ai_stream` dùng Đèn 1..4 = GPIO 47, 21, 20, 19
  (đã kiểm tra trên board, đèn 1 = vùng trái ảnh); `streetlight-wifi`, `thu_dataset` vẫn dùng 19, 20, 21, 47.
  Dùng các app đó với dây hiện tại thì sửa `main/app_config.h` của chúng.
- **Nút RESET luôn đưa streetlight-node về chế độ bình thường** (cờ chế độ chỉ sống qua khởi động lại bằng
  phần mềm). Muốn vào ai-stream thì nhấn đôi IO0; đừng giữ IO0 lúc reset (board sẽ vào chế độ nạp firmware).
- **Demo "đêm" giữa ban ngày:** dùng nút ép chế độ đêm trên dashboard, đừng chỉ tắt đèn phòng. Nếu giờ
  SNTP là ban ngày mà BH1750 báo tối liên tục 2 phút, streetlight-node coi cảm biến bị che: tắt đèn và gửi
  cảnh báo (`APP_LUX_SUSPECT_MS` trong `app_config.h`; ngoài trời nên để ~30 phút).
- **Đèn hỏng do board tự xét** (`lamp_fault.c`): cần mỗi bóng sáng ổn định 100 % ít nhất 10 s một lần để board
  đo P100; trước đó chỉ phát hiện được bóng hở mạch hoàn toàn (dòng ≈ 0).
- **Secrets:** tài khoản WiFi/MQTT nằm trong `secrets.csv` (đã `.gitignore`), nạp vào NVS; mẫu ở
  `firmware/components/sl_net/secrets.example.csv`. Không commit mật khẩu thật.
- **Không commit:** thư mục `build/`, `managed_components/`, dataset ảnh, kết quả sinh ra của tool
  (`ai_stream/tools/calib_out/`), file `nvs.bin` (ảnh NVS có thể chứa mật khẩu WiFi/MQTT).
- Các trang web trên board không có đăng nhập, chỉ dựa vào mật khẩu WiFi AP.
