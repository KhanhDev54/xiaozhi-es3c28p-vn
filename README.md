# KenDev Xiaozhi · ES3C28P 2.8" (tiếng Việt)

Firmware trợ lý giọng nói Xiaozhi cho mạch **LCD Wiki ES3C28P** (ESP32-S3 N16R8, màn 2.8" IPS 320×240,
cảm ứng điện dung FT6336G, loa ES8311), giao diện tiếng Việt kiểu ứng dụng.

- Nền: [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) v2.5.3 (giấy phép MIT), cấu hình mạch lấy từ
  board `quandong/esp32-s3-spi-2.8` của bản gốc.
- Bản cũ (dựa trên fork ricky) vẫn giữ ở nhánh [`legacy-ricky`](https://github.com/KhanhDev54/xiaozhi-es3c28p-vn/tree/legacy-ricky).

> Trạng thái: **đợt 1** (đang thử trên phần cứng). Các tính năng VietQR, báo biến động số dư, dẫn đường,
> ảnh, nhạc… sẽ được thêm dần ở các đợt sau.

## 1. Nạp firmware

1. Mở trang nạp: <https://khanhdev54.github.io/xiaozhi-es3c28p-vn/> bằng Chrome/Edge trên máy tính.
2. Cắm mạch bằng cáp USB-C có truyền dữ liệu, bấm **Kết nối**, chọn cổng COM.
3. Chọn **KenDev 2.8" ES3C28P (cảm ứng)**, bật **Xoá toàn bộ flash** (lần đầu), bấm **Bắt đầu nạp**.
4. Lần đầu bật: điện thoại vào Wi-Fi `KenDev-xxxx`, mở `192.168.4.1` để cài mạng (thẻ **Tùy chỉnh**:
   thành phố thời tiết, âm lượng, giờ ban đêm).

Trang nạp còn có: **Xem log của mạch**, **Gỡ liên kết thiết bị** (lấy MAC + soạn email gửi Xiaozhi),
**Sửa lỗi thiết bị** (mạch treo, cổng COM chập chờn).

## 2. Lần đầu: cân chỉnh cảm ứng

Lần đầu khởi động, màn hình hiện **"Cân chỉnh cảm ứng"**: chạm vào vòng tròn vàng ở góc trên trái, rồi góc
dưới phải. Làm lại bất cứ lúc nào: vuốt xuống ở màn hình chính → **Cảm ứng**, hoặc Menu → **Cài đặt**.

## 3. Sử dụng

| Thao tác | Kết quả |
|---|---|
| Nói **"Sophia"** hoặc bấm nút **BOOT** | Bắt đầu / dừng trò chuyện |
| Vuốt **trái/phải** ở màn hình chính | Đổi trang: Đồng hồ + thời tiết ↔ Lịch tháng (âm lịch) |
| Vuốt **lên** | Mở **Menu ứng dụng** |
| Vuốt **xuống** | Mở **Điều khiển**: âm lượng, độ sáng, cân chỉnh cảm ứng, khởi động lại |
| Trong ứng dụng: vuốt **lên** / **xuống** | Về Menu / về màn hình chính |

Khi đang trò chuyện, giao diện trò chuyện hiện lên; nói xong vài giây máy tự về màn hình chính.

### Ứng dụng trong Menu

| Ứng dụng | Nội dung |
|---|---|
| Chat | Bắt đầu trò chuyện |
| FM | Radio VOV1, VOV2, VOV3, VOV5, VOV Giao thông HN/HCM, VOV Mekong; nút Dừng |
| Lịch | Lịch tháng có ngày âm, can chi, giờ hoàng đạo |
| Thời tiết | Hiện tại, cảm giác, độ ẩm, AQI, dự báo 3–4 ngày (Open-Meteo) |
| Báo thức | Danh sách báo thức, hẹn giờ đang chạy, nút tắt chuông |
| Giá vàng | Giá vàng SJC hôm nay (từ website SJC) |
| Hệ thống | Wi-Fi, IP, RAM, thời gian chạy, phiên bản |
| Cài đặt | Hướng dẫn đổi Wi-Fi, cân chỉnh cảm ứng |

### Hỏi bằng giọng nói

Thời tiết, tin tức (VnExpress), giá vàng SJC, tỷ giá Vietcombank, radio VOV, báo thức / hẹn giờ,
âm lịch / can chi / giờ hoàng đạo, chế độ ban đêm.

## 4. Phần cứng

| Thành phần | Chân |
|---|---|
| Màn ILI9341V (SPI) | CS 10, MOSI 11, SCK 12, DC 46, đèn nền 45 |
| Cảm ứng FT6336G (I2C 0x38) | SDA 16, SCL 15, RST 18, INT 17 |
| ES8311 (I2S) | MCLK 4, BCLK 5, DIN 6, WS 7, DOUT 8; I2C chung; PA bật mức thấp GPIO1 |
| LED RGB WS2812 | GPIO42 |
| Nút BOOT | GPIO0 |

## 5. Dành cho người phát triển

```bash
python scripts/build.py es3c28p-kendev --name es3c28p-kendev --language vi-VN --wake-word wn9_sophia_tts
```

| Tệp | Nội dung |
|---|---|
| `main/boards/es3c28p-kendev/es3c28p_kendev_board.cc` | Khởi tạo mạch, cảm ứng (LVGL input), công cụ MCP |
| `main/boards/es3c28p-kendev/launcher.*` | Giao diện: màn hình chính, menu, ứng dụng, điều khiển, cân chỉnh cảm ứng |
| `main/boards/es3c28p-kendev/touch_cal.*`, `ft6336_touch.*` | Driver FT6336 và quy đổi toạ độ |
| `main/boards/es3c28p-kendev/weather_icon.*` | Biểu tượng thời tiết vẽ bằng LVGL |
| `main/boards/es3c28p-kendev/info_services.*` | Thời tiết, tin tức, giá vàng, tỷ giá |
| `third_party/esp-wifi-connect` | Trang cài Wi-Fi tiếng Việt (bản sửa của 78/esp-wifi-connect, MIT) |
| `web/` | Trang nạp firmware (GitHub Pages) |

## 6. Nguồn gốc & giấy phép

MIT, theo bản gốc 78/xiaozhi-esp32 (xem `LICENSE`, `README_upstream.md`). Dữ liệu: Open-Meteo, VnExpress RSS,
website SJC và Vietcombank — chỉ để tham khảo.
