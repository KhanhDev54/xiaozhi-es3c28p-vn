#include "phone_nav_ble.h"

#ifdef CONFIG_PHONE_NAVIGATION_ENABLE

#include <algorithm>
#include <cstring>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define TAG "PhoneNavBle"

// Nordic UART style service used by the Chronos app
// 6e400001-b5a3-f393-e0a9-e50e24dcca9e (service)
// 6e400002-...  RX (phone -> device, write)
// 6e400003-...  TX (device -> phone, notify)
static const ble_uuid128_t kServiceUuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
static const ble_uuid128_t kRxUuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
static const ble_uuid128_t kTxUuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

static uint16_t s_tx_val_handle = 0;
static uint8_t s_own_addr_type = 0;
static std::string s_device_name;
static ble_gatt_chr_def s_chrs[3];
static ble_gatt_svc_def s_svcs[2];

static void Advertise();

static int GattAccess(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt,
                      void* arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t buf[520];
        uint16_t out_len = 0;
        if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &out_len) == 0 && out_len > 0) {
            PhoneNavBle::GetInstance().OnWrite(buf, out_len);
        }
        return 0;
    }
    return 0;
}

static int GapEvent(struct ble_gap_event* event, void* arg) {
    auto& nav = PhoneNavBle::GetInstance();
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                ESP_LOGI(TAG, "Phone connected");
                nav.OnConnect(event->connect.conn_handle);
            } else {
                Advertise();
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Phone disconnected, reason=%d", event->disconnect.reason);
            nav.OnDisconnect();
            Advertise();
            break;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            Advertise();
            break;
        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == s_tx_val_handle) {
                nav.OnSubscribe(event->subscribe.cur_notify != 0);
            }
            break;
        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "MTU updated: %d", event->mtu.value);
            break;
        default:
            break;
    }
    return 0;
}

static void Advertise() {
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = &kServiceUuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields failed: %d", rc);
        return;
    }

    struct ble_hs_adv_fields rsp;
    memset(&rsp, 0, sizeof(rsp));
    rsp.name = (const uint8_t*)s_device_name.c_str();
    rsp.name_len = s_device_name.size();
    rsp.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_rsp_set_fields failed: %d", rc);
        return;
    }

    struct ble_gap_adv_params params;
    memset(&params, 0, sizeof(params));
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, nullptr, BLE_HS_FOREVER, &params, GapEvent, nullptr);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "adv_start failed: %d", rc);
    }
}

static void OnSync() {
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &s_own_addr_type);
    Advertise();
    ESP_LOGI(TAG, "Advertising as \"%s\"", s_device_name.c_str());
}

static void OnReset(int reason) {
    ESP_LOGW(TAG, "NimBLE host reset, reason=%d", reason);
}

static void HostTask(void* param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

bool PhoneNavBle::Start(const std::string& device_name, Callback callback) {
    if (started_) {
        return true;
    }
    callback_ = std::move(callback);
    s_device_name = device_name;

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
        return false;
    }

    ble_hs_cfg.sync_cb = OnSync;
    ble_hs_cfg.reset_cb = OnReset;

    memset(s_chrs, 0, sizeof(s_chrs));
    s_chrs[0].uuid = &kRxUuid.u;
    s_chrs[0].access_cb = GattAccess;
    s_chrs[0].flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP;
    s_chrs[1].uuid = &kTxUuid.u;
    s_chrs[1].access_cb = GattAccess;
    s_chrs[1].flags = BLE_GATT_CHR_F_NOTIFY;
    s_chrs[1].val_handle = &s_tx_val_handle;

    memset(s_svcs, 0, sizeof(s_svcs));
    s_svcs[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
    s_svcs[0].uuid = &kServiceUuid.u;
    s_svcs[0].characteristics = s_chrs;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(s_svcs);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(s_svcs);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT setup failed: %d", rc);
        return false;
    }
    ble_svc_gap_device_name_set(s_device_name.c_str());

    esp_timer_create_args_t timer_args = {};
    timer_args.callback = [](void*) { PhoneNavBle::GetInstance().SendInfo(); };
    timer_args.dispatch_method = ESP_TIMER_TASK;
    timer_args.name = "nav_info";
    esp_timer_handle_t timer = nullptr;
    if (esp_timer_create(&timer_args, &timer) == ESP_OK) {
        info_timer_ = timer;
    }

    nimble_port_freertos_init(HostTask);
    started_ = true;
    return true;
}

