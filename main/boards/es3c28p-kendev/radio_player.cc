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
#include "esp_mp3_dec.h"
#include <http.h>
#include <memory>

#include "application.h"
#include "board.h"
#include "display.h"
#include "mcp_server.h"
#include "net_util.h"

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

int RadioPlayer::StationCount() { return sizeof(kStations) / sizeof(kStations[0]); }
const char* RadioPlayer::StationKey(int i) { return kStations[i].key; }
const char* RadioPlayer::StationName(int i) { return kStations[i].name; }

std::string RadioPlayer::CurrentName() {
    std::lock_guard<std::mutex> lock(mutex_);
    return name_;
}

void RadioPlayer::Current(std::string& url, std::string& name, std::string& codec_hint) {
    std::lock_guard<std::mutex> lock(mutex_);
    url = url_;
    name = name_;
    codec_hint = codec_hint_;
}

bool RadioPlayer::Play(const std::string& station_key) {
    const Station* st = FindStation(station_key);
    if (st == nullptr) return false;
    return PlayUrl(st->url, st->name, "AAC");
}

bool RadioPlayer::PlayUrl(const std::string& url, const std::string& name, const std::string& codec_hint) {
    if (url.empty()) return false;
    Stop();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        url_ = url;
        name_ = name;
        codec_hint_ = codec_hint;
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

// ---------------------------------------------------------------------------
// Frame parsers. Each returns the length of the frame starting at p, or 0.

// AAC in ADTS
static size_t AdtsFrameLength(const uint8_t* p, size_t avail) {
    if (avail < 7) return 0;
    if (p[0] != 0xFF || (p[1] & 0xF6) != 0xF0) return 0;
    size_t len = ((size_t)(p[3] & 0x03) << 11) | ((size_t)p[4] << 3) | ((p[5] & 0xE0) >> 5);
    return len >= 7 ? len : 0;
}

// MPEG-1/2/2.5 Layer III
static size_t Mp3FrameLength(const uint8_t* p, size_t avail) {
    if (avail < 4) return 0;
    if (p[0] != 0xFF || (p[1] & 0xE0) != 0xE0) return 0;
    int ver = (p[1] >> 3) & 3;    // 0 = 2.5, 1 = reserved, 2 = MPEG2, 3 = MPEG1
    int layer = (p[1] >> 1) & 3;  // 1 = Layer III
    if (ver == 1 || layer != 1) return 0;
    int bri = (p[2] >> 4) & 0x0F, sri = (p[2] >> 2) & 3, pad = (p[2] >> 1) & 1;
    if (bri == 0 || bri == 15 || sri == 3) return 0;
    static const uint16_t kBr1[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    static const uint16_t kBr2[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
    static const uint32_t kSr[] = {44100, 48000, 32000};
    uint32_t sr = kSr[sri];
    if (ver == 2) sr /= 2;
    else if (ver == 0) sr /= 4;
    uint32_t br = (ver == 3 ? kBr1[bri] : kBr2[bri]) * 1000u;
    size_t len = (ver == 3 ? 144u * br / sr : 72u * br / sr) + pad;
    return len >= 4 ? len : 0;
}

enum class StreamCodec { kUnknown, kAac, kMp3 };

static StreamCodec CodecFromText(std::string t) {
    for (auto& c : t) c = (char)tolower((unsigned char)c);
    if (t.find("aac") != std::string::npos) return StreamCodec::kAac;
    if (t.find("mpeg") != std::string::npos || t.find("mp3") != std::string::npos) return StreamCodec::kMp3;
    return StreamCodec::kUnknown;
}

// Look at the first bytes: whichever format shows two consecutive valid frames wins
static StreamCodec SniffCodec(const std::vector<uint8_t>& b) {
    for (size_t i = 0; i + 8 < b.size(); i++) {
        size_t l = AdtsFrameLength(&b[i], b.size() - i);
        if (l && i + l + 7 <= b.size() && AdtsFrameLength(&b[i + l], b.size() - i - l)) return StreamCodec::kAac;
        l = Mp3FrameLength(&b[i], b.size() - i);
        if (l && i + l + 4 <= b.size() && Mp3FrameLength(&b[i + l], b.size() - i - l)) return StreamCodec::kMp3;
    }
    return StreamCodec::kUnknown;
}

// Skip an ID3v2 tag at the start of the buffer (some MP3 servers send one)
static size_t Id3Length(const std::vector<uint8_t>& b) {
    if (b.size() < 10 || b[0] != 'I' || b[1] != 'D' || b[2] != '3') return 0;
    return 10 + (((size_t)(b[6] & 0x7F) << 21) | ((size_t)(b[7] & 0x7F) << 14) |
                 ((size_t)(b[8] & 0x7F) << 7) | (size_t)(b[9] & 0x7F));
}

void RadioPlayer::Run() {
    std::string url, name, hint;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        url = url_;
        name = name_;
        hint = codec_hint_;
    }
    ESP_LOGI(TAG, "Play %s (%s)", name.c_str(), url.c_str());

    auto& app = Application::GetInstance();
    auto* codec = Board::GetInstance().GetAudioCodec();
    auto network = Board::GetInstance().GetNetwork();
    if (codec == nullptr || network == nullptr) return;

    // When started from a voice command the assistant is still talking: wait until the
    // conversation ends (device idle), then connect and play. Stop only when a NEW
    // conversation starts.
    for (int i = 0; !stop_requested_ && app.GetDeviceState() != kDeviceStateIdle; i++) {
        if (i > 1200) {  // ~2 minutes
            ESP_LOGW(TAG, "Device never became idle, giving up");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (stop_requested_) return;

    std::unique_ptr<Http> http;
    bool ok = false;
    std::string ctype;
    for (int hop = 0; hop < 4 && !ok; hop++) {
        http = network->CreateHttp(1);
        http->SetHeader("Icy-MetaData", "0");
        http->SetHeader("User-Agent", "KenDev-Xiaozhi/1.0");
        if (auto opened = http->Open("GET", url); !opened) {
            ESP_LOGE(TAG, "Open failed: %s", opened.error().ToString().c_str());
            // Some servers (stream.vovmedia.vn) reject the TLS handshake on this firmware.
            // Public streams: retry once over plain HTTP.
            if (url.rfind("https://", 0) == 0) {
                url = "http://" + url.substr(8);
                ESP_LOGW(TAG, "Retrying without TLS: %s", url.c_str());
                continue;
            }
            break;
        }
        auto status = http->GetStatusCode();
        int code = status ? *status : -1;
        ctype = http->GetResponseHeader("Content-Type");
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
    if (!ok) {
        app.Schedule([name]() {
            auto display = Board::GetInstance().GetDisplay();
            if (display) display->ShowNotification(("Không mở được: " + name).c_str(), 3000);
        });
        if (http) http->Close();
        return;
    }

    StreamCodec kind = CodecFromText(ctype);
    if (kind == StreamCodec::kUnknown) kind = CodecFromText(hint);

    std::vector<uint8_t> acc;
    acc.reserve(8192);
    std::vector<uint8_t> in(2048);
    // Fill a little to detect the format if headers did not say, and to skip ID3
    while (!stop_requested_ && acc.size() < 4096) {
        auto n = http->Read(reinterpret_cast<char*>(in.data()), in.size());
        if (!n || *n == 0) break;
        acc.insert(acc.end(), in.begin(), in.begin() + *n);
    }
    size_t id3 = Id3Length(acc);
    if (id3 > 0 && id3 <= acc.size()) acc.erase(acc.begin(), acc.begin() + id3);
    StreamCodec sniffed = SniffCodec(acc);
    if (sniffed != StreamCodec::kUnknown && sniffed != kind) {
        ESP_LOGW(TAG, "Header says %d, data looks like %d: using data", (int)kind, (int)sniffed);
        kind = sniffed;
    }
    if (kind == StreamCodec::kUnknown) {
        ESP_LOGE(TAG, "Unsupported stream format (Content-Type '%s')", ctype.c_str());
        app.Schedule([]() {
            auto display = Board::GetInstance().GetDisplay();
            if (display) display->ShowNotification("Định dạng đài không hỗ trợ", 3000);
        });
        http->Close();
        return;
    }
    const bool is_mp3 = kind == StreamCodec::kMp3;
    ESP_LOGI(TAG, "Stream codec: %s", is_mp3 ? "MP3" : "AAC");

    void* dec = nullptr;
    esp_audio_err_t oret;
    if (is_mp3) {
        oret = esp_mp3_dec_open(nullptr, 0, &dec);
    } else {
        esp_aac_dec_cfg_t cfg = ESP_AAC_DEC_CONFIG_DEFAULT();
        cfg.aac_plus_enable = true;
        oret = esp_aac_dec_open(&cfg, sizeof(cfg), &dec);
    }
    if (oret != ESP_AUDIO_ERR_OK || dec == nullptr) {
        ESP_LOGE(TAG, "Decoder open failed (%d)", (int)oret);
        http->Close();
        return;
    }
    auto frame_len = is_mp3 ? Mp3FrameLength : AdtsFrameLength;
    const size_t min_header = is_mp3 ? 4 : 7;
    auto decode = [&](esp_audio_dec_in_raw_t* raw, esp_audio_dec_out_frame_t* f, esp_audio_dec_info_t* info) {
        return is_mp3 ? esp_mp3_dec_decode(dec, raw, f, info) : esp_aac_dec_decode(dec, raw, f, info);
    };

    app.Schedule([name]() {
        auto display = Board::GetInstance().GetDisplay();
        if (display) display->ShowNotification(("Radio: " + name).c_str(), 3000);
    });

    std::vector<uint8_t> pcm(8192);
    std::vector<int16_t> out;
    const int out_rate = codec->output_sample_rate();
    double resample_pos = 0;  // fractional read position carried across frames
    int16_t last_sample = 0;
    int errors = 0;
    bool synced = false;
    // Diagnostics (one line every ~5 s in the device log)
    size_t total_bytes = acc.size();
    int frames_found = 0, frames_ok = 0;
    int last_rate = 0, last_ch = 0;
    int64_t last_report = esp_timer_get_time();

    bool first = true;
    while (!stop_requested_) {
        // The assistant needs the speaker/mic: stop the radio
        DeviceState st = app.GetDeviceState();
        if (st == kDeviceStateListening || st == kDeviceStateSpeaking || st == kDeviceStateConnecting) {
            ESP_LOGI(TAG, "Stopping radio: assistant active");
            break;
        }

        if (!first) {
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
        }
        first = false;
        if (esp_timer_get_time() - last_report > 5000000) {
            last_report = esp_timer_get_time();
            ESP_LOGI(TAG, "diag: %u KB read, frames %d, decoded %d, errors %d, %d Hz x%d -> %d Hz",
                     (unsigned)(total_bytes / 1024), frames_found, frames_ok, errors, last_rate, last_ch,
                     out_rate);
        }

        size_t pos = 0;
        while (pos + min_header <= acc.size()) {
            size_t flen = frame_len(&acc[pos], acc.size() - pos);
            if (flen == 0) {  // resync
                synced = false;
                pos++;
                continue;
            }
            if (!synced) {
                // Avoid false sync words: the next frame header must be valid too
                if (pos + flen + min_header > acc.size()) break;  // need more data
                if (frame_len(&acc[pos + flen], acc.size() - pos - flen) == 0) {
                    pos++;
                    continue;
                }
                synced = true;
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
            esp_audio_err_t ret = decode(&raw, &frame, &info);
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
                // HE-AAC / MP3 bit reservoir may need a few frames before producing audio:
                // only give up when nothing at all has been decoded
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

    http->Close();
    if (is_mp3) esp_mp3_dec_close(dec);
    else esp_aac_dec_close(dec);
    ESP_LOGI(TAG, "Radio stopped (%u KB, %d frames decoded)", (unsigned)(total_bytes / 1024), frames_ok);
}

// ---------------------------------------------------------------------------
// Radio Browser directory (community-run, free, no key). API docs: https://api.radio-browser.info
// The project asks clients to send a descriptive User-Agent and to spread load over its servers.

static const char* kRbHosts[] = {
    "de1.api.radio-browser.info",
    "de2.api.radio-browser.info",
    "fi1.api.radio-browser.info",
    "all.api.radio-browser.info",
};

static bool RbGet(std::string& host_cache, const std::string& path, std::string& body, std::string& error) {
    std::vector<std::string> hosts;
    if (!host_cache.empty()) hosts.push_back(host_cache);
    for (const char* h : kRbHosts) {
        if (host_cache != h) hosts.push_back(h);
    }
    for (const auto& h : hosts) {
        for (const char* scheme : {"https://", "http://"}) {
            if (zero::HttpGetText(std::string(scheme) + h + path, body, error)) {
                host_cache = h;
                return true;
            }
            ESP_LOGW(TAG, "Radio Browser %s%s: %s", scheme, h.c_str(), error.c_str());
        }
    }
    return false;
}

static std::string JStr(cJSON* o, const char* key) {
    cJSON* v = cJSON_GetObjectItem(o, key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}

static int JInt(cJSON* o, const char* key) {
    cJSON* v = cJSON_GetObjectItem(o, key);
    return cJSON_IsNumber(v) ? v->valueint : 0;
}

bool RadioPlayer::SearchStations(const std::string& query, bool vietnam_only, std::vector<FoundStation>& out,
                                 std::string& error) {
    out.clear();
    const std::string q = zero::UrlEncode(query);
    const std::string base =
        "/json/stations/search?hidebroken=true&order=clickcount&reverse=true&limit=25";
    std::vector<std::string> filters;
    if (vietnam_only) {
        filters = {"&countrycode=VN", "&language=vietnamese"};
    } else {
        filters = {""};
    }
    std::vector<std::string> fields;
    if (query.empty()) {
        fields = {""};
    } else {
        fields = {"&name=" + q, "&tag=" + q};
    }

    std::string host;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        host = api_host_;
    }
    bool any_answer = false;
    for (const auto& field : fields) {
        for (const auto& filter : filters) {
            std::string body;
            if (!RbGet(host, base + field + filter, body, error)) continue;
            any_answer = true;
            cJSON* arr = cJSON_Parse(body.c_str());
            body.clear();
            if (!cJSON_IsArray(arr)) {
                cJSON_Delete(arr);
                continue;
            }
            cJSON* it = nullptr;
            cJSON_ArrayForEach(it, arr) {
                if (out.size() >= 8) break;
                FoundStation s;
                s.codec = JStr(it, "codec");
                for (auto& c : s.codec) c = (char)toupper((unsigned char)c);
                if (s.codec != "MP3" && s.codec != "AAC" && s.codec != "AAC+") continue;
                if (JInt(it, "hls") != 0 || JInt(it, "lastcheckok") != 1) continue;
                s.url = JStr(it, "url_resolved");
                if (s.url.empty()) s.url = JStr(it, "url");
                if (s.url.empty() || s.url.find(".m3u8") != std::string::npos) continue;
                s.bitrate = JInt(it, "bitrate");
                if (s.bitrate > 320) continue;
                s.uuid = JStr(it, "stationuuid");
                bool dup = false;
                for (const auto& o : out) dup = dup || o.uuid == s.uuid || o.url == s.url;
                if (dup) continue;
                s.name = JStr(it, "name");
                s.tags = JStr(it, "tags");
                if (s.tags.size() > 60) s.tags.resize(60);
                s.country = JStr(it, "countrycode");
                out.push_back(std::move(s));
            }
            cJSON_Delete(arr);
            if (out.size() >= 8) break;
        }
        if (!out.empty()) break;  // name matches found: no need for the tag search
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        api_host_ = host;
        last_results_ = out;
    }
    if (!any_answer) {
        if (error.empty()) error = "Radio Browser không phản hồi";
        return false;
    }
    error.clear();
    return true;
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

    mcp.AddTool("self.radio.stop", "Tắt radio hoặc đài nhạc đang phát.", PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            Stop();
            return true;
        });

    mcp.AddTool("self.radio.list", "Danh sách kênh VOV có sẵn.", PropertyList(),
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

    mcp.AddTool("self.music.search_radio",
        "Tìm đài phát nhạc trực tuyến trong danh bạ công khai Radio Browser (miễn phí, hợp lệ).\n"
        "Dùng khi người dùng muốn nghe nhạc, nhạc Việt, bolero, nhạc trẻ, nhạc không lời, lo-fi, jazz...\n"
        "Thiết bị KHÔNG phát được bài hát cụ thể theo tên (không có nguồn nhạc bản quyền); "
        "hãy nói rõ điều đó và đề nghị mở một đài nhạc cùng thể loại.\n"
        "Args:\n"
        "  query: từ khoá ngắn, KHÔNG dấu, tên đài hoặc thể loại (vd. 'bolero', 'nhac tre', 'pop', "
        "'lofi', 'jazz', 'classical', 'VOH', 'Ha Noi'). Để trống để lấy các đài Việt phổ biến.\n"
        "  vietnam: true = chỉ đài ở Việt Nam hoặc nói tiếng Việt (mặc định); false = toàn thế giới.\n"
        "Kết quả có 'index'; gọi self.music.play_radio với index đó để phát.",
        PropertyList({Property("query", kPropertyTypeString, std::string("")), Property("vietnam", kPropertyTypeBoolean, true)}),
        [this](const PropertyList& p) -> ReturnValue {
            std::vector<FoundStation> found;
            std::string err;
            if (!SearchStations(p["query"].value<std::string>(), p["vietnam"].value<bool>(), found, err)) {
                return "{\"success\": false, \"message\": \"Không kết nối được danh bạ đài: " + err + "\"}";
            }
            cJSON* root = cJSON_CreateObject();
            cJSON_AddBoolToObject(root, "success", true);
            cJSON* arr = cJSON_AddArrayToObject(root, "stations");
            for (size_t i = 0; i < found.size(); i++) {
                cJSON* o = cJSON_CreateObject();
                cJSON_AddNumberToObject(o, "index", (double)(i + 1));
                cJSON_AddStringToObject(o, "name", found[i].name.c_str());
                cJSON_AddStringToObject(o, "tags", found[i].tags.c_str());
                cJSON_AddStringToObject(o, "country", found[i].country.c_str());
                cJSON_AddNumberToObject(o, "kbps", found[i].bitrate);
                cJSON_AddItemToArray(arr, o);
            }
            if (found.empty()) {
                cJSON_AddStringToObject(root, "message",
                                        "Không thấy đài phù hợp. Thử từ khoá khác (tiếng Anh, không dấu) hoặc vietnam=false.");
            }
            return root;
        });

    mcp.AddTool("self.music.play_radio",
        "Phát một đài nhạc tìm được bằng self.music.search_radio.\n"
        "Args:\n"
        "  index: số thứ tự trong kết quả tìm kiếm gần nhất (1, 2, ...). 0 = tự tìm theo query và mở đài đầu tiên.\n"
        "  query: từ khoá (chỉ dùng khi index = 0), cùng quy tắc như search_radio; trống = đài nhạc Việt phổ biến.\n"
        "Đài tự dừng khi người dùng gọi trợ lý; hãy trả lời thật ngắn rồi kết thúc hội thoại để nhạc phát.",
        PropertyList({Property("index", kPropertyTypeInteger, 0, 0, 20), Property("query", kPropertyTypeString, std::string(""))}),
        [this](const PropertyList& p) -> ReturnValue {
            int index = p["index"].value<int>();
            FoundStation pick;
            bool have = false;
            if (index > 0) {
                std::lock_guard<std::mutex> lock(mutex_);
                if ((size_t)index <= last_results_.size()) {
                    pick = last_results_[index - 1];
                    have = true;
                }
            }
            if (!have) {
                std::vector<FoundStation> found;
                std::string err;
                std::string q = p["query"].value<std::string>();
                if (!SearchStations(q.empty() ? "music" : q, true, found, err) || found.empty()) {
                    if (!SearchStations(q.empty() ? "music" : q, false, found, err) || found.empty()) {
                        return "{\"success\": false, \"message\": \"Không tìm được đài phù hợp.\"}";
                    }
                }
                pick = found[0];
            }
            // Courtesy click count for the directory (also returns the current stream URL)
            if (!pick.uuid.empty()) {
                std::string body, err, host;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    host = api_host_;
                }
                if (!host.empty() && zero::HttpGetText("https://" + host + "/json/url/" + pick.uuid, body, err)) {
                    cJSON* o = cJSON_Parse(body.c_str());
                    std::string u = o ? JStr(o, "url") : "";
                    cJSON_Delete(o);
                    if (!u.empty() && u.find(".m3u") == std::string::npos && u.find(".pls") == std::string::npos) {
                        pick.url = u;
                    }
                }
            }
            if (!PlayUrl(pick.url, pick.name, pick.codec)) {
                return "{\"success\": false, \"message\": \"Không mở được đài.\"}";
            }
            cJSON* root = cJSON_CreateObject();
            cJSON_AddBoolToObject(root, "success", true);
            cJSON_AddStringToObject(root, "station", pick.name.c_str());
            cJSON_AddStringToObject(root, "message",
                                    "Đang mở đài. Nói thật ngắn rồi kết thúc hội thoại để nhạc phát.");
            return root;
        });
}
