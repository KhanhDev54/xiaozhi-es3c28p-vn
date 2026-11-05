# xiaozhi-es3c28p-vn

Firmware chatbot AI Xiaozhi **tiếng Việt** cho mạch **ESP32-S3 màn hình 2.8" cảm ứng**
(ES3C28P / ES3N28P, Freenove FNK0104 và các bản cùng thiết kế), có thêm
**dẫn đường từ Google Maps trên điện thoại**.

👉 **Trang nạp firmware:** https://khanhdev54.github.io/xiaozhi-es3c28p-vn/

---

## Mục lục

1. [Tính năng](#1-tính-năng)
2. [Chuẩn bị](#2-chuẩn-bị)
3. [Nạp firmware](#3-nạp-firmware)
4. [Cài Wi-Fi lần đầu](#4-cài-wi-fi-lần-đầu)
5. [Kích hoạt trên xiaozhi.me](#5-kích-hoạt-trên-xiaozhime)
6. [Trò chuyện với Xiaozhi](#6-trò-chuyện-với-xiaozhi)
7. [Thao tác chạm & nút bấm](#7-thao-tác-chạm--nút-bấm)
8. [Nghe nhạc & radio](#8-nghe-nhạc--radio)
9. [Đồng hồ & thời tiết](#9-đồng-hồ--thời-tiết)
10. [Dẫn đường từ điện thoại](#10-dẫn-đường-từ-điện-thoại)
11. [Báo chuyển khoản](#11-báo-chuyển-khoản)
12. [QR nhận tiền (VietQR)](#12-qr-nhận-tiền-vietqr)
13. [Báo thức, hẹn giờ, chế độ ban đêm](#13-báo-thức-hẹn-giờ-chế-độ-ban-đêm)
14. [Xử lý sự cố](#14-xử-lý-sự-cố)
15. [Dành cho người phát triển](#15-dành-cho-người-phát-triển)
16. [Nguồn gốc & giấy phép](#16-nguồn-gốc--giấy-phép)

---

## 1. Tính năng

| Tính năng | Ghi chú |
|---|---|
| 🗣️ Trò chuyện AI bằng giọng nói | Qua server xiaozhi.me, trả lời tiếng Việt |
| 😊 Icon cảm xúc | Đổi theo nội dung câu trả lời |
| 🎵 Nhạc Việt, radio VOV | Ra lệnh bằng giọng nói |
| 🕒 Đồng hồ + 🌤️ thời tiết + 📅 âm lịch | Tự hiện khi rảnh; có chỉ số không khí AQI |
| 👆 Cảm ứng | Vuốt chỉnh âm lượng, độ sáng; chạm 1 lần để nói |
| 🧭 Dẫn đường | Google Maps trên Android → app Chronos → màn hình mạch |
| 💰 Báo chuyển khoản | Thông báo app ngân hàng → "ting" + hiện số tiền |
| 🔳 QR nhận tiền | VietQR, nói "hiện QR 50 nghìn" |
| ⏰ Báo thức, hẹn giờ | Ra lệnh bằng giọng nói |
| 🌙 Chế độ ban đêm | Tự giảm độ sáng 22h–6h |

## 2. Chuẩn bị

- Mạch **ESP32-S3 2.8" cảm ứng** loại **16MB Flash + 8MB PSRAM (N16R8)**.
  Bản 4MB/8MB flash **không chạy được** firmware này.
- Cáp **USB-C có truyền dữ liệu** (cáp chỉ sạc sẽ không nhận cổng COM).
- Máy tính dùng **Chrome** hoặc **Edge**.
- Loa gắn vào cổng **SPEAKER** trên mạch.
- (Tuỳ chọn) Điện thoại **Android** cài app **Chronos** nếu muốn dùng dẫn đường
  và báo chuyển khoản.

## 3. Nạp firmware

### Cách 1 — Nạp trên trình duyệt (khuyên dùng)

1. Mở **https://khanhdev54.github.io/xiaozhi-es3c28p-vn/** bằng Chrome/Edge.
2. Cắm mạch vào máy tính.
3. Bấm **Kết nối** → chọn cổng COM của mạch (thường tên *USB JTAG/serial debug unit*
   hoặc *USB Serial*) → **Connect**.
   - Không thấy cổng? **Giữ nút BOOT**, bấm **RESET**, thả BOOT rồi bấm Kết nối lại.
4. Firmware mới nhất đã được chọn sẵn, địa chỉ để nguyên **`0x0`**.
5. **Lần đầu nạp**: tích ô *"Xoá toàn bộ flash trước khi nạp"*.
6. Bấm **Bắt đầu nạp** và chờ thanh tiến trình chạy tới 100% (khoảng 1–3 phút).
7. Mạch tự khởi động lại. Nếu màn hình vẫn đen, bấm **RESET**.

### Cách 2 — Tải file về tự nạp

1. Vào tab **Actions** của repo → chọn lần chạy mới nhất có dấu ✓ xanh.
2. Kéo xuống **Artifacts** → tải về, giải nén lấy `merged-binary.bin`.
3. Nạp ở địa chỉ `0x0`, ví dụ bằng esptool:
   ```
   esptool.py --chip esp32s3 -p COM5 -b 460800 write_flash 0x0 merged-binary.bin
   ```
   (đổi `COM5` thành cổng của bạn). Hoặc kéo thả file vào trang nạp ở Cách 1.

## 4. Cài Wi-Fi lần đầu

1. Sau khi nạp, màn hình hiện hướng dẫn cài mạng kèm **tên Wi-Fi** do mạch phát ra
   (bắt đầu bằng `Xiaozhi`) và **địa chỉ trang cài đặt** (thường là `http://192.168.4.1`).
2. Trên điện thoại/máy tính, kết nối vào Wi-Fi đó.
3. Trang cài đặt thường tự bật lên; nếu không, mở địa chỉ hiện trên màn hình.
4. Chọn Wi-Fi nhà bạn (**chỉ hỗ trợ 2.4GHz**), nhập mật khẩu → lưu.
5. Mạch tự khởi động lại và kết nối mạng.

Muốn **đổi Wi-Fi** sau này: bấm nút **BOOT 5 lần liên tiếp** để vào lại chế độ cài mạng.

## 5. Kích hoạt trên xiaozhi.me

Lần đầu kết nối mạng, màn hình (và loa) sẽ đọc một **mã kích hoạt 6 số**.

1. Vào **https://xiaozhi.me**, đăng nhập (đăng ký bằng số điện thoại/email).
2. Vào **Console / 控制台** → **Thêm thiết bị (添加设备)** → nhập mã 6 số.
3. Trong phần cấu hình **Agent** của thiết bị, bạn có thể:
   - Chọn **ngôn ngữ** trả lời là tiếng Việt và giọng đọc phù hợp.
   - Đặt tên, tính cách, lời giới thiệu cho trợ lý.
4. Bấm RESET mạch để áp dụng.

> Giao diện xiaozhi.me có thể đổi theo thời gian; tên các mục ở trên chỉ mang tính tham khảo.

## 6. Trò chuyện với Xiaozhi

Có 3 cách bắt đầu nói chuyện:

- Nói từ đánh thức **"Sophia"** (đọc *"Sô-phia"*). Nói rõ, bình thường, cách mạch khoảng 1–3 m.
- **Chạm 1 lần** lên màn hình.
- Bấm nút **BOOT** một lần.

Khi trợ lý **đang nói**: chạm 1 lần hoặc bấm BOOT để **ngắt lời**.
Khi trợ lý **đang nghe**: chạm 1 lần hoặc bấm BOOT để **kết thúc** cuộc trò chuyện.

Ví dụ câu lệnh: *"Hôm nay thời tiết thế nào?"*, *"Tăng âm lượng lên 80"*,
*"Giảm độ sáng màn hình"*, *"Kể chuyện cười đi"*.

## 7. Thao tác chạm & nút bấm

| Thao tác | Chức năng |
|---|---|
| Chạm 1 lần | Bắt đầu / dừng trò chuyện |
| Vuốt **lên** | Tăng âm lượng (+5) |
| Vuốt **xuống** | Giảm âm lượng (−5) |
| Vuốt **phải** | Tăng độ sáng (+5) |
| Vuốt **trái** | Giảm độ sáng (−5) |
| Nhấn giữ khi đang phát nhạc | Tắt tiếng |
| Nút **BOOT** 1 lần | Bắt đầu / dừng trò chuyện |
| Nút **BOOT** 5 lần | Vào chế độ cài lại Wi-Fi |
| Nút **RESET** | Khởi động lại mạch |

> **Màn hình không nhận cảm ứng?** Khoảng 10 giây sau khi bật, máy hiện một thông báo:
> "Cảm ứng: OK" nghĩa là đã thấy chip cảm ứng; "Không thấy chip cảm ứng (I2C 0x38)" nghĩa là
> board không có chip cảm ứng (bản ES3N28P thường là bản **không** cảm ứng, bản có cảm ứng là ES3C28P)
> hoặc cáp cảm ứng chưa tiếp xúc. Khi đó vẫn dùng được bằng giọng nói và nút BOOT.


## 8. Nghe nhạc & radio

Ra lệnh bằng giọng nói, ví dụ:

- *"Mở bài Lạc Trôi"*, *"Phát nhạc Sơn Tùng"*
- *"Mở radio VOV1"*, *"Nghe VOV Giao thông Hà Nội"* (có VOV1, VOV2, VOV3, VOV5, VOV Giao thông Hà Nội / TP.HCM…)
- *"Dừng nhạc"*, *"Bài tiếp theo"*

Khi phát nhạc, màn hình hiện phổ nhạc và lời bài hát (nếu có).

> **Nhạc từ thẻ SD** chưa được bật trong bản build hiện tại.

## 9. Đồng hồ & thời tiết

Khi không trò chuyện, màn hình tự chuyển sang **đồng hồ + thời tiết**:

- Giờ (đồng hồ lật), thứ, ngày dương lịch **và âm lịch** (ví dụ `Thứ Tư 04/11/2026 · 26/9 ÂL`).
- Thành phố, nhiệt độ, mô tả (*Mưa rào*, *Nhiều mây*…), nhiệt độ thấp/cao trong ngày.
- **Chất lượng không khí AQI** kèm mức: Tốt (xanh), Trung bình (vàng), Kém (cam), Xấu (đỏ)…

Ghi chú:

- Dữ liệu thời tiết lấy từ [Open-Meteo](https://open-meteo.com) — **miễn phí, không cần key**.
- Thành phố được **tự xác định theo địa chỉ IP** mạng nhà bạn; cập nhật 30 phút/lần.
- Âm lịch tính ngay trên mạch (không cần mạng), theo múi giờ Việt Nam.
- Hỏi trợ lý *"Hôm nay âm lịch ngày mấy?"*, *"Giờ hoàng đạo hôm nay?"* thì trợ lý trả lời bằng giọng.

## 10. Dẫn đường từ điện thoại

Điện thoại Android chạy Google Maps → app **Chronos** đọc chỉ dẫn → gửi qua Bluetooth
→ màn hình hiện **mũi tên rẽ, khoảng cách, tên đường, thời gian còn lại, giờ đến**.
Mỗi khi có chỉ dẫn mới, loa kêu "bíp".

### Cài đặt (làm 1 lần)

1. Cài app **Chronos** trên điện thoại Android (Google Play).
2. Bật **Bluetooth** trên điện thoại.
3. Mở Chronos → thêm thiết bị → chọn **`Xiaozhi-Nav`**.
   Màn hình mạch hiện *"Đã kết nối điện thoại"*.
4. Trong Chronos, cấp quyền **đọc thông báo** và bật tính năng **dẫn đường / Navigation**.

### Sử dụng

1. Mở **Google Maps**, chọn điểm đến → **Bắt đầu**.
2. Màn hình mạch tự chuyển sang trang dẫn đường.
3. Kết thúc chỉ đường → màn hình quay về giao diện chat / đồng hồ.

> - Chỉ hỗ trợ **Android**; iPhone chưa được kiểm chứng.
> - Mạch **không có GPS** — vị trí lấy từ điện thoại, nên điện thoại phải đi cùng mạch.
> - Tên các mục trong app Chronos có thể khác đôi chút tuỳ phiên bản.

Chi tiết kỹ thuật: [main/features/navigation/README.md](main/features/navigation/README.md).

## 11. Báo chuyển khoản

Khi có tiền vào tài khoản, loa kêu **"ting"** và màn hình hiện số tiền
(ví dụ `+150.000đ · Tien nha thang 11`) trong 10 giây — kể cả lúc đang ở chế độ ban đêm.

### Cài đặt

1. Làm phần **Cài đặt** của [mục 10](#10-dẫn-đường-từ-điện-thoại) (kết nối Chronos với `Xiaozhi-Nav`).
2. Trong Chronos, phần chọn **ứng dụng được chuyển thông báo**, bật **app ngân hàng** của bạn
   (Vietcombank, MB, Techcombank, MoMo…).
3. Trong app ngân hàng, bật **thông báo biến động số dư** trên điện thoại.

### Sử dụng

- Tự động: có tiền vào là kêu và hiện.
- Hỏi: *"Hôm nay mình nhận được bao nhiêu tiền?"*, *"Giao dịch gần nhất là gì?"*.
- Tắt / bật: *"Tắt báo chuyển khoản"*, *"Báo cả tiền ra nữa"*.

> - Hoạt động khi **điện thoại ở gần mạch** (Bluetooth) và thông báo hiện trên điện thoại.
> - Mạch đọc số tiền từ nội dung thông báo; mỗi ngân hàng viết một kiểu nên có thể
>   có app chưa nhận đúng — gửi mẫu thông báo (che số tài khoản, số dư) qua Issues để bổ sung.
> - Lịch sử chỉ lưu **20 giao dịch trong RAM**, mất khi tắt máy; **không lưu số dư**.

## 12. QR nhận tiền (VietQR)

Hiện mã **VietQR** trên màn hình để người khác quét bằng app ngân hàng bất kỳ.

1. Lưu tài khoản (làm 1 lần), nói: *"Lưu tài khoản nhận tiền Vietcombank số 0123456789,
   tên NGUYEN VAN A"*. Thông tin lưu trên mạch, không đưa lên GitHub.
2. Hiện QR: *"Hiện QR nhận tiền"* (người chuyển tự nhập số tiền) hoặc
   *"Cho mình QR 50 nghìn, nội dung tien an trua"*.
3. QR tự ẩn khi bạn bắt đầu nói chuyện lại.

Ngân hàng hỗ trợ theo tên: Vietcombank, VietinBank, BIDV, Agribank, MB, Techcombank, ACB,
VPBank, TPBank, Sacombank, VIB, SHB, HDBank, OCB, MSB, SeABank, Eximbank, LPBank, Nam A Bank,
Bac A Bank, ABBANK, Kienlongbank, PVcomBank. Ngân hàng khác: đọc **mã BIN 6 số** của ngân hàng.

> Hãy quét thử bằng app ngân hàng và kiểm tra **tên người nhận** trước khi dùng thật.

## 13. Báo thức, hẹn giờ, chế độ ban đêm

| Câu lệnh mẫu | Kết quả |
|---|---|
| *"Đặt báo thức 6 giờ 30 sáng"* | Báo thức một lần |
| *"Báo thức 7 giờ mỗi ngày, nhãn đi làm"* | Báo thức lặp hằng ngày |
| *"Mình có những báo thức nào?"* | Liệt kê báo thức |
| *"Xoá báo thức thứ nhất"* / *"Xoá hết báo thức"* | Xoá |
| *"Hẹn giờ 10 phút"*, *"Hẹn giờ 90 giây luộc trứng"* | Đếm ngược |
| *"Tắt báo thức"* | Tắt chuông đang kêu |
| *"Bật chế độ ban đêm từ 23 giờ đến 6 giờ"* | Đổi khung giờ ban đêm |

- Chuông kêu tối đa **1 phút**; chạm 1 lần / bấm BOOT / bắt đầu nói chuyện để tắt.
- Báo thức lưu trong bộ nhớ mạch, **không mất khi tắt nguồn** (tối đa 8 báo thức).
- **Chế độ ban đêm** (mặc định bật, 22h–6h): khi rảnh màn hình tự giảm còn 5% độ sáng;
  khi trò chuyện, phát nhạc, báo thức hoặc có tiền vào thì sáng lại.
- Cần mạch đã có giờ chuẩn (đã kết nối mạng ít nhất một lần sau khi bật).

## 14. Xử lý sự cố

| Hiện tượng | Cách xử lý |
|---|---|
| Trang nạp không thấy cổng COM | Đổi cáp USB có truyền dữ liệu; giữ BOOT + bấm RESET; cài driver CH340/CP210x nếu mạch dùng chip đó |
| Nạp báo lỗi giữa chừng | Giảm **Tốc độ** xuống 115200, thử lại |
| Màn hình đen sau khi nạp | Bấm RESET; kiểm tra mạch đúng loại 16MB |
| Khởi động lại liên tục | Nạp lại với ô *Xoá toàn bộ flash* được tích |
| Không cài được Wi-Fi | Wi-Fi nhà phải là **2.4GHz**; bấm BOOT 5 lần để cài lại |
| Không nghe tiếng | Kiểm tra loa cắm cổng SPEAKER; vuốt lên để tăng âm lượng |
| Chronos không thấy `Xiaozhi-Nav` | Bật Bluetooth + quyền vị trí cho Chronos; RESET mạch rồi tìm lại |
| Thời tiết không hiện | Chờ ~1 phút sau khi có mạng; kiểm tra mạng ra được Internet |
| Không báo chuyển khoản | Kiểm tra Chronos đang kết nối và đã chọn app ngân hàng để chuyển thông báo |
| Báo thức không kêu | Mạch phải có giờ chuẩn (đã kết nối mạng); hỏi *"Mình có báo thức nào?"* để kiểm tra |

Nếu vẫn lỗi, mở **Issues** của repo và đính kèm log (trang nạp có khung Log, hoặc chạy
`idf.py monitor`).

## 15. Dành cho người phát triển

### Build tự động (GitHub Actions)

Mỗi lần push lên `main` (trừ khi chỉ sửa file `.md`), workflow
[`build-es3c28p-nav.yml`](.github/workflows/build-es3c28p-nav.yml) sẽ:

1. Build bản `xiaozhi-ai-iot-vietnam-es3n28p-lcd-2.8-nav` bằng ESP-IDF 5.5.
2. Lưu file `.bin` vào **Artifacts** (giữ 30 ngày).
3. Đăng trang nạp + firmware mới nhất lên **GitHub Pages**.

Build lại thủ công: **Actions → Build ES3C28P 2.8" (VN + Navigation) → Run workflow**.

### Build trên máy

Cần ESP-IDF 5.4 hoặc 5.5.

```
python scripts/release.py xiaozhi-ai-iot-vietnam-es3n28p-lcd-2.8 --name xiaozhi-ai-iot-vietnam-es3n28p-lcd-2.8-nav
idf.py -p COM5 flash monitor
```

### Đổi từ đánh thức

Từ đánh thức được nhận dạng **offline trên chip** bằng mô hình WakeNet của
[ESP-SR](https://github.com/espressif/esp-sr). Espressif chưa có mô hình tiếng Việt,
nên bản này dùng từ tiếng Anh **"Sophia"**. Muốn đổi, sửa 2 dòng trong
`main/boards/xiaozhi-ai-iot-vietnam-es3n28p-lcd-2.8/config.json` (bản `-nav`):

```
"CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS=n",
"CONFIG_SR_WN_WN9_SOPHIA_TTS=y"
```

Thay `SOPHIA_TTS` bằng mô hình khác, ví dụ `ALEXA`, `JARVIS_TTS`, `HIESP`
(xem danh sách trong `Kconfig.projbuild` của esp-sr).

### Các file quan trọng

| File | Nội dung |
|---|---|
| `main/boards/xiaozhi-ai-iot-vietnam-es3n28p-lcd-2.8/config.h` | Chân GPIO, xoay màn hình (`LCD_ROTATION_DEGREE`) |
| `main/boards/xiaozhi-ai-iot-vietnam-es3n28p-lcd-2.8/config.json` | Các bản build và tuỳ chọn sdkconfig |
| `main/features/navigation/` | Bluetooth (Chronos) + giao diện dẫn đường |
| `main/features/weather/` | Thời tiết Open-Meteo + AQI, giao diện màn hình chờ |
| `main/features/lunar/` | Âm lịch, can chi, giờ hoàng đạo |
| `main/features/vietqr/` | Tạo mã VietQR + lệnh giọng nói |
| `main/features/alarm_clock/` | Báo thức, hẹn giờ, chế độ ban đêm |
| `main/features/bank_alert/` | Đọc thông báo ngân hàng, báo chuyển khoản |
| `partitions/v2/16m_single_app.csv` | Bảng phân vùng: 1 khe app 8MB + 8MB assets |
| `web/` | Trang nạp firmware |

> **Không** đưa mật khẩu Wi-Fi hay API key riêng lên repo công khai.

### Sơ đồ chân

| Thành phần | Chân |
|---|---|
| Chip | ESP32-S3 N16R8 (16MB Flash, 8MB PSRAM) |
| Màn hình ILI9341 240x320 (SPI) | CS 10, MOSI 11, SCK 12, MISO 13, DC 46, BL 45 |
| Cảm ứng FT6236/FT6336 (I2C 0x38) | SCL 15, SDA 16, INT 17, RST 18 |
| Âm thanh ES8311 | MCLK 4, BCLK 5, DIN 6, WS 7, DOUT 8, PA 1 |
| Thẻ nhớ SDMMC 4-bit | CLK 38, CMD 40, D0 39, D1 41, D2 48, D3 47 |
| Nút BOOT | GPIO 0 |

## 16. Nguồn gốc & giấy phép

- Dựa trên [Ngthhoang/xiaozhi-vn-ricky0611](https://github.com/Ngthhoang/xiaozhi-vn-ricky0611)
  (commit `0a79f26`), vốn là bản fork của [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32).
  README gốc: [README_ricky.md](README_ricky.md).
- Giao thức dẫn đường theo [fbiego/chronos-esp32](https://github.com/fbiego/chronos-esp32) (MIT, Felix Biego).
- Trang nạp dùng [esptool-js](https://github.com/espressif/esptool-js) của Espressif.
- Giấy phép MIT — xem [LICENSE](LICENSE).