PhoneNavState PhoneNavBle::GetState() {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void PhoneNavBle::OnConnect(uint16_t conn_handle) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        conn_handle_ = conn_handle;
        state_.connected = true;
        rx_length_ = 0;
    }
    Notify(Event::kConnection);
}

void PhoneNavBle::OnDisconnect() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        conn_handle_ = 0xFFFF;
        state_.connected = false;
        state_.active = false;
        chunked_ = false;
    }
    if (info_timer_) {
        esp_timer_stop((esp_timer_handle_t)info_timer_);
    }
    Notify(Event::kConnection);
}

void PhoneNavBle::OnSubscribe(bool subscribed) {
    // The app expects a device info packet shortly after subscribing (3s in Chronos lib)
    if (subscribed && info_timer_) {
        esp_timer_stop((esp_timer_handle_t)info_timer_);
        esp_timer_start_once((esp_timer_handle_t)info_timer_, 3 * 1000 * 1000);
    }
}

void PhoneNavBle::SendInfo() {
    // Same layout as ChronosESP32::sendInfo(); library version 1.9.1, screen config 0 (default)
    const uint8_t info[] = {0xab, 0x00, 0x11, 0xff, 0x92, 0xc0, 1, 91, 0x00, 0xfb,
                            0x1e, 0x40, 0xc0, 0x0e, 0x32, 0x28, 0x00, 0xe2, 0x00, 0x80};
    SendCommand(info, sizeof(info));

    // Same handshake order as ChronosESP32: info -> battery -> battery notify -> sync request
    const uint8_t battery[] = {0xAB, 0x00, 0x05, 0xFF, 0x91, 0x80, 0x00, 100};
    SendCommand(battery, sizeof(battery));
    const uint8_t battery_notify[] = {0xAB, 0x00, 0x04, 0xFE, 0x91, 0x80, 0x00};
    SendCommand(battery_notify, sizeof(battery_notify));
    const uint8_t sync[] = {0xAB, 0x00, 0x03, 0xFE, 0x23, 0x80};
    SendCommand(sync, sizeof(sync));
}

void PhoneNavBle::SendCommand(const uint8_t* data, size_t len, bool force_chunked) {
    uint16_t conn;
    bool chunked;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        conn = conn_handle_;
        chunked = chunked_;
    }
    if (conn == 0xFFFF || s_tx_val_handle == 0) {
        return;
    }
    auto send = [conn](const uint8_t* d, size_t l) {
        struct os_mbuf* om = ble_hs_mbuf_from_flat(d, l);
        if (om) {
            ble_gatts_notify_custom(conn, s_tx_val_handle, om);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    };

    if ((len <= 20 || !chunked) && !force_chunked) {
        send(data, len);
        return;
    }
    send(data, 20);
    uint8_t chunk[20];
    size_t offset = 20;
    uint8_t seq = 0;
    while (offset < len) {
        size_t n = std::min<size_t>(19, len - offset);
        chunk[0] = seq++;
        memcpy(chunk + 1, data + offset, n);
        send(chunk, n + 1);
        offset += n;
    }
}

void PhoneNavBle::OnWrite(const uint8_t* data, size_t len) {
    bool complete = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (len >= 4 && (data[0] == 0xAB || data[0] == 0xEA) && (data[3] == 0xFE || data[3] == 0xFF)) {
            // First packet: total length is in bytes 1-2
            rx_length_ = data[1] * 256 + data[2] + 3;
            if (rx_length_ > kBufferSize) {
                ESP_LOGW(TAG, "Packet too large: %u", (unsigned)rx_length_);
                rx_length_ = 0;
                return;
            }
            memcpy(rx_buffer_, data, std::min(len, kBufferSize));
            complete = rx_length_ <= len;
        } else if (rx_length_ > 0) {
            // Continuation packet: byte 0 is sequence number
            size_t pos = 20 + data[0] * 19;
            if (pos + len - 1 > kBufferSize) {
                rx_length_ = 0;
                return;
            }
            memcpy(rx_buffer_ + pos, data + 1, len - 1);
            complete = rx_length_ <= pos + len - 1;
        }
    }
    if (complete) {
        DataReceived();
    }
}

