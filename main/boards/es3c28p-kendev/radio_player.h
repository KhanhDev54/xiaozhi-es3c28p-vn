#ifndef ZERO_RADIO_PLAYER_H
#define ZERO_RADIO_PLAYER_H

// Internet radio (VOV, AAC/ADTS over HTTP) for the Zero board.
// Decodes with esp_audio_codec's AAC decoder only (no other decoders linked),
// downmixes to mono, resamples to the codec output rate and writes to the codec.
// Stops automatically when the user starts talking to the assistant.
// MCP tools: self.radio.play / self.radio.stop / self.radio.list

#include <atomic>
#include <mutex>
#include <string>

class RadioPlayer {
public:
    static RadioPlayer& GetInstance() {
        static RadioPlayer instance;
        return instance;
    }

    void RegisterTools();
    bool Play(const std::string& station_key);
    void Stop();
    bool IsPlaying() const { return playing_; }
    std::string CurrentName();

    // Station list for the on-screen FM app
    static int StationCount();
    static const char* StationKey(int i);
    static const char* StationName(int i);

private:
    RadioPlayer() = default;
    static void TaskEntry(void* arg);
    void Run();

    std::mutex mutex_;
    std::string url_;
    std::string name_;
    std::atomic<bool> playing_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> task_running_{false};
};

#endif  // ZERO_RADIO_PLAYER_H
