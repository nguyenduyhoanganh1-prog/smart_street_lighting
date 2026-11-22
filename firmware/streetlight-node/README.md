# firmware/streetlight-node

App chính của Mức 1, nạp cho **một ESP32-S3 có camera** (Freenove WROOM CAM N8R8):
camera + FOMO nhận diện người/xe ngay trên board, 4 đèn sáng theo vùng, BH1750 phân biệt ngày/đêm,
4 INA219 đo điện năng từng bóng, gửi số liệu và ảnh lên HiveMQ Cloud cho [dashboard](../../dashboard/).

Board có **2 chế độ**. **Nhấn đôi nút BOOT (IO0)** để chuyển qua lại:

| Chế độ | Làm gì | Mạng |
|---|---|---|
| **Bình thường** (mặc định) | Mọi thứ ở trên: luật đèn, điện năng, MQTT | WiFi nhà + MQTT/TLS |
| **ai-stream** | Chỉ camera + model + đèn + trang web xem trực tiếp (giống app [`ai_stream`](../../ai_stream/)) | Board phát WiFi `StreetLight-AI`, mở http://192.168.4.1 |

## Cách hoạt động (chế độ bình thường)

Ba task FreeRTOS, để việc chậm (mạng, camera) không làm trễ vòng điều khiển đèn:

| Task | Ưu tiên | Việc |
|---|---|---|
| `control` | 6 | Đúng mỗi 200 ms: đọc cảm biến → giờ → kết quả camera → luật đèn → PWM → điện năng → lưu NVS. Không bao giờ chờ mạng hay camera |
| `vision` ([vision.c](main/vision.c)) | 4 | Camera + FOMO (~3–4 khung/s), tìm đường; để kết quả vào "hộp thư" cho `control` đọc |
| `network` | 3 | WiFi, SNTP, MQTT: gửi những gì `control` chuẩn bị, chuyển lệnh từ dashboard vào hàng đợi |

**Luật đèn** ([zone_rule.c](main/zone_rule.c), [lighting.c](main/lighting.c)):
- Ngày: tắt đèn. Đêm: nền 20 %.
- Người ở vùng K → đèn K−1, K, K+1 sáng 100 %, giữ 5 s sau khi người rời đi. Xe: giống vậy, giữ 2 s
  (chưa có nhận biết hướng đi).
- Vật **trên mặt đường hoặc trên lề** mới được tính. Nền bên ngoài lề bị bỏ qua.
- BH1750 lỗi → ngày/đêm theo giờ SNTP (18:00–06:00, chỉnh được), camera vẫn chia vùng bình thường.
- Ban ngày theo giờ SNTP thì **luôn là ngày**: BH1750 báo tối cũng không bật đèn.
- BH1750 vẫn trả lời nhưng **ban ngày (theo giờ) báo tối liên tục 1 phút** (demo; ngoài trời nên ~30 phút)
  → nghi bị che/kẹt ([lux_check.c](main/lux_check.c)): coi như BH1750 lỗi và gửi cảnh báo
  `bh1750` lên dashboard. Cảm biến thấy sáng lại 10 s thì tin lại và tự đóng cảnh báo. Đánh đổi: trời giông
  tối thật ban ngày thì đèn cũng tắt. Muốn demo "đêm" giữa ban ngày thì dùng **ép chế độ đêm** trên dashboard.
- Camera/model lỗi → không có vùng, ban đêm sáng 100 % cả 4 đèn (chế độ "theo giờ").