std::string PhoneNavBle::ReadCString(const uint8_t* data, size_t len, size_t& pos) {
    std::string out;
    while (pos < len && data[pos] != 0) {
        out += (char)data[pos++];
    }
    pos++;  // skip terminator
    return out;
}

void PhoneNavBle::DataReceived() {
    std::unique_lock<std::mutex> lock(mutex_);
    const uint8_t* d = rx_buffer_;
    size_t len = rx_length_;
    rx_length_ = 0;
    if (len < 6 || d[0] != 0xAB) {
        return;
    }

    switch (d[4]) {
        case 0xCA:  // app info -> reply with a short device description
            if (d[3] == 0xFE) {
                lock.unlock();
                const char* text = "Xiaozhi ES3N28P\nESP32-S3 + NimBLE";
                uint8_t out[64];
                size_t tlen = strlen(text);
                out[0] = 0xAB;
                out[1] = (uint8_t)((tlen + 3) >> 8);
                out[2] = (uint8_t)(tlen + 3);
                out[3] = 0xFE;
                out[4] = 0x92;
                out[5] = 0x80;
                memcpy(out + 6, text, tlen);
                SendCommand(out, 6 + tlen, true);
                return;
            }
            break;
        case 0xCC:  // chunked transfer setting
            if (d[3] == 0xFE) {
                chunked_ = d[5] != 0x00;
            }
            break;
        case 0xEE:  // navigation icon, sent in 3 parts of 96 bytes
            if (d[3] == 0xFE && len >= 11 + 96) {
                uint8_t part = d[6];
                if (part < 3) {
                    memcpy(state_.icon + 96 * part, d + 11, 96);
                    icon_parts_ |= (1 << part);
                }
                if (part == 2) {
                    icon_parts_ = 0;
                    lock.unlock();
                    Notify(Event::kNavIcon);
                    return;
                }
            }
            break;
        case 0xEF:  // navigation data
            if (d[3] == 0xFE) {
                if (d[5] == 0x00) {
                    state_.active = false;
                    state_.is_navigation = false;
                    state_.has_icon = false;
                    state_.title.clear();
                    state_.directions.clear();
                    state_.duration.clear();
                    state_.distance.clear();
                    state_.eta.clear();
                    state_.speed.clear();
                    state_.icon_crc = 0xFFFFFFFF;
                } else if (d[5] == 0xFF) {
                    state_.active = true;
                    state_.is_navigation = false;
                    state_.has_icon = false;
                    state_.title = "Chronos";
                    state_.directions = "Hãy bật Dẫn đường trong cài đặt app Chronos";
                    state_.duration.clear();
                    state_.distance.clear();
                    state_.eta.clear();
                    state_.speed.clear();
                } else if (d[5] == 0x80 && len > 12) {
                    state_.active = true;
                    state_.has_icon = d[6] == 1;
                    state_.is_navigation = d[7] == 1;
                    state_.icon_crc = (uint32_t(d[8]) << 24) | (uint32_t(d[9]) << 16) |
                                      (uint32_t(d[10]) << 8) | uint32_t(d[11]);
                    size_t pos = 12;
                    state_.title = ReadCString(d, len, pos);
                    state_.duration = ReadCString(d, len, pos);
                    state_.distance = ReadCString(d, len, pos);
                    state_.eta = ReadCString(d, len, pos);
                    state_.directions = ReadCString(d, len, pos);
                    state_.speed = ReadCString(d, len, pos);
                }
                lock.unlock();
                Notify(Event::kNavData);
                return;
            }
            break;
        default:
            break;
    }
}

void PhoneNavBle::Notify(Event event) {
    if (!callback_) {
        return;
    }
    PhoneNavState copy = GetState();
    callback_(event, copy);
}

#endif  // CONFIG_PHONE_NAVIGATION_ENABLE
