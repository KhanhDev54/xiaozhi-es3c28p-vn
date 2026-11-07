#ifndef ZERO_RADIO_PLAYER_H
#define ZERO_RADIO_PLAYER_H

// Internet radio player (shared by the Zero OLED and ES3C28P firmwares).
// - Built-in VOV stations (AAC/ADTS).
// - Vietnamese / worldwide music stations found at run time in the public
//   Radio Browser directory (https://www.radio-browser.info), MP3 or AAC streams.
// Decodes with esp_audio_codec (AAC + MP3), downmixes to mono, resamples to the
// codec output rate and writes to the codec. Stops when the user talks to the assistant.
// MCP tools: self.radio.play / stop / list, self.music.search_radio / play_radio

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

class RadioPlayer {
public:
    static RadioPlayer& GetInstance() {
        static RadioPlayer instance;
        return instance;
    }

    struct FoundStation {
        std::string uuid;
        std::string name;
        std::string url;
        std::string codec;  // "MP3", "AAC", "AAC+"
        std::string tags;
        std::string country;
        int bitrate = 0;
    };

    void RegisterTools();
    bool Play(const std::string& station_key);
    // Play any MP3/AAC stream. codec_hint: "MP3", "AAC" or "" (detect).
    bool PlayUrl(const std::string& url, const std::string& name, const std::string& codec_hint = "");
    void Stop();
    bool IsPlaying() const { return playing_; }
    std::string CurrentName();

    // Search the Radio Browser directory. vietnam_only: stations in Vietnam or in Vietnamese.
    bool SearchStations(const std::string& query, bool vietnam_only, std::vector<FoundStation>& out,
                        std::string& error);

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
    std::string codec_hint_;
    std::vector<FoundStation> last_results_;
    std::string api_host_;  // Radio Browser server that answered last
    std::atomic<bool> playing_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> task_running_{false};
};

#endif  // ZERO_RADIO_PLAYER_H