**Cảnh báo vật dừng lâu** ([stop_detect.c](main/stop_detect.c)), bật/tắt và thời gian (10 s – 30 phút, mặc định
10 s) chỉnh trong Cài đặt của dashboard: **chỉ xe** (người đứng yên bỏ qua) **trên đường hoặc lề** có tâm lệch
< 1,5 ô lưới FOMO (30 px) quá thời gian đó → gửi cảnh báo `vat_dung` (vùng, lúc bắt đầu dừng, tọa độ x/y và độ tin
cậy), rồi ảnh JPEG của khung hình kế tiếp lên `alert/image`, rồi gửi lại cảnh báo (cùng id) kèm `objs` = **mọi xe
của chính khung hình đó** (cả xe đã bấm "Đã xử lý"), sắp từ trái sang phải, mỗi xe có vùng và `since` (đứng yên từ
lúc nào, lấy từ bộ theo dõi). Dashboard vẽ chấm có số (không chữ), xe đang dừng có vòng đỏ, và liệt kê từng xe.
`since` (giờ Unix) chỉ có khi board đã đồng bộ SNTP; luôn kèm `still_s` (đã dừng bao nhiêu giây, đếm bằng đồng hồ
nội) nên dashboard vẫn hiện thời gian dừng khi board chưa có giờ (lệch thêm nếu mở trang lâu sau lúc board gửi).
Vật rời đi (mất > 3 s) hoặc di chuyển → tự đóng cảnh báo. Bấm "Đã xử lý" → không báo lại vật đó (trừ khi nó đi rồi dừng chỗ khác).
Chỉ chạy ban đêm (ban ngày FOMO nghỉ; cảnh báo đang mở giữ tới khi bấm "Đã xử lý").

**Tìm đường và chia vùng** ([road_zones.c](../components/road_zones/road_zones.c)): bật cả 4 đèn tối đa
khoảng 2 s, chụp một khung, tìm mép đường, đầu trái của đường và mép ngoài của hai lề, rồi chia đường thành
4 đoạn dài bằng nhau ngoài thực tế (có tính phối cảnh). Vùng k ứng với đèn k, đèn 1 ở bên trái ảnh.
Bước này chạy:
- sau mỗi lần khởi động (cả ban ngày, nên đèn chớp sáng ~2 s),
- đầu mỗi đêm nếu chưa tìm được (tối đa 3 lần, cách nhau 5 s),
- khi bấm **"Tìm lại đường"** trên dashboard.

Không tìm thấy đường thì 4 vùng là 4 phần đều theo chiều ngang ảnh. Sau **mỗi lần thử**, board gửi ảnh
JPEG của khung hình đó lên dashboard để kiểm tra bằng mắt.

**Điện năng:** tích phân công suất INA219 theo thời gian, lưu NVS mỗi 10 phút và khi sang giờ/ngày mới,
giữ lịch sử 24 giờ. Công suất bóng ở 100 % (P100) tự đo lại khi một bóng sáng ổn định 100 % trong 10 s.

**Bù offset INA219** ([ina_zero.c](main/ina_zero.c)): mỗi module INA219 lệch không vài trăm µA (đèn tắt mà
đọc ra −0,2 mA hoặc +0,2 mA). Khi đèn tắt hẳn và đứng yên ≥ 1 s, board lấy trung bình 50 mẫu (~10 s) làm
offset riêng của kênh đó rồi trừ ở mọi lần đo; các lần sau trộn dần (theo trôi nhiệt). Số > 1,5 mA lúc tắt
là dòng thật, không học. Sau hiệu chỉnh |I| < 0,15 mA coi là 0. Offset chỉ ở RAM: sau khởi động phải đợi
đèn tắt lần đầu (thường là ban ngày) mới có. Log: `lamp k: INA219 zero offset x mA`.

**Đèn hỏng** ([lamp_fault.c](main/lamp_fault.c)): board tự xét và gửi mã `fault` trong gói `energy`,
dashboard chỉ hiển thị. Chỉ xét khi đèn đã **đứng yên ≥ 0,6 s** (đang fade thì số đo không khớp độ sáng),
"hở mạch" khi công suất < 30 % × P100 × độ sáng (chưa có P100 thì khi dòng < 0,5 mA), độ sáng < 10 % thì
không xét. Lỗi phải **kéo dài liên tục 1 phút** mới được báo (một mẫu bình thường xen vào là đếm lại), bình
thường lại **10 s** thì gỡ lỗi. Dashboard dùng cùng quy tắc khi tự đoán lỗi cho board chạy firmware cũ.
Mỗi lần đổi kết luận board in log `lamp k: fault a -> b (...)`.

