#include "radio_player.h"

#include <cJSON.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <algorithm>
#include <cstring>
#include <vector>

#include "esp_aac_dec.h"
#include <http.h>
#include <memory>

#include "application.h"
#include "board.h"
#include "display.h"
#include "mcp_server.h"

#define TAG "Radio"

struct Station {
    const char* key;
    const char* name;
    const char* url;
};

// VOV public streams (AAC). Same list as the ES3C28P firmware.
static const Station kStations[] = {
    {"VOV1", "VOV1 Thời sự", "https://stream.vovmedia.vn/vov-1"},
    {"VOV2", "VOV2 Văn hóa", "https://stream.vovmedia.vn/vov-2"},
    {"VOV3", "VOV3 Âm nhạc", "https://stream.vovmedia.vn/vov-3"},
    {"VOV5", "VOV5 Đối ngoại", "https://stream.vovmedia.vn/vov5"},
    {"VOV_GT_HN", "VOV Giao thông HN", "https://stream.vovmedia.vn/vovgt-hn"},
    {"VOV_GT_HCM", "VOV Giao thông HCM", "https://stream.vovmedia.vn/vovgt-hcm"},
    {"VOV_MEKONG", "VOV Mekong FM", "https://stream.vovmedia.vn/vovmekong"},
};

static const Station* FindStation(const std::string& key) {
    std::string k;
    for (char c : key) k += (char)toupper((unsigned char)c);
    for (const auto& s : kStations) {
        if (k == s.key) return &s;
    }
    // loose match: "vov 3", "vov3", "giao thong ha noi"...
    std::string flat;
    for (char c : k) if (isalnum((unsigned char)c)) flat += c;
    for (const auto& s : kStations) {
        std::string sk;
        for (const char* p = s.key; *p; p++) if (isalnum((unsigned char)*p)) sk += *p;
        if (flat == sk) return &s;
    }
    if (flat.find("GIAOTHONG") != std::string::npos || flat.find("VOVGT") != std::string::npos) {
        bool hcm = flat.find("HCM") != std::string::npos || flat.find("SAIGON") != std::string::npos;
        return &kStations[hcm ? 5 : 4];
    }
    return nullptr;
}

std::string RadioPlayer::CurrentName() {
    std::lock_guard<std::mutex> lock(mutex_);
    return name_;
}

bool RadioPlayer::Play(const std::string& station_key) {
    const Station* st = FindStation(station_key);
    if (st == nullptr) return false;
    Stop();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        url_ = st->url;
        name_ = st->name;
    }
    stop_requested_ = false;
    playing_ = true;
    task_running_ = true;
    if (xTaskCreate(TaskEntry, "radio", 12 * 1024, this, 4, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create radio task");
        playing_ = false;
        task_running_ = false;
        return false;
    }
    return true;
}

void RadioPlayer::Stop() {
    stop_requested_ = true;
    for (int i = 0; i < 50 && task_running_; i++) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    playing_ = false;
}

void RadioPlayer::TaskEntry(void* arg) {
    auto* self = static_cast<RadioPlayer*>(arg);
    self->Run();
    self->playing_ = false;
    self->task_running_ = false;
    vTaskDelete(nullptr);
}

// Length of the ADTS frame starting at p (0 if not a valid header)
static size_t AdtsFrameLength(const uint8_t* p, size_t avail) {
    if (avail < 7) return 0;
    if (p[0] != 0xFF || (p[1] & 0xF6) != 0xF0) return 0;
    size_t len = ((size_t)(p[3] & 0x03) << 11) | ((size_t)p[4] << 3) | ((p[5] & 0xE0) >> 5);
    return len >= 7 ? len : 0;
}

