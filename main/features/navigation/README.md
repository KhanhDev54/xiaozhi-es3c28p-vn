# Dẫn đường từ điện thoại (Google Maps → màn hình Xiaozhi)

Điện thoại Android chạy Google Maps (hoặc OsmAnd). App **Chronos** đọc thông báo
dẫn đường rồi gửi qua Bluetooth BLE sang mạch. Màn hình hiện mũi tên rẽ, khoảng
cách tới chỗ rẽ, tên đường, thời gian còn lại và giờ đến. Mỗi khi có chỉ dẫn mới,
loa kêu "bíp".

Giao thức lấy theo thư viện [ChronosESP32](https://github.com/fbiego/chronos-esp32)
(MIT License, Felix Biego), viết lại cho ESP-IDF + NimBLE.

## Build

```
python scripts/release.py xiaozhi-ai-iot-vietnam-es3n28p-lcd-2.8 --name xiaozhi-ai-iot-vietnam-es3n28p-lcd-2.8-nav
```

Bản `-nav` bật thêm: cảm ứng, thời tiết khi rảnh, Bluetooth NimBLE và dẫn đường.

## Dùng

1. Cài app **Chronos** (Android) từ Google Play.
2. Bật Bluetooth, mở Chronos → thêm thiết bị → chọn **Xiaozhi-Nav**.
3. Trong Chronos: cấp quyền đọc thông báo, bật mục **Navigation**.
4. Mở Google Maps, bắt đầu dẫn đường → màn hình mạch tự chuyển sang trang dẫn đường.
5. Kết thúc dẫn đường → màn hình quay lại giao diện chat/thời tiết.

## Tùy chỉnh

- Tên Bluetooth: `CONFIG_PHONE_NAVIGATION_BLE_NAME` (menuconfig → Xiaozhi Assistant).
- Giao diện: `nav_ui.cc` (màu, vị trí, cỡ chữ).

## Giới hạn

- Chỉ dẫn đường qua app Chronos trên Android; iPhone chưa kiểm chứng.
- Mạch không có GPS: mọi dữ liệu vị trí đến từ điện thoại.