## Topic MQTT

Tiền tố `streetlight/`. Định dạng chi tiết nằm ở [protocol.ts](../../dashboard/src/lib/protocol.ts) (web) và
[sl_mqtt_codec.h](../components/sl_net/include/sl_mqtt_codec.h) (board): hai file này phải khớp nhau.

| Topic | Chiều | Nội dung |
|---|---|---|
| `online` | board → | `online` / `offline` (LWT), retained |
| `state` | board → | ngày/đêm, lux, chế độ, độ sáng 4 đèn, số người/xe, vùng đang có vật, fps; mỗi 5 s |
| `energy`, `energy/history` | board → | V/I/P, điện năng hôm nay/tổng từng bóng; lịch sử 24 giờ |
| `config` | board → | ngưỡng lux, thời gian sáng/tối dần, giờ đêm, retained |
| `calib` | board → | kết quả lần tìm đường gần nhất (đường, lề, vạch chia vùng, đơn vị pixel), retained |
| `calib/image` | board → | ảnh JPEG (base64) lúc tìm đường, 4 đèn sáng tối đa, retained |
| `alert`, `alert/image` | board → | cảnh báo, retained: `vat_dung` (kèm `x`, `y`, `score` + ảnh JPEG base64 trên `alert/image`), `bh1750` (tối bất thường ban ngày) |
| `cmd/mode`, `cmd/lamp`, `cmd/force_night`, `cmd/config`, `cmd/alert_ack` | → board | lệnh từ dashboard |
| `cmd/recalibrate` | → board | tìm lại đường ngay (payload khác rỗng, ví dụ `1`) |

## Các file chính

| File | Vai trò |
|---|---|
| [main.c](main/main.c) | Khởi động, chọn chế độ, task `control` và `network`, xử lý lệnh |
| [vision.c](main/vision.c) | Task camera + FOMO + tìm đường + nén ảnh tìm đường |
| [zone_rule.c](main/zone_rule.c) | Luật vùng (thuần C, test trên PC) |
| [lamp_fault.c](main/lamp_fault.c) | Phát hiện đèn hỏng từ INA219 (thuần C, test trên PC) |
| [ina_zero.c](main/ina_zero.c) | Tự bù offset của từng INA219 khi đèn tắt (thuần C, test trên PC) |
| [stop_detect.c](main/stop_detect.c) | Theo dõi tâm vật qua các khung, phát hiện vật dừng lâu (thuần C, test trên PC) |
| [lux_check.c](main/lux_check.c) | Phát hiện BH1750 bị che/kẹt: tối bất thường ban ngày (thuần C, test trên PC) |
| [lighting.c](main/lighting.c), [energy_*.c](main/) | Ngày/đêm có trễ, chế độ theo giờ; điện năng |
| [boot_mode.c](main/boot_mode.c), [mode_button.c](main/mode_button.c), [button_click.c](main/button_click.c) | Nhấn đôi IO0 → khởi động lại sang chế độ kia |
| [stream_mode.c](main/stream_mode.c) + `ai_pipeline`, `lamp_ctrl`, `web_server`, `wifi_ap`, `web/index.html` | Chế độ ai-stream (chép từ `ai_stream/main`) |
| [app_config.h](main/app_config.h) | Chân GPIO, địa chỉ I2C, thời gian, ngưỡng |

## Build, nạp, secrets

```
idf.py set-target esp32s3
idf.py -p COMx build flash monitor
```

- Nạp qua cổng **UART (COM)**: đèn 3 và 4 dùng GPIO 20/19 (chân USB D+/D−), PWM tắt cổng USB native.
- Phân vùng app 3 MB ([partitions.csv](partitions.csv)). Board là N8R8: `sdkconfig` phải là flash **8 MB**,
  để 16 MB thì board khởi động lại liên tục.