void RadioPlayer::Run() {
    std::string url, name;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        url = url_;
        name = name_;
    }
    ESP_LOGI(TAG, "Play %s (%s)", name.c_str(), url.c_str());

    auto& app = Application::GetInstance();
    auto* codec = Board::GetInstance().GetAudioCodec();
    auto network = Board::GetInstance().GetNetwork();
    if (codec == nullptr || network == nullptr) return;

    esp_aac_dec_cfg_t cfg = ESP_AAC_DEC_CONFIG_DEFAULT();
    cfg.aac_plus_enable = true;
    void* dec = nullptr;
    if (esp_aac_dec_open(&cfg, sizeof(cfg), &dec) != ESP_AUDIO_ERR_OK || dec == nullptr) {
        ESP_LOGE(TAG, "AAC decoder open failed");
        return;
    }

    // When started from a voice command the assistant is still talking: wait until the
    // conversation ends (device idle), then connect and play. Stop only when a NEW
    // conversation starts.
    for (int i = 0; !stop_requested_ && app.GetDeviceState() != kDeviceStateIdle; i++) {
        if (i > 1200) {  // ~2 minutes
            ESP_LOGW(TAG, "Device never became idle, giving up");
            esp_aac_dec_close(dec);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (stop_requested_) {
        esp_aac_dec_close(dec);
        return;
    }

    std::unique_ptr<Http> http;
    bool ok = false;
    for (int hop = 0; hop < 4 && !ok; hop++) {
        http = network->CreateHttp(1);
        http->SetHeader("Icy-MetaData", "0");
        http->SetHeader("User-Agent", "Mozilla/5.0 (xiaozhi-s3zero-oled-vn)");
        if (auto opened = http->Open("GET", url); !opened) {
            ESP_LOGE(TAG, "Open failed: %s", opened.error().ToString().c_str());
            // stream.vovmedia.vn rejected the TLS handshake (fatal alert) on this
            // firmware: the stream is public, so retry once over plain HTTP.
            if (url.rfind("https://", 0) == 0) {
                url = "http://" + url.substr(8);
                ESP_LOGW(TAG, "Retrying without TLS: %s", url.c_str());
                continue;
            }
            break;
        }
        auto status = http->GetStatusCode();
        int code = status ? *status : -1;
        std::string ctype = http->GetResponseHeader("Content-Type");
        ESP_LOGI(TAG, "HTTP %d, Content-Type: %s", code, ctype.c_str());
        if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
            std::string loc = http->GetResponseHeader("Location");
            if (loc.empty()) loc = http->GetResponseHeader("location");
            http->Close();
            if (loc.empty()) break;
            ESP_LOGI(TAG, "Redirect -> %s", loc.c_str());
            url = loc;
            continue;
        }
        if (code != 200) {
            ESP_LOGE(TAG, "Bad HTTP status %d", code);
            break;
        }
        ok = true;
    }

    if (ok) {
        app.Schedule([name]() {
            auto display = Board::GetInstance().GetDisplay();
            if (display) display->ShowNotification(("Radio: " + name).c_str(), 3000);
        });
    }

    std::vector<uint8_t> acc;
    acc.reserve(8192);
    std::vector<uint8_t> in(2048);
    std::vector<uint8_t> pcm(8192);
    std::vector<int16_t> out;
    const int out_rate = codec->output_sample_rate();
    double resample_pos = 0;  // fractional read position carried across frames
    int16_t last_sample = 0;
    int errors = 0;
    // Diagnostics (one line every ~5 s in the device log)
    size_t total_bytes = 0;
    int frames_found = 0, frames_ok = 0;
    int last_rate = 0, last_ch = 0;
    int64_t last_report = esp_timer_get_time();
    bool dumped_head = false;

    while (ok && !stop_requested_) {
        // The assistant needs the speaker/mic: stop the radio
        DeviceState st = app.GetDeviceState();
        if (st == kDeviceStateListening || st == kDeviceStateSpeaking || st == kDeviceStateConnecting) {
            ESP_LOGI(TAG, "Stopping radio: assistant active");
            break;
        }

        auto n = http->Read(reinterpret_cast<char*>(in.data()), in.size());
        if (!n) {
            ESP_LOGE(TAG, "Read failed: %s", n.error().ToString().c_str());
            break;
        }
        if (*n == 0) {
            ESP_LOGW(TAG, "Stream ended");
            break;
        }
        acc.insert(acc.end(), in.begin(), in.begin() + *n);
        total_bytes += *n;
        if (!dumped_head && acc.size() >= 16) {
            dumped_head = true;
            ESP_LOGI(TAG, "Stream head: %02X %02X %02X %02X %02X %02X %02X %02X | %.8s",
                     acc[0], acc[1], acc[2], acc[3], acc[4], acc[5], acc[6], acc[7],
                     reinterpret_cast<const char*>(acc.data()));
        }
        if (esp_timer_get_time() - last_report > 5000000) {
            last_report = esp_timer_get_time();
            ESP_LOGI(TAG, "diag: %u KB read, ADTS frames %d, decoded %d, errors %d, %d Hz x%d -> %d Hz",
                     (unsigned)(total_bytes / 1024), frames_found, frames_ok, errors, last_rate, last_ch,
                     out_rate);
        }

        size_t pos = 0;
        while (pos + 7 <= acc.size()) {
            size_t flen = AdtsFrameLength(&acc[pos], acc.size() - pos);
            if (flen == 0) {  // resync
                pos++;
                continue;
            }
            if (pos + flen > acc.size()) break;  // need more data
            frames_found++;

            esp_audio_dec_in_raw_t raw = {};
            raw.buffer = &acc[pos];
            raw.len = flen;
            esp_audio_dec_out_frame_t frame = {};
            frame.buffer = pcm.data();
            frame.len = pcm.size();
            esp_audio_dec_info_t info = {};
            esp_audio_err_t ret = esp_aac_dec_decode(dec, &raw, &frame, &info);
            if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                pcm.resize(frame.needed_size);
                continue;  // retry same frame with a bigger buffer
            }
            pos += flen;
            if (ret != ESP_AUDIO_ERR_OK || frame.decoded_size == 0 || info.sample_rate == 0) {
                if (errors < 5) {
                    ESP_LOGW(TAG, "Decode issue: ret=%d decoded=%u rate=%d ch=%d", (int)ret,
                             (unsigned)frame.decoded_size, (int)info.sample_rate, (int)info.channel);
                }
                // HE-AAC may need a few frames before producing audio: only give up when
                // nothing at all has been decoded
                if (++errors > 200 && frames_ok == 0) {
                    ESP_LOGE(TAG, "Too many decode errors");
                    stop_requested_ = true;
                    break;
                }
                continue;
            }

            frames_ok++;
            last_rate = info.sample_rate;
            last_ch = info.channel;
            if (frames_ok == 1) {
                ESP_LOGI(TAG, "First audio: %d Hz, %d ch, %u bytes/frame", (int)info.sample_rate,
                         (int)info.channel, (unsigned)frame.decoded_size);
            }
            // Downmix to mono and resample (linear) to the codec output rate
            const int16_t* s = reinterpret_cast<const int16_t*>(pcm.data());
            int ch = info.channel > 0 ? info.channel : 1;
            size_t frames = frame.decoded_size / (2 * ch);
            double step = (double)info.sample_rate / out_rate;
            out.clear();
            while (resample_pos < (double)frames) {
                size_t i = (size_t)resample_pos;
                double frac = resample_pos - (double)i;
                int a = 0, b = 0;
                for (int c = 0; c < ch; c++) {
                    b += s[i * ch + c];
                    a += (i == 0 ? last_sample : s[(i - 1) * ch + c]);
                }
                a /= ch;
                b /= ch;
                // interpolate between previous and current sample
                out.push_back((int16_t)(a + (b - a) * frac));
                resample_pos += step;
            }
            resample_pos -= (double)frames;
            if (frames > 0) {
                int v = 0;
                for (int c = 0; c < ch; c++) v += s[(frames - 1) * ch + c];
                last_sample = (int16_t)(v / ch);
            }

            if (!out.empty()) {
                app.GetAudioService().KeepOutputAlive();
                codec->OutputData(out);
            }
        }
        if (pos > 0) acc.erase(acc.begin(), acc.begin() + pos);
        if (acc.size() > 64 * 1024) acc.clear();  // garbage, start over
    }

    if (http) http->Close();
    esp_aac_dec_close(dec);
    ESP_LOGI(TAG, "Radio stopped (%u KB, %d frames decoded)", (unsigned)(total_bytes / 1024), frames_ok);
}

