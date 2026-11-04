# xiaozhi-es3c28p-vn

Firmware chatbot AI Xiaozhi **tiếng Việt** cho mạch **ESP32-S3 màn hình 2.8" cảm ứng**
(ES3C28P / ES3N28P, Freenove FNK0104 và các bản cùng thiết kế), có thêm
**dẫn đường từ Google Maps trên điện thoại**.

## Tính năng

- Trò chuyện AI bằng giọng nói, icon cảm xúc
- Nghe nhạc Việt (server nhạc), radio VOV, nhạc từ thẻ SD, lời bài hát, phổ nhạc
- Đồng hồ + thời tiết khi rảnh (OpenWeatherMap, tự xác định thành phố theo IP)
- Báo thức
- Cảm ứng
- **Dẫn đường**: điện thoại Android chạy Google Maps → app Chronos gửi qua Bluetooth →
  màn hình hiện mũi tên rẽ, khoảng cách, tên đường, thời gian còn lại, giờ đến.
  Xem [main/features/navigation/README.md](main/features/navigation/README.md).

## Phần cứng

| Thành phần | Thông số |
|---|---|
| Chip | ESP32-S3 N16R8 (16MB Flash, 8MB PSRAM) |
| Màn hình | 2.8" ILI9341 240x320, SPI (CS 10, MOSI 11, SCK 12, DC 46, BL 45) |
| Cảm ứng | FT6336/FT6236, I2C 0x38 (SCL 15, SDA 16, INT 17) |
| Âm thanh | ES8311 (MCLK 4, BCLK 5, DIN 6, WS 7, DOUT 8, PA 1) |
| Thẻ nhớ | SDMMC 4-bit (38, 40, 39, 41, 48, 47) |

## Nạp firmware trên trình duyệt

👉 **https://khanhdev54.github.io/xiaozhi-es3c28p-vn/** (Chrome/Edge trên máy tính)

Bấm **Kết nối** → chọn cổng COM → **Bắt đầu nạp**. Trang luôn có bản firmware mới nhất,
tự cập nhật sau mỗi lần build. Mã nguồn trang nạp nằm trong thư mục [`web/`](web/).

## Tải firmware

Mỗi lần push lên `main`, GitHub Actions tự build bản
`xiaozhi-ai-iot-vietnam-es3n28p-lcd-2.8-nav` rồi đăng lên trang nạp ở trên:

1. Vào tab **Actions** → chọn lần chạy mới nhất (dấu ✓ xanh).
2. Kéo xuống **Artifacts** → tải về, giải nén lấy `merged-binary.bin`.
3. Nạp ở địa chỉ `0x0` bằng web flasher hoặc:
   ```
   esptool.py --chip esp32s3 -p COMx -b 460800 write_flash 0x0 merged-binary.bin
   ```

Muốn build lại mà không sửa code: **Actions** → *Build ES3C28P 2.8" (VN + Navigation)* → **Run workflow**.

## Tự build trên máy

Cần ESP-IDF 5.4 hoặc 5.5.

```
python scripts/release.py xiaozhi-ai-iot-vietnam-es3n28p-lcd-2.8 --name xiaozhi-ai-iot-vietnam-es3n28p-lcd-2.8-nav
```

## Nguồn gốc & giấy phép

- Dựa trên [Ngthhoang/xiaozhi-vn-ricky0611](https://github.com/Ngthhoang/xiaozhi-vn-ricky0611)
  (commit `0a79f26`), vốn là bản fork của [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32).
  README gốc: [README_ricky.md](README_ricky.md).
- Giao thức dẫn đường theo [fbiego/chronos-esp32](https://github.com/fbiego/chronos-esp32) (MIT, Felix Biego).
- Giấy phép MIT — xem [LICENSE](LICENSE).
