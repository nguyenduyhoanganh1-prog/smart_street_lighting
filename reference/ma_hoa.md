# Mã hoá dữ liệu MQTT trong dự án đèn đường

Tài liệu ghi lại những gì đã tìm hiểu về mã hoá dữ liệu giữa board ESP32-S3, broker EMQX và dashboard,
các cách có thể chọn, ưu nhược của từng cách và lý do chọn AES-256-GCM.

> **Trạng thái:** phần này mới ở mức **thiết kế và thử nghiệm bằng Python**. Firmware (`streetlight-wifi`,
> `firmware/streetlight-node`) và dashboard **chưa có code mã hoá nội dung**. Thứ đang chạy thật chỉ là TLS.
> Xem mục 9 để biết việc còn phải làm.

## Mục lục

1. [Bài toán và hiện trạng](#1-bài-toán-và-hiện-trạng)
2. [Các cách mã hoá, ưu nhược](#2-các-cách-mã-hoá-ưu-nhược)
3. [Cách đã chọn và lý do](#3-cách-đã-chọn-và-lý-do)
4. [AES-256-GCM giải thích từng phần](#4-aes-256-gcm-giải-thích-từng-phần)
5. [Ví dụ số thật](#5-ví-dụ-số-thật)
6. [Luồng bên gửi và bên nhận](#6-luồng-bên-gửi-và-bên-nhận)
7. [Base64](#7-base64)
8. [Gói tin đi qua những lớp nào](#8-gói-tin-đi-qua-những-lớp-nào)
9. [Giới hạn và việc cần làm tiếp](#9-giới-hạn-và-việc-cần-làm-tiếp)
10. [Hình minh hoạ](#10-hình-minh-hoạ)
11. [Thuật ngữ](#11-thuật-ngữ)

---

## 1. Bài toán và hiện trạng

Luồng dữ liệu: **board ESP32-S3 → broker EMQX Cloud → dashboard (GitHub Pages)**. Board gửi trạng thái, điện
năng, ảnh cảnh báo; dashboard gửi lệnh điều khiển đèn. Mọi thứ đi qua topic dạng `streetlight/state`,
`streetlight/energy`, `streetlight/cmd/lamp`...

| Phần | Đã mã hoá chưa | Ghi chú |
|---|---|---|
| Board ↔ broker (`mqtts://`, cổng 8883) | **Rồi (TLS)** | Board kiểm tra chứng chỉ broker bằng bộ CA của ESP-IDF |
| Dashboard ↔ broker (`wss://`, cổng 8084) | **Rồi (TLS)** | Trình duyệt chỉ mở được WebSocket |
| Nội dung tin nhắn trên broker | **Chưa** | Broker giải mã TLS để chuyển tiếp nên đọc được chữ thường |
| Tin `retain` lưu trên broker | **Chưa** | Lưu chữ thường trên server của nhà cung cấp |
| WiFi và MQTT credential trong flash của board (NVS) | **Chưa** | Ai cầm board và có cáp USB đọc được bằng `esptool` |
| Trang web tuning `192.168.4.1` | **Chưa** | HTTP thường, chỉ dựa vào mật khẩu WiFi AP (mặc định `12345678`) |
| File `secrets.csv` trên máy | **Chưa** | Chỉ được `.gitignore` chặn không cho commit |

**Điểm mấu chốt:** TLS chỉ mã hoá **đoạn đường**. Nó không bảo vệ nội dung khỏi chính broker, khỏi người có
tài khoản broker, hay khỏi tin retain đang nằm trên server.

## 2. Các cách mã hoá, ưu nhược

Mỗi cách bảo vệ một **lớp khác nhau**, nên chúng không loại trừ nhau.

### 2.1. Lớp đường truyền (board/dashboard ↔ broker)

| Cách | Ưu | Nhược |
|---|---|---|
| **TLS một chiều** (đang dùng) | Có sẵn, không tốn công; chống nghe lén trên mạng và giả mạo broker | Broker thấy dữ liệu chữ thường; ai có tài khoản đều đăng nhập được |
| **TLS hai chiều (mTLS)**: board có chứng chỉ riêng | Board được xác thực bằng chứng chỉ, không chỉ bằng mật khẩu | Phải tạo và quản lý chứng chỉ cho từng thiết bị. EMQX Serverless có hỗ trợ không thì **chưa kiểm tra** |

### 2.2. Lớp nội dung tin nhắn (mã hoá đầu cuối: broker không đọc được)

| Cách | Ưu | Nhược |
|---|---|---|
| **AES-256-GCM, khoá dùng chung** | Vừa mã hoá vừa chống sửa tin; ESP32-S3 có tăng tốc AES bằng phần cứng; trình duyệt có sẵn Web Crypto | Hai bên phải chia sẻ khoá; nonce không được dùng lại với cùng khoá |
| **ChaCha20-Poly1305** | Nhanh trên chip không có phần cứng AES | Web Crypto của trình duyệt không hỗ trợ, phải thêm thư viện; ESP32-S3 vốn có phần cứng AES nên không cần |
| **Bất đối xứng + AES** (hybrid, dashboard giữ khoá riêng) | Board bị lấy cũng không lộ khoá giải mã | Phức tạp nhất; ESP32 tính RSA/ECDH chậm hơn |
| **AES-CBC** | Đơn giản | Không phát hiện tin bị sửa, phải tự thêm lớp xác thực, dễ làm sai |
| **AES-ECB** | Đơn giản nhất | **Không dùng**: cùng đầu vào cho cùng đầu ra nên ảnh mã hoá vẫn lộ hình dáng |

### 2.3. Lớp chữ ký, chống giả lệnh điều khiển

| Cách | Ưu | Nhược |
|---|---|---|
| **HMAC-SHA256** gắn vào lệnh, kèm bộ đếm hoặc thời gian | Board chỉ nhận lệnh có chữ ký đúng; chống gửi lại lệnh cũ | Không giấu nội dung lệnh. Nếu đã dùng AES-GCM thì GCM đã có xác thực, không cần thêm |

### 2.4. Lớp dữ liệu lưu trên board

| Cách | Ưu | Nhược |
|---|---|---|
| **NVS encryption (HMAC)** | Mật khẩu WiFi/MQTT trong flash không đọc được bằng `esptool` | Ghi eFuse, **không đảo ngược**. Theo tài liệu ESP-IDF chip S3 hỗ trợ, cần kiểm tra lại khi làm |
| **Flash encryption + Secure Boot** | Bảo vệ cả firmware lẫn secrets | Chế độ Release khoá vĩnh viễn việc nạp qua USB, sai là có thể hỏng board |
| **Giữ NVS thường, khoá vật lý** | Không rủi ro, không đổi code | Không phòng được người có board trong tay |

### 2.5. Trang web tuning trên board (`192.168.4.1`)

| Cách | Ưu | Nhược |
|---|---|---|
| **HTTPS với chứng chỉ tự ký** | Mã hoá trang web | Trình duyệt báo "không an toàn" mỗi lần; tốn thêm RAM và flash |
| **Mật khẩu WiFi AP mạnh** (đã có WPA2) | Rất đơn giản, đã mã hoá giữa điện thoại và board | Mật khẩu mặc định `12345678` quá yếu, cần đổi trong `tune_config.h` |

## 3. Cách đã chọn và lý do

**Chọn: giữ TLS làm nền, thêm AES-256-GCM cho nội dung tin nhắn.** Chỉ cần dashboard đọc dữ liệu, không có
công cụ MQTT nào khác cần xem nội dung.

| Lý do | Giải thích |
|---|---|
| Mã hoá **và** xác thực trong một bước | Kẻ xấu không tự chế được lệnh `duty:0` để tắt đèn, vì không tính ra được tag đúng |
| Cả hai đầu đã có công cụ sẵn | Board dùng `mbedtls` (có tăng tốc phần cứng), trình duyệt dùng Web Crypto. Không phải thêm thư viện |
| Không cần bất đối xứng | Chỉ có một board và một dashboard của chính nhóm, nên dùng chung khoá là đủ và đơn giản hơn |
| Khoá từ cụm mật khẩu | Dashboard là trang công khai trên GitHub Pages nên **không được ghi khoá vào code**. Người dùng nhập cụm mật khẩu khi mở trang, cùng cách đang nhập tài khoản MQTT |
| Không chọn ECB, CBC | ECB lộ hình ảnh; CBC thiếu xác thực |
| Chưa chọn NVS encryption | Ghi eFuse không đảo ngược. Nên thử trên board dự phòng trước, làm sau cùng |

**Phạm vi:** áp dụng cho `state`, `energy`, `config`, `energy/history`, `alert`, `alert/image` và mọi lệnh
`cmd/*`. Riêng `streetlight/online` (LWT) **giữ chữ thường** vì chỉ chứa `online`/`offline`.

## 4. AES-256-GCM giải thích từng phần

Ý chính: **có một khoá chung. Gửi thì khoá lại, nhận thì mở ra. Ai không có khoá thì không đọc được và
không sửa được.**

| Thành phần | Là gì | Nếu thiếu thì sao |
|---|---|---|
| **AES-256** | Thuật toán mã hoá khối (16 byte/khối), khoá 256 bit = 32 byte | Thư đọc được |
| **GCM** | Chế độ vận hành: mã hoá (phần Counter) **và** xác thực (phần Galois) cùng lúc | Chỉ là tên gọi của cách làm |
| **Nonce** (12 byte) | Số ngẫu nhiên khác nhau cho mỗi tin; **không bí mật**, gửi kèm tin | Hai tin giống nhau trông giống nhau; dùng lại nonce thì lộ nội dung |
| **Ciphertext** | Nội dung gốc XOR keystream | Chính là dữ liệu đã giấu |
| **Tag** (16 byte) | "Con dấu" xác thực, tính từ khoá, nonce, ciphertext, AAD | Kẻ xấu sửa hoặc tự chế tin mà không bị phát hiện |
| **AAD** | Dữ liệu **không mã hoá nhưng được đưa vào tag**. Ở đây là tên topic | Kẻ xấu lấy tin của topic này gửi sang topic khác mà không bị phát hiện |
| **PBKDF2** | Công thức biến cụm mật khẩu thành khoá 32 byte, cố tình chạy chậm | Phải nhớ khoá 32 byte ngẫu nhiên, hoặc khoá yếu |

### 4.1. Ciphertext được tạo ra thế nào

```
ciphertext = nội dung gốc  XOR  keystream
keystream  = AES(khoá, nonce + bộ đếm 2) ghép với AES(khoá, nonce + bộ đếm 3) ghép ...
```

- AES nhận **16 byte** (nonce 12 byte + bộ đếm 4 byte) và trả về 16 byte trông ngẫu nhiên.
- XOR từng byte nội dung gốc với keystream. XOR hai lần cùng một số thì trở về ban đầu, nên bên nhận chỉ cần
  tạo lại đúng keystream rồi XOR lần nữa.
- Bộ đếm bắt đầu từ **2** vì số 1 dành cho bước che tag.
- Keystream chỉ phụ thuộc **khoá** và **nonce**. Đổi nonce thì keystream đổi hoàn toàn.

**Vì sao nonce không được lặp:** nếu hai tin dùng cùng khoá và cùng nonce thì có cùng keystream, và
`cipher1 XOR cipher2 = plain1 XOR plain2` (keystream tự triệt tiêu), kẻ nghe lén suy ra được nội dung.

### 4.2. Tag được tạo ra thế nào

Tag tính từ **4 thứ**: khoá, nonce, AAD và ciphertext. Không phải chỉ khoá và nonce.

```
H     = AES(khoá, 16 byte số 0)
X     = trộn bằng H lần lượt: các khối AAD → các khối ciphertext → khối độ dài (đệm 0 cho đủ 16 byte)
Tag   = X  XOR  AES(khoá, nonce + bộ đếm 1)
```

Phép "trộn" là phép nhân trong trường Galois 128 bit (phần "G" của GCM). Không cần nắm toán học, chỉ cần biết
mọi thay đổi nhỏ ở AAD hay ciphertext đều làm tag đổi hoàn toàn.

### 4.3. Vì sao lại là những con số này

| Con số | Lý do |
|---|---|
| **16 byte** (khối AES, tag) | AES được thiết kế xử lý khối 128 bit. Tag đủ 16 byte thì xác suất đoán đúng là 1/2¹²⁸ |
| **32 byte** (khoá) | AES cho 128, 192 hoặc 256 bit. Chọn 256 cho dư an toàn; 128 hiện nay cũng được coi là đủ |
| **12 byte** (nonce) | Độ dài 96 bit có đường tính nhanh nhất của GCM: nonce 12 + bộ đếm 4 = đúng 16 byte. Độ dài khác phải tính thêm một bước |
| **4 byte** (bộ đếm) | Phần còn lại của khối: `16 − 12 = 4` |
| **Đếm từ 2** | Số 1 dành cho bước che tag |
| **0x88, 0xb8** trong khối độ dài | Độ dài tính **bằng bit**: AAD 17 byte × 8 = 136 = 0x88; ciphertext 23 byte × 8 = 184 = 0xb8 |
| **Đệm số 0** | Phép trộn chỉ làm việc trên khối đúng 16 byte |

**Mức an toàn của nonce ngẫu nhiên:** theo khuyến nghị của NIST, nonce 96 bit ngẫu nhiên nên dùng tối đa
khoảng 2³² tin cho một khoá. Với 2 tin mỗi 5 giây (34.560 tin/ngày) thì phải mất khoảng 340 năm mới chạm
giới hạn đó.

**Nonce lấy ở đâu:** trên board, từ bộ sinh số ngẫu nhiên phần cứng của chip, hàm `esp_fill_random()`. Khi
WiFi đang bật, chip dùng nhiễu sóng vô tuyến làm nguồn ngẫu nhiên thật. Dự án luôn bật WiFi để gửi MQTT.
Trên trình duyệt dùng `crypto.getRandomValues()`.

### 4.4. Khoá sinh từ cụm mật khẩu

```
khoá = PBKDF2-HMAC-SHA256(cụm mật khẩu, salt cố định, 100.000 vòng) → 32 byte
```

- Cùng cụm mật khẩu, cùng salt, cùng số vòng → **cùng khoá** ở hai bên, nên không ai phải gửi khoá cho ai.
- Số vòng càng lớn thì kẻ dò mật khẩu càng chậm, nhưng board cũng chậm theo. **100.000 vòng chỉ là đề xuất,
  chưa đo trên ESP32-S3.**
- **Độ an toàn phụ thuộc độ mạnh của cụm mật khẩu.** Cụm ngắn dễ bị dò ngoại tuyến từ ciphertext bắt được.
  Dùng 5-6 từ ngẫu nhiên.

## 5. Ví dụ số thật

Chạy bằng Python trong buổi tìm hiểu. Cụm mật khẩu **chỉ để minh hoạ**: `den duong nhom 15 demo`, salt
`streetlight-v1`, 100.000 vòng. Nội dung gốc rút gọn `{"night":true,"lux":12}`, AAD là `streetlight/state`.

| Thành phần | Giá trị (hex) | Độ dài |
|---|---|---|
| Khoá | `1119256bb01666b97dd98bcea7398b86ca5e6c62572d16a8283dc91ccc8f9d8c` | 32 byte |
| Nonce | `6b5c4d8d139a63db8707f3be` | 12 byte |
| Ciphertext | `a175387bbddc9c7deebcdf4149a34c67770b4ff181c200` | 23 byte |
| Tag | `7f2f0d5521e8ebd13716ea42d77452dc` | 16 byte |
| Gói = nonce + ciphertext + tag | | 51 byte |
| Sau Base64 | `a1xNjROaY9uHB/O+oXU4e73cnH3uvN9BSaNMZ3cLT/GBwgB/Lw1VIejr0TcW6kLXdFLc` | 68 ký tự |

**Tạo ciphertext bằng tay** (bảng dưới liệt kê 6 byte đầu trong tổng 23 byte):

```
Khối 1: AES(khoá, nonce + 00000002) = da575612dab4e85fd4c8ad342c8f6e0b
Khối 2: AES(khoá, nonce + 00000003) = 02736dcbb0f07ddd51baabf87b1f2e03   (dùng 7 byte đầu)
```

| # | Ký tự | Gốc (bin) | Keystream (bin) | Ciphertext (bin) |
|---|---|---|---|---|
| 1 | `{` 7b | 01111011 | 11011010 (da) | 10100001 (a1) |
| 2 | `"` 22 | 00100010 | 01010111 (57) | 01110101 (75) |
| 3 | `n` 6e | 01101110 | 01010110 (56) | 00111000 (38) |
| 4 | `i` 69 | 01101001 | 00010010 (12) | 01111011 (7b) |
| 5 | `g` 67 | 01100111 | 11011010 (da) | 10111101 (bd) |
| 6 | `h` 68 | 01101000 | 10110100 (b4) | 11011100 (dc) |

Kết quả tính tay khớp từng byte với thư viện (`cryptography` của Python). Byte thứ 23 của ciphertext là `00`
là trùng hợp (`}` đúng bằng keystream ở vị trí đó).

**Tạo tag bằng tay:**

| Bước | Khối đưa vào | Giải nghĩa | X sau bước |
|---|---|---|---|
| H | | `AES(khoá, 16 byte số 0)` = `b3e0ea6d56d7edaae7430dd8a018e7c6` | |
| 1 | `7374726565746c696768742f73746174` | 16 byte đầu của `streetlight/state` | `1472930b...b1ad` |
| 2 | `65000000...00` | byte `e` còn lại, đệm số 0 | `c730c0cb...82c3` |
| 3 | `a175387b...4c67` | 16 byte đầu ciphertext | `a25d4d5c...0c76` |
| 4 | `770b4ff1...0000` | 7 byte còn lại, đệm số 0 | `79eb3b67...7e7a` |
| 5 | `000000000000008800000000000000b8` | độ dài bit của AAD (0x88) và ciphertext (0xb8) | `a6a9040a5b7e83f3dcfd776285e58a2b` |

```
Mặt nạ = AES(khoá, nonce + 00000001) = d986095f7a966822ebeb9d205291d8f7
Tag    = Mặt nạ XOR X = 7f2f0d5521e8ebd13716ea42d77452dc    (khớp với thư viện)
```

**Thí nghiệm chứng minh vai trò của tag và AAD:**

| Kẻ xấu làm gì | Kết quả |
|---|---|
| Giải mã đúng khoá, đúng topic | Ra `{"night":true,"lux":12}` |
| Đổi 1 bit trong tin | `InvalidTag`, bị từ chối |
| Lấy tin `state` gửi sang topic `cmd/mode` | `InvalidTag` (nhờ AAD) |
| Dùng sai cụm mật khẩu | `InvalidTag` |

## 6. Luồng bên gửi và bên nhận

Cùng một gói tin, hai bên làm các bước **đối xứng**.

| Bước | Bên gửi (board) | Bên nhận (dashboard) |
|---|---|---|
| Khoá | Tạo từ cụm mật khẩu | Tạo từ cụm mật khẩu. **Ra cùng khoá** |
| Đầu vào | Bản gốc JSON | Chuỗi base64 nhận được |
| Nonce | **Bốc mới** 12 byte ngẫu nhiên | **Lấy ra** từ 12 byte đầu của gói |
| AAD | Tên topic mình sắp gửi | Tên topic của tin vừa nhận |
| Keystream | AES(khoá, nonce + bộ đếm 2, 3...) | Giống hệt bên gửi |
| Ciphertext | **Tạo**: bản gốc XOR keystream | **Lấy ra** từ phần giữa của gói |
| Tag | **Tính** rồi đính vào gói | **Tính lại** và **so sánh** với tag nhận được |
| Nếu tag sai | | **Vứt gói**, không giải mã |
| Giải mã | | Ciphertext XOR keystream = bản gốc |
| Đóng gói | Ghép `nonce + ciphertext + tag` (51 byte) | Tách gói theo vị trí cố định |
| Chuyển dạng | Byte → base64 (68 ký tự) | Base64 → byte |

**Thứ tự thực tế:**

| Bên gửi | Bên nhận |
|---|---|
| 1. Chuẩn bị JSON | 1. Nhận base64 |
| 2. Tạo khoá (lúc khởi động) | 2. Đổi base64 → byte |
| 3. Bốc nonce | 3. Tách nonce, ciphertext, tag |
| 4. Tạo keystream | 4. Tạo khoá từ cụm mật khẩu |
| 5. XOR ra ciphertext | 5. Tính lại tag, so sánh |
| 6. Tính tag | 6. Sai → vứt; đúng → tiếp |
| 7. Ghép gói | 7. Tạo keystream |
| 8. Đổi base64 | 8. XOR ra bản gốc |
| 9. Gửi | 9. Đọc JSON, hiển thị |

**Vì sao kiểm tra tag trước khi giải mã:** nếu giải mã trước rồi mới kiểm tra, kẻ xấu có thể thử gửi gói giả
để dò thông tin.

**Chiều ngược lại** (dashboard gửi lệnh cho board) làm y hệt, hai bên đổi vai. Board bỏ qua lệnh có tag sai,
nên người lạ không điều khiển được đèn.

**Giải mã chuỗi ví dụ** (đã chạy thật, chỉ dùng cụm mật khẩu và tên topic): base64 → 51 byte → tách 12 / 23 / 16
→ tạo khoá → tag tự tính `7f2f0d55...52dc` khớp tag nhận → tạo keystream → XOR → `{"night":true,"lux":12}`.

## 7. Base64

**Là gì:** cách **viết lại** byte thành chữ cái. Nó **không làm gì với bảo mật**, chỉ để gói tin đi qua mọi nơi
xử lý chữ (log, JSON, ô nhập) mà không bị hỏng.

**Vì sao cần:** gói 51 byte chứa những số từ 0 đến 255 trông như rác. Trong gói ví dụ, ciphertext kết thúc
bằng byte `00`. Với ngôn ngữ C, `00` nghĩa là hết chuỗi: code nào xử lý gói như chuỗi chữ (`strlen`, `printf`)
sẽ cắt mất phần sau, gồm cả tag.

**Cách chuyển** (3 byte → 4 ký tự), ví dụ `Man`:

```
M = 01001101    a = 01100001    n = 01101110
nối 24 bit:   010011010110000101101110
cắt 6 bit:    010011 | 010110 | 000101 | 101110
số:             19        22        5        46
tra bảng:        T         W        F         u        →   TWFu
```

Bảng 64 ký tự: `A-Z` là 0-25, `a-z` là 26-51, `0-9` là 52-61, `+` là 62, `/` là 63. Các bit **không đổi**,
chỉ cắt lại theo khổ 6 bit (vì `2⁶ = 64`). Thiếu byte thì đệm bit 0 và thêm dấu `=`:
`Ma → TWE=`, `M → TQ==`. Gói 51 byte chia hết cho 3 nên không có dấu `=`.

**Nhầm số hay gặp:** 51 là số **byte** của gói, 64 là số **loại ký tự** của bảng Base64, 68 là số **ký tự** sau
khi đổi (`51 ÷ 3 × 4`).

| | Byte thô (51 byte) | Base64 (68 ký tự) |
|---|---|---|
| Dung lượng | Nhỏ hơn 33% | To hơn 33% |
| Xem trong MQTTX, log | Thấy rác, khó copy | Copy được |
| Dễ lỗi khi xử lý | Phải cẩn thận không coi là chuỗi chữ | An toàn |
| Ảnh JPEG | Giảm khoảng 25% so với Base64 | Đang dùng sẵn cho `alert/image` |

**Chọn Base64** cho giai đoạn này vì cần nhìn được gói tin khi kiểm tra. MQTT gửi byte thô và retain bình
thường được, nên khi cần tiết kiệm dung lượng (ví dụ ảnh) có thể chuyển sang byte thô cho riêng topic đó.

## 8. Gói tin đi qua những lớp nào

Gói 51 byte không đi một mình. Nó nằm trong ba lớp:

```
Gói mã hoá 51 byte
  │ Base64
  ▼
Payload 68 ký tự
  │ MQTT bọc: + cờ (QoS, retain) + topic + mã gói
  ▼
Gói PUBLISH 91 byte
  │ TLS mã hoá + tiêu đề
  ▼
Dữ liệu loạn ──WiFi──► Internet ──► EMQX
```

Gói MQTT PUBLISH thật của ví dụ (91 byte):

| Phần | Byte (hex) | Giải nghĩa |
|---|---|---|
| Tiêu đề cố định | `33` | `3` = PUBLISH; các cờ bên dưới: QoS 1 và retain = 1 |
| | `59` | Phần còn lại dài 89 byte |
| Độ dài topic | `00 11` | 17 byte |
| Tên topic | `streetlight/state` | **Không mã hoá**, broker cần đọc |
| Mã gói | `00 01` | Dùng vì QoS 1: broker trả lời "đã nhận gói số 1" |
| Payload | 68 ký tự base64 | |

Tổng: `2 + 2 + 17 + 2 + 68 = 91 byte`. Broker **không giải mã Base64 hay AES**, chỉ đọc topic và chuyển
payload nguyên văn. Lớp AES-GCM nằm trong cùng, lớp TLS ở ngoài cùng.

### Ai thấy được gì

| Thông tin | Board | Dashboard | Broker EMQX | Người nghe lén trên mạng |
|---|---|---|---|---|
| Khoá (từ cụm mật khẩu) | Có | Có | Không | Không |
| Nội dung JSON | Có | Có | Không | Không |
| Tên topic | Có | Có | Có | Không, TLS che |
| Nonce, ciphertext, tag | Có | Có | Thấy, không đọc được | Không, TLS che |

Người nghe lén trên mạng chỉ thấy các gói TLS: biết có dữ liệu chạy, kích thước và thời điểm. Phần AES-GCM
bảo vệ thêm ở phía broker, nơi TLS đã được bóc ra. Cả broker và người nghe lén vẫn thấy được **kích thước và
thời điểm** của tin, mã hoá nội dung không che được điều đó.

## 9. Giới hạn và việc cần làm tiếp

### Giới hạn đã biết

| Giới hạn | Chi tiết |
|---|---|
| **Replay lệnh** | Người có tài khoản broker vẫn gửi lại được nguyên một lệnh mã hoá đã bắt được. Cách chặn: thêm bộ đếm tăng dần vào mỗi lệnh, board nhớ số lớn nhất đã nhận |
| **Mất cụm mật khẩu** | Không khôi phục được dữ liệu. Lưu vào trình quản lý mật khẩu |
| **Khoá trong NVS thường** | Ai cầm board và đọc được flash thì lấy được cụm mật khẩu, trừ khi bật NVS encryption |
| **Dung lượng tăng khoảng 37%** | Base64 cộng thêm nonce và tag. Bộ đệm gửi 1536 byte trong `sl_mqtt.c` phải tăng |
| **PBKDF2 tốn thời gian** | Chạy một lần lúc khởi động board; số vòng chưa đo trên ESP32-S3 |
| **Không thấy nội dung bằng công cụ MQTT** | MQTTX hay Web Client của EMQX chỉ thấy chuỗi loạn. Chấp nhận được vì chỉ dashboard cần đọc |
| **Không tương thích ngược** | Dashboard bản cũ không đọc được tin mới; phải build và đưa bản mới lên GitHub Pages |

### Việc còn phải làm

- [ ] Viết code mã hoá/giải mã cho board: component `sl_mqtt` (`publish_raw`, `handle_data`) dùng `mbedtls`.
- [ ] Thêm key `enc_pass` vào `secrets.csv` và `sl_secrets.c`.
- [ ] Viết giải mã/mã hoá cho dashboard: `dashboard/src/lib/station.ts` dùng Web Crypto.
- [ ] Thêm ô nhập cụm mật khẩu vào `dashboard/src/components/login.tsx`.
- [ ] Tăng bộ đệm gửi; đo thời gian PBKDF2 trên board rồi chốt số vòng.
- [ ] Đồng bộ code giữa `streetlight-wifi/components/sl_mqtt` và `firmware/components/sl_net`.
- [ ] Thêm bộ đếm chống replay cho lệnh `cmd/*`.
- [ ] Đổi mật khẩu WiFi AP mặc định `12345678` trong `tune_config.h`.
- [ ] Dùng tài khoản MQTT riêng cho board và dashboard, giới hạn quyền (ACL) trong `streetlight/#`.
- [ ] Cân nhắc NVS encryption **trên board dự phòng** trước khi làm trên board chính.

### Về bí mật trong repo

Không ghi mật khẩu thật vào tài liệu hay lên GitHub. `secrets.csv` và `secrets.bin` đã nằm trong
`.gitignore`; kiểm tra bằng `git status` trước mỗi lần commit.

## 10. Hình minh hoạ

Sơ đồ **"Hành trình một gói tin"**: bên gửi (board) ở cột trái đi từ trên xuống, bên nhận (dashboard) ở cột
phải đi ngược từ dưới lên, các bước đối xứng nằm cùng hàng, kèm bảng "Ai thấy được gì".

- Link: <https://claude.ai/artifact/5dkefWqbAUNBjLnaqAZjxr>
- Trang này **riêng tư**, chỉ chủ tài khoản đã tạo mở được. Muốn gửi cho nhóm thì dùng nút Share trên trang.

Hình gốc nằm trong hai khối chữ ở mục 6 và mục 8 của tài liệu này, nên vẫn đọc được khi không mở được link.

## 11. Thuật ngữ

| Từ | Nghĩa |
|---|---|
| **Mã hoá đối xứng** | Hai bên dùng chung một khoá để khoá và mở |
| **Mã hoá đầu cuối** | Chỉ hai đầu đọc được, kể cả broker ở giữa |
| **Khoá** | Bí mật chung, ở đây 32 byte |
| **Nonce** | Số ngẫu nhiên dùng một lần cho mỗi tin; không bí mật |
| **Keystream** | Dãy byte do AES sinh ra từ khoá và nonce, dùng để XOR với nội dung |
| **Ciphertext** | Nội dung đã mã hoá |
| **Tag** | Con dấu xác thực 16 byte, phát hiện tin bị sửa hoặc giả mạo |
| **AAD** | Dữ liệu không mã hoá nhưng được xác thực (ở đây là tên topic) |
| **PBKDF2** | Công thức sinh khoá từ cụm mật khẩu, cố tình chạy chậm |
| **XOR** | Phép toán từng bit: giống nhau cho 0, khác nhau cho 1 |
| **Base64** | Cách viết byte thành chữ cái, 3 byte thành 4 ký tự |
| **TLS** | Mã hoá đường truyền (đoạn giữa hai đầu và broker) |
| **MQTT PUBLISH** | Gói tin MQTT mang dữ liệu: topic cộng payload |
| **QoS 1** | Chế độ gửi cần broker xác nhận đã nhận |
| **Retain** | Broker giữ tin mới nhất của topic để người vào sau thấy ngay |
| **Replay** | Gửi lại nguyên một gói hợp lệ đã bắt được |
| **NVS** | Vùng nhớ flash của ESP32 lưu cấu hình, đang chứa mật khẩu WiFi/MQTT |
| **eFuse** | Cầu chì trong chip, ghi một lần, không đảo ngược |
