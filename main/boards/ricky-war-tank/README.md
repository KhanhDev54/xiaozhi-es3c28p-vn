# Ricky War Tank (XH-S3E-AI_V1.0)

Board xe tank điều khiển bằng giọng nói / MCP, dựa trên mạch **XH-S3E-AI_V1.0**.

## Phần cứng

| Chức năng | GPIO | Ghi chú |
|-----------|------|---------|
| Motor A trái IN1 | 39 | VOL- (L298) |
| Motor A trái IN2 | 40 | VOL+ (L298) |
| Motor B phải IN1 | 43 | TXD (L298) |
| Motor B phải IN2 | 44 | RXD (L298) |
| OLED SDA | 41 | IO41/SDA |
| OLED SCL | 42 | IO42/SCL |
| Boot | 0 | IO0 |

- **Motor**: 2× N20 DC qua driver **L298** (cầu H).
- **Màn hình**: SSD1306 0.96" I2C 128×64 — mắt biểu cảm (LVGL).
- **Âm thanh**: mặc định **tắt**. Bật bằng `self.tank.set_sound` / `sound_on` / "bật âm thanh" — chỉ phát tiếng máy khi xe đang chạy.

## MCP tools

| Tool | Mô tả |
|------|--------|
| `self.tank.action` | Web/MCP: `action` + `speed` + `duration` (0 = giữ đến khi stop) |
| `self.tank.stop` | Dừng cả hai motor |
| `self.tank.forward` | Tiến thẳng |
| `self.tank.backward` | Lùi thẳng |
| `self.tank.turn_left` | Rẽ trái (bánh trái dừng) |
| `self.tank.turn_right` | Rẽ phải (bánh phải dừng) |
| `self.tank.spin_left` | Xoay trái tại chỗ |
| `self.tank.spin_right` | Xoay phải tại chỗ |
| `self.tank.dance` | Nhảy múa (chuỗi tiến/xoay/lùi) |
| `self.tank.patrol` | Tuần tra (tiến + xoay phải, lặp) |
| `self.tank.set_sound` | Bật/tắt tiếng máy khi chạy |

Tham số tool riêng: `pace` (normal/slow/fast/custom), `duration_ms` (0–60000), `speed` (30–100, khi pace=custom).

## Web control

Sau khi kết nối WiFi, mở trình duyệt:

```
http://<IP-xe>:8081
```

HTTP thuần (không WebSocket) để tránh hết socket LWIP làm nghẽn MQTT/nhạc. Nút: tiến / lùi / rẽ / xoay / nhảy / tuần tra / STOP / bật-tắt âm.

| Endpoint | Việc |
|----------|------|
| `GET /` | Panel HTML |
| `GET /api/status` | IP + `sound` |
| `POST /api/action` | `{action, speed, duration}` |

## Lưu ý

- **GPIO 43/44**: log serial qua `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG` (USB), không dùng UART0.
- **GPIO 39/40**: trên PCB là nút volume; firmware dùng PWM LEDC cho L298.

## Build

```bash
idf.py set-target esp32s3
idf.py menuconfig   # Xiaozhi Configuration → Ricky War Tank
idf.py build flash
```

Hoặc: `python scripts/release.py ricky-war-tank`
