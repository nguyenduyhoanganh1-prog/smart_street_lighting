# dashboard

Trang web tĩnh (React + Vite + TypeScript) đăng trên GitHub Pages:
**https://nguyenduyhoanganh1-prog.github.io/smart_street_lighting/**

Trang kết nối HiveMQ Cloud qua MQTT WebSocket (TLS) để nói chuyện với
[streetlight-node](../firmware/streetlight-node/). Không có mật khẩu MQTT trong code: người dùng nhập tài
khoản khi mở trang.

## Trang có gì

- Hình đường phố: 4 đèn, độ sáng, công suất, vùng đang có người/xe; thanh tổng lux / số đèn sáng / công suất.
- Điều khiển: chỉnh tay từng đèn (tự về tự động sau 10 phút), ép chế độ đêm.
- **Vùng camera**: ảnh camera lúc board tìm đường, có vẽ chồng mặt đường (xanh), mép lề (cam), 4 vùng (đỏ),
  và nút **"Tìm lại đường"** (board bật 4 đèn tối đa ~2 s rồi gửi ảnh mới). Dùng để kiểm tra vùng chia có
  đúng với mô hình không.
- Điện năng từng bóng (bảng + biểu đồ 24 giờ), dòng điện, lux theo thời gian.
- Cài đặt ngưỡng lux, thời gian sáng/tối dần, giờ đêm; lịch sử sự kiện (lưu trong trình duyệt).
- Cảnh báo vật dừng lâu: ảnh camera, dashboard vẽ chấm ở tâm vật + "Xe · 93%" từ `x`, `y`, `score` của gói
  alert. Cài đặt có công tắc bật/tắt, thời gian dừng (10 s – 30 phút) và độ sáng nền ban đêm (10–100 %).
- Cảnh báo đèn hỏng: dùng mã `fault` board gửi (streetlight-node tự xét, xem `lamp_fault.c`); chỉ khi gói
  không có `fault` (firmware cũ, giả lập) dashboard mới tự đoán từ số đo INA219. Cảnh báo kèm ảnh từ board.
- **Dữ liệu giả lập**: nút ở màn hình đăng nhập, chạy cả trang mà không cần board (có người/xe đi qua,
  bóng hỏng, tìm đường với ảnh vẽ từ số đo thật).

Định dạng các topic MQTT: [src/lib/protocol.ts](src/lib/protocol.ts), phải khớp với
`firmware/components/sl_net/include/sl_mqtt_codec.h` (bảng topic trong README của streetlight-node).

## Chạy và đăng

```
npm install
npm run dev      # chạy thử trên máy: http://localhost:5173
npm run build    # kiểm tra kiểu + xuất ra ../docs/index.html (một file, đã gồm font, CSS, JS)
```

GitHub Pages lấy `docs/index.html` trên nhánh `main`: build xong thì commit file đó và push, trang cập nhật
sau 1–2 phút (trình duyệt có thể giữ bản cũ tối đa 10 phút: Ctrl+F5).

## Các file chính

| File | Vai trò |
|---|---|
| [src/App.tsx](src/App.tsx) | Bố cục trang |
| [src/lib/station.ts](src/lib/station.ts) | Kết nối MQTT hoặc giả lập, nhận/gửi tin, lịch sử |
| [src/lib/protocol.ts](src/lib/protocol.ts) | Topic, kiểu dữ liệu, phát hiện đèn hỏng, kiểm tra cài đặt |
| [src/lib/sim.ts](src/lib/sim.ts) | Board giả lập |
| [src/components/street.tsx](src/components/street.tsx) | Hình đường phố |
| [src/components/calib-panel.tsx](src/components/calib-panel.tsx) | Panel "Vùng camera" (vẽ lại đường/lề/vùng theo cùng công thức với `road_zones.c`) |
