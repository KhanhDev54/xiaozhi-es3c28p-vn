# Ricky Hunonic Robot (XH-S3E-AI_V1.0)

Board robot 839 điều khiển 2 motor DC qua **L298N** (không dùng hồng ngoại). Phần cứng và đấu nối giống **Ricky War Tank**, firmware gait độc lập.

## Phần cứng

| Chức năng | GPIO | Ghi chú |
|-----------|------|---------|
| Motor trái IN1 | 39 | VOL- (L298) |
| Motor trái IN2 | 40 | VOL+ (L298) |
| Motor phải IN1 | 43 | TXD (L298) |
| Motor phải IN2 | 44 | RXD (L298) |
| OLED SDA | 41 | IO41/SDA |
| OLED SCL | 42 | IO42/SCL |
| Boot | 0 | IO0 |

- **Motor**: 2× N20 DC qua driver **L298** (cầu H). Motor A = chân trái, Motor B = chân phải.
- **Màn hình**: SSD1306 0.96" I2C 128×64 — mắt biểu cảm (LVGL).
- **GPIO 43/44**: log serial qua `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG` (USB).

## Chuyển động

| Kiểu | Nguyên lý |
|------|-----------|
| **Bước** | Luân phiên từng chân (không quay cùng lúc) |
| **Trượt** | Hai chân quay cùng lúc |

Giai đoạn 1: Bước tới/lùi/trái/phải và Trượt tới/lùi/trái/phải. Dance / Patrol ghép từ các primitive này.

- **Bước**: `speed` + `step_ms` + `steps` (không dùng duration). STEPS mặc định = **2**.
- **Trượt**: `speed` + `duration` (0 = chạy đến STOP).

## MCP tools

| Tool | Mô tả |
|------|--------|
| `self.hunonic.action` | Web/MCP: `action` + `speed` + `duration` + `steps` + `step_ms` |
| `self.hunonic.stop` | Dừng cả hai motor |
| `self.hunonic.walk_*` | Bước tới / lùi / trái / phải |
| `self.hunonic.slide_*` | Trượt tới / lùi / trái / phải |
| `self.hunonic.dance` | Nhảy múa (chuỗi primitive) |
| `self.hunonic.patrol` | Đi tuần tra |
| `self.hunonic.show_qrcode` | Hiện QR `http://<IP>:8080` trên OLED |
| `self.hunonic.hide_qrcode` | Tắt QR, trả lại mặt |
| `self.hunonic.save_config` | Lưu SPEED / STEP MS / STEPS / DURATION vào NVS |

Nói: **"hiện mã QR"**, **"show QR"**, **"quét mã vào web"** → OLED hiện QR, điện thoại cùng WiFi quét vào panel.

## Web control

Sau khi kết nối WiFi:

```
http://<IP-robot>:8080
```

Nút Bước / Trượt / STOP / Nhảy múa / Tuần tra / QR WEB. **LƯU CẤU HÌNH** ghi SPEED / STEP MS / STEPS / DURATION vào NVS (mở web lại hoặc reset vẫn giữ; chưa từng lưu thì STEPS = 2). Joystick = trượt. Phím W/A/S/D bước, I/J/K/L trượt, Space STOP.

## Build

```bash
idf.py set-target esp32s3
idf.py menuconfig   # Xiaozhi Configuration → Ricky Hunonic Robot
idf.py build flash
```

Hoặc: `python scripts/release.py ricky-hunonic-robot`
