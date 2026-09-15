# firmware/streetlight-node

ESP-IDF app duy nhất của Mức 1: 1 ESP32-S3 có camera chạy Model 1 (FOMO nhận diện người/xe), điều khiển 4 đèn theo zone, đọc BH1750 (ngày/đêm) và 4 INA219 (điện năng từng bóng), gửi số liệu và ảnh cảnh báo lên MQTT cloud cho dashboard.
Khởi tạo bằng `idf.py create-project` khi bắt đầu code.
