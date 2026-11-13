#ifndef KENDEV_HUD_LINK_H
#define KENDEV_HUD_LINK_H

// Navigation data from the phone over the home network ("HUD Link", HLP/1).
// The phone app is the WebSocket client; the speaker runs the server:
//   ws://<speaker IP>:8765/hlp   text frames, one JSON object per frame
// Messages handled: hi (new session), s (full state snapshot), ping (answered with
// pong right away), bye. Unknown keys and types are ignored, as the protocol asks.

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct HudAlert {
    int kind = 0;      // alert code (0 = none)
    int dist = -1;     // metres, -1 = unknown
    int value = 0;     // km/h for speed-drop / end of restriction, 0 = none
    int severity = 0;  // traffic jam 1..5, 0 = none
    int minutes = 0;   // traffic jam delay, 0 = none
};

struct HudState {
    bool nav = false;       // route guidance active
    int spd = 0;            // km/h
    int lim = 0;            // km/h, 0 = no sign
    bool over = false;
    int trn = 0;            // next manoeuvre (0 = none)
    int trn2 = 0;           // the one after
    int dst = -1;           // metres to the next manoeuvre, -1 = none
    int exit = 0;           // roundabout exit, 0 = n/a
    std::vector<std::pair<int, int>> lanes;  // [directions mask, recommended mask], left to right
    std::string st;         // current street
    std::string st2;        // street after the next turn
    std::string eta;        // "HH:MM"
    int rmin = 0;           // minutes left
    int rm = 0;             // metres left
    bool avg = false;       // inside a no-passing zone
    int avg_left = 0;       // metres left in that zone
    std::vector<HudAlert> alerts;  // nearest first (at most 4)
};

class HudLink {
public:
    static HudLink& GetInstance() {
        static HudLink instance;
        return instance;
    }

    // Safe to call repeatedly; starts once Wi-Fi has an address
    void EnsureStarted();
    bool running() const { return server_ != nullptr; }
    std::string Url();  // ws://IP:8765/hlp, "" when not running

    bool connected() const { return client_fd_ >= 0; }
    // Milliseconds since the last message from the phone (large when none yet)
    int64_t SilenceMs() const;
    // Copy of the latest state; returns the revision (bumps on every snapshot)
    int Snapshot(HudState& out);
    int revision() const { return revision_; }

    // Called from the server task
    void OnOpen(int fd);
    void OnClose(int fd);
    // Handles one text frame; returns the reply to send (empty = none)
    std::string OnText(const char* text, size_t len);

private:
    HudLink() = default;
    void* server_ = nullptr;
    volatile int client_fd_ = -1;
    int64_t last_rx_us_ = 0;     // guarded by mutex_
    volatile int revision_ = 0;
    int session_ = -1;
    mutable std::mutex mutex_;
    HudState state_;
};

#endif  // KENDEV_HUD_LINK_H