- **WiFi/MQTT** nằm trong NVS, không nằm trong code. Chép
  [secrets.example.csv](../components/sl_net/secrets.example.csv) thành `secrets.csv` (đã `.gitignore`),
  điền thông tin, rồi nạp:
  ```
  python %IDF_PATH%/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py generate secrets.csv nvs.bin 0x6000
  python -m esptool --chip esp32s3 -p COMx write_flash 0x9000 nvs.bin
  ```
  Việc này **ghi đè cả phân vùng NVS**: mất điện năng tích luỹ và P100 đang lưu. Không có secrets thì board
  vẫn chạy luật đèn, chỉ không có SNTP và MQTT.

## Test trên PC (không cần board)

Từ `test/host/`, cần `gcc` (MSYS2):

```
gcc -std=c11 -O2 -Wall -Wextra -Werror -I../../main -I../../../components/road_zones/include test_zone_rule.c ../../main/zone_rule.c ../../../components/road_zones/road_zones.c -lm -o test_zone_rule
gcc -std=c11 -O2 -Wall -Wextra -Werror -I../../main test_button_click.c ../../main/button_click.c -o test_button_click
gcc -std=c11 -O2 -Wall -Wextra -Werror -I../../main test_lamp_fault.c ../../main/lamp_fault.c -lm -o test_lamp_fault
gcc -std=c11 -O2 -Wall -Wextra -Werror -I../../main test_ina_zero.c ../../main/ina_zero.c -lm -o test_ina_zero
gcc -std=c11 -O2 -Wall -Wextra -Werror -I../../main test_stop_detect.c ../../main/stop_detect.c -lm -o test_stop_detect
gcc -std=c11 -O2 -Wall -Wextra -Werror -I../../main -I../../../components/sl_net/include test_lux_check.c ../../main/lux_check.c ../../main/lighting.c -o test_lux_check
```

`test_sl_codec.c` (codec MQTT) cần thêm cJSON của ESP-IDF, lệnh nằm ở đầu file. Thuật toán tìm đường
có test riêng ở `ai_stream/test/host/test_road_zones.c` và công cụ so C với Python
`ai_stream/tools/check_c_port.py`.

## Lưu ý khi đọc/sửa code

- **Chuyển chế độ = khởi động lại.** Hai chế độ cần chung camera, sóng WiFi (STA ↔ AP) và PSRAM, nên
  khởi động lại sạch đơn giản và an toàn hơn chuyển nóng. Cờ chế độ nằm trong **RTC RAM**, chỉ sống qua
  `esp_restart()`: cúp điện, nút RESET, panic hay watchdog đều đưa board về chế độ bình thường (an toàn
  ngoài thực địa, không ghi flash).
- **IO0 là chân strapping:** giữ thấp lúc reset thì chip vào chế độ nạp firmware. Vì vậy board chuyển chế
  độ khi **nhả** lần nhấn thứ hai, không phải lúc nhấn xuống.
- **Code ai-stream là bản chép:** `ai_pipeline.*`, `lamp_ctrl.*`, `web_server.*`, `wifi_ap.*`, `tune_config.h`,
  `web/index.html` hiện giống hệt bản trong `ai_stream/main`. Sửa bên nào thì chép sang bên kia.
- `road_zones` là component dùng chung với `ai_stream`: đổi thuật toán thì sửa luôn bản Python
  (`zone_calib.py`) và chạy `check_c_port.py`.
- Chưa làm: nhận biết hướng xe, gửi riêng vùng có người/vùng có xe lên MQTT
  (đã tách trong `zone_rule_stats_t`, mới in ra log).
- Chưa kiểm chứng trên board: chuyển chế độ bằng IO0, vùng lề, gửi ảnh tìm đường qua HiveMQ, xét đèn hỏng,
  cảnh báo BH1750, bù offset INA219, cảnh báo vật dừng lâu kèm ảnh (đã có test PC cho phần logic).
