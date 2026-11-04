#ifndef PHONE_NAV_BLE_H
#define PHONE_NAV_BLE_H

// Receive turn-by-turn navigation from a phone over BLE.
//
// Implements the navigation subset of the Chronos watch protocol
// (https://github.com/fbiego/chronos-esp32, MIT License), so the Android app
// "Chronos" can forward Google Maps / OsmAnd navigation notifications.

#include <sdkconfig.h>

#ifdef CONFIG_PHONE_NAVIGATION_ENABLE

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#define PHONE_NAV_ICON_SIZE 48
#define PHONE_NAV_ICON_BYTES ((PHONE_NAV_ICON_SIZE * PHONE_NAV_ICON_SIZE) / 8)

struct PhoneNavState {
    bool connected = false;      // phone connected over BLE
    bool active = false;         // navigation running
    bool is_navigation = false;  // true: turn-by-turn, false: general info
    bool has_icon = false;
    std::string title;       // distance to next turn, e.g. "200 m"
    std::string directions;  // instruction / street name
    std::string duration;    // remaining time
    std::string distance;    // remaining distance
    std::string eta;         // arrival time
    std::string speed;       // only from OsmAnd
    uint32_t icon_crc = 0xFFFFFFFF;
    uint8_t icon[PHONE_NAV_ICON_BYTES] = {};  // 48x48, 1bpp, MSB first, row major
};

class PhoneNavBle {
public:
    enum class Event { kConnection, kNavData, kNavIcon };
    using Callback = std::function<void(Event event, const PhoneNavState& state)>;

    static PhoneNavBle& GetInstance() {
        static PhoneNavBle instance;
        return instance;
    }

    // Start BLE advertising with the given device name. Safe to call once.
    bool Start(const std::string& device_name, Callback callback);

    // Phone notifications forwarded by the Chronos app (title: message)
    using NotificationCallback = std::function<void(int icon, const std::string& text)>;
    void SetNotificationCallback(NotificationCallback cb) { notification_cb_ = std::move(cb); }

    PhoneNavState GetState();

    // Internal: called from NimBLE callbacks
    void OnConnect(uint16_t conn_handle);
    void OnDisconnect();
    void OnSubscribe(bool subscribed);
    void OnWrite(const uint8_t* data, size_t len);
    void SendInfo();

private:
    PhoneNavBle() = default;
    void DataReceived();
    void SendCommand(const uint8_t* data, size_t len, bool force_chunked = false);
    void Notify(Event event);
    static std::string ReadCString(const uint8_t* data, size_t len, size_t& pos);

    static constexpr size_t kBufferSize = 600;

    std::mutex mutex_;
    PhoneNavState state_;
    Callback callback_;
    NotificationCallback notification_cb_;
    bool started_ = false;
    bool chunked_ = false;
    uint16_t conn_handle_ = 0xFFFF;
    uint8_t rx_buffer_[kBufferSize] = {};
    size_t rx_length_ = 0;
    uint8_t icon_parts_ = 0;
    void* info_timer_ = nullptr;
};

#endif  // CONFIG_PHONE_NAVIGATION_ENABLE
#endif  // PHONE_NAV_BLE_H
