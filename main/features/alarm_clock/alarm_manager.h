#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H

// Alarms (persisted in NVS), countdown timer and night mode.
// A 1 Hz background task checks the wall clock (needs SNTP/server time).

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct AlarmItem {
    int hour = 7;
    int minute = 0;
    bool repeat_daily = false;
    bool enabled = true;
    std::string label;
};

class AlarmManager {
public:
    static AlarmManager& GetInstance() {
        static AlarmManager instance;
        return instance;
    }

    // Start background task + register MCP tools
    void Start();

    bool AddAlarm(const AlarmItem& item, std::string& error);
    std::vector<AlarmItem> ListAlarms();
    bool DeleteAlarm(int index);  // index < 0 -> delete all
    void StartTimer(int seconds, const std::string& label);
    void CancelTimer();
    int TimerRemaining();         // seconds, 0 if none
    void StopRinging();
    bool IsRinging() const { return ringing_; }
    void SetNightMode(bool enabled, int start_hour, int end_hour);

private:
    AlarmManager() = default;
    void Load();
    void Save();
    void Tick();
    void Ring(const std::string& title, const std::string& label);
    void UpdateNightMode(int hour);
    void RegisterTools();
    static void TaskEntry(void* arg);

    std::mutex mutex_;
    std::vector<AlarmItem> alarms_;
    bool started_ = false;
    int last_checked_minute_ = -1;

    int64_t timer_end_us_ = 0;
    std::string timer_label_;

    volatile bool ringing_ = false;
    int ring_seconds_ = 0;

    bool night_enabled_ = true;
    int night_start_ = 22;
    int night_end_ = 6;
    bool night_active_ = false;
};

#endif  // ALARM_MANAGER_H