void RadioPlayer::RegisterTools() {
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool("self.radio.play",
        "Phát radio VOV trực tuyến. Dùng khi người dùng nói 'mở radio', 'nghe đài', 'mở VOV giao thông'.\n"
        "Radio tự dừng khi người dùng gọi trợ lý; muốn nghe tiếp thì gọi lại tool này.\n"
        "Args:\n"
        "  station: VOV1 (thời sự, mặc định), VOV2 (văn hóa), VOV3 (âm nhạc), VOV5 (đối ngoại),\n"
        "           VOV_GT_HN (giao thông Hà Nội), VOV_GT_HCM (giao thông TP.HCM), VOV_MEKONG",
        PropertyList({Property("station", kPropertyTypeString, "VOV1")}),
        [this](const PropertyList& p) -> ReturnValue {
            std::string key = p["station"].value<std::string>();
            if (!Play(key)) {
                return "{\"success\": false, \"message\": \"Không có kênh này. Dùng self.radio.list để xem danh sách.\"}";
            }
            return "{\"success\": true, \"message\": \"Đang mở " + CurrentName() +
                   ". Hãy nói ngắn gọn rồi kết thúc hội thoại để radio phát.\"}";
        });

    mcp.AddTool("self.radio.stop", "Tắt radio đang phát.", PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            Stop();
            return true;
        });

    mcp.AddTool("self.radio.list", "Danh sách kênh radio có sẵn.", PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            cJSON* arr = cJSON_CreateArray();
            for (const auto& s : kStations) {
                cJSON* o = cJSON_CreateObject();
                cJSON_AddStringToObject(o, "station", s.key);
                cJSON_AddStringToObject(o, "name", s.name);
                cJSON_AddItemToArray(arr, o);
            }
            return arr;
        });
}
