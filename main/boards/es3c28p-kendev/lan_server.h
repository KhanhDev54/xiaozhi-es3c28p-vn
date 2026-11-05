#ifndef KENDEV_LAN_SERVER_H
#define KENDEV_LAN_SERVER_H

// Small HTTP server on the home network (port 80), started once Wi-Fi is up:
//   GET  /setup?t=TOKEN      settings page (QR accounts, night mode, toggles)
//   POST /setup              save the form
//   POST /api/tx?t=TOKEN     new bank notification: raw text, or JSON
//                            {"text": "...", "amount": 150000, "bank": "...", "name": "...", "content": "..."}
//   GET  /tx.csv?t=TOKEN     transaction history as CSV (opens in Excel)
// The token is generated on first boot and shown on the speaker (Cài đặt).

#include <functional>
#include <string>

class LanServer {
public:
    static LanServer& GetInstance() {
        static LanServer instance;
        return instance;
    }

    // Safe to call repeatedly; starts once when an IP address is available
    void EnsureStarted();
    bool running() const { return server_ != nullptr; }
    std::string SetupUrl();   // http://IP/setup?t=TOKEN, "" if not running

    void SetOnSettingsChanged(std::function<void()> cb) { on_settings_ = std::move(cb); }
    void NotifySettingsChanged() { if (on_settings_) on_settings_(); }

private:
    LanServer() = default;
    void* server_ = nullptr;
    std::function<void()> on_settings_;
};

#endif  // KENDEV_LAN_SERVER_H
