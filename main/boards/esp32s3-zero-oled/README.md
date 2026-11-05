# ESP32-S3 Zero + OLED (INMP441 + MAX98357A)

Board cho Waveshare ESP32-S3 Zero (4MB Flash / 2MB PSRAM) đi dây theo sơ đồ
"ChatBot AI (Xiaozhi) mod by tandev".

## Sơ đồ chân

| Thiết bị | Chân thiết bị | ESP32-S3 |
|---|---|---|
| OLED SSD1306/SH1106 | VCC / GND / SCL / SDA | 3V3 / GND / GPIO9 / GPIO8 |
| MAX98357A | DIN / BCLK / LRCLK | GPIO5 / GPIO6 / GPIO7 |
| MAX98357A | VIN / GAIN / SD / GND | 5V / GND / 5V / GND |
| INMP441 | WS / SCK / SD | GPIO2 / GPIO3 / GPIO4 |
| INMP441 | VDD / GND / L/R | 3V3 / GND / GND |
| Nút đánh thức | | GPIO1 (nối GND khi nhấn) |
| Nút tăng âm lượng | | GPIO10 (nối GND khi nhấn) |
| Nút giảm âm lượng | | GPIO11 (nối GND khi nhấn) |
| LED / Đèn ngoài | | GPIO12 (điều khiển bằng giọng nói qua MCP lamp) |
| LED RGB WS2812 trên bo | | GPIO21 (LED trạng thái) |

## Các biến thể build

| Tên | Màn hình |
|---|---|
| `esp32s3-zero-oled-096` | SSD1306 128x64 (0.96") |
| `esp32s3-zero-oled-091` | SSD1306 128x32 (0.91") |
| `esp32s3-zero-oled-13-sh1106` | SH1106 128x64 (1.3") |

Tất cả: flash 4MB, bảng phân vùng `partitions/v2/4m.csv`, PSRAM Quad.

## Build

Cần ESP-IDF v6.0.1 trở lên (khuyến nghị v6.1).

```bash
. $HOME/esp/esp-idf/export.sh
python scripts/build.py esp32s3-zero-oled --name esp32s3-zero-oled-096 --language vi-VN
idf.py -p COMx flash monitor
```

## Ghi chú

- Đèn LED đỏ ở GPIO12 là đèn "Lamp": nó bật/tắt theo lệnh giọng nói, không
  phải LED trạng thái. Nên mắc nối tiếp điện trở ~220–330Ω.
- Nếu bo của bạn không có LED RGB ở GPIO21, đổi `BUILTIN_LED_GPIO` thành
  `GPIO_NUM_NC` trong `config.h`.
- Nếu màn hình bị lật ngược, đổi `DISPLAY_MIRROR_X` / `DISPLAY_MIRROR_Y`.
