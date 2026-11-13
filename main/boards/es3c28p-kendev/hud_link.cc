#include "hud_link.h"

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <sys/socket.h>
#include <unistd.h>
#include <wifi_manager.h>

#include <cstring>

#define TAG "HudLink"

static const int kPort = 8765;
static const size_t kMaxFrame = 2048;  // protocol frames are <= 512 bytes

// Fields this screen draws; sent once when the phone connects ("dev")
static std::string DevMessage() {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "v", 1);
    cJSON_AddStringToObject(root, "t", "dev");
    cJSON_AddStringToObject(root, "name", "KenDev ES3C28P");
    cJSON_AddStringToObject(root, "fw", esp_app_get_description()->version);
    cJSON* proto = cJSON_AddArrayToObject(root, "proto");
    cJSON_AddItemToArray(proto, cJSON_CreateNumber(1));
    cJSON* disp = cJSON_AddObjectToObject(root, "disp");
    cJSON_AddNumberToObject(disp, "w", 320);
    cJSON_AddNumberToObject(disp, "h", 240);
    cJSON_AddBoolToObject(disp, "color", true);
    static const char* kCan[] = {"speed", "limit", "turn", "lanes", "street", "eta", "avgzone", "alerts"};
    cJSON* can = cJSON_AddArrayToObject(root, "can");
    for (auto c : kCan) cJSON_AddItemToArray(can, cJSON_CreateString(c));
    cJSON* want = cJSON_AddObjectToObject(root, "want");
    cJSON_AddNumberToObject(want, "rate", 4);
    static const char* kFields[] = {"nav", "spd",  "lim", "over", "trn", "trn2", "dst", "exit",
                                    "lan", "st",   "st2", "eta",  "rmin", "rm",  "avg", "avgL",
                                    "alr", "alrD", "alrV", "alrS", "alrM", "alrs", "ts"};
    cJSON* fields = cJSON_AddArrayToObject(want, "fields");
    for (auto f : kFields) cJSON_AddItemToArray(fields, cJSON_CreateString(f));
    char* s = cJSON_PrintUnformatted(root);
    std::string out = s ? s : "";
    cJSON_free(s);
    cJSON_Delete(root);
    return out;
}

static esp_err_t SendText(httpd_req_t* req, const std::string& text) {
    httpd_ws_frame_t f = {};
    f.type = HTTPD_WS_TYPE_TEXT;
    f.final = true;
    f.payload = (uint8_t*)text.data();
    f.len = text.size();
    return httpd_ws_send_frame(req, &f);
}

static esp_err_t HandleWs(httpd_req_t* req) {
    auto& link = HudLink::GetInstance();
    if (req->method == HTTP_GET) {
        // Handshake done: a new phone connection
        link.OnOpen(httpd_req_to_sockfd(req));
        SendText(req, DevMessage());
        return ESP_OK;
    }
    httpd_ws_frame_t f = {};
    esp_err_t err = httpd_ws_recv_frame(req, &f, 0);
    if (err != ESP_OK) return err;
    if (f.len == 0) return ESP_OK;
    if (f.len > kMaxFrame) {
        ESP_LOGW(TAG, "frame too large (%u)", (unsigned)f.len);
        return ESP_FAIL;  // closes the connection
    }
    std::string buf(f.len, '\0');
    f.payload = (uint8_t*)buf.data();
    err = httpd_ws_recv_frame(req, &f, f.len);
    if (err != ESP_OK) return err;
    if (f.type != HTTPD_WS_TYPE_TEXT) return ESP_OK;
    std::string reply = link.OnText(buf.data(), buf.size());
    if (!reply.empty()) SendText(req, reply);
    return ESP_OK;
}

static void CloseSocket(httpd_handle_t, int fd) {
    HudLink::GetInstance().OnClose(fd);
    close(fd);
}

void HudLink::EnsureStarted() {
    if (server_ != nullptr) return;
    auto& wifi = WifiManager::GetInstance();
    if (!wifi.IsConnected()) return;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = kPort;
    config.ctrl_port = ESP_HTTPD_DEF_CTRL_PORT + 1;  // port 80 server uses the default
    config.max_uri_handlers = 2;
    config.max_open_sockets = 3;
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    config.close_fn = CloseSocket;
    httpd_handle_t handle = nullptr;
    esp_err_t err = httpd_start(&handle, &config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return;
    }
    httpd_uri_t ws = {};
    ws.uri = "/hlp";
    ws.method = HTTP_GET;
    ws.handler = HandleWs;
    ws.is_websocket = true;
    ws.handle_ws_control_frames = false;  // ping / pong / close handled by the server
    httpd_register_uri_handler(handle, &ws);
    server_ = handle;
    ESP_LOGI(TAG, "HUD Link on %s", Url().c_str());
}

std::string HudLink::Url() {
    if (server_ == nullptr) return "";
    return "ws://" + WifiManager::GetInstance().GetIpAddress() + ":" + std::to_string(kPort) + "/hlp";
}

int64_t HudLink::SilenceMs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (last_rx_us_ == 0) return INT32_MAX;
    return (esp_timer_get_time() - last_rx_us_) / 1000;
}

int HudLink::Snapshot(HudState& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    out = state_;
    return revision_;
}

void HudLink::OnOpen(int fd) {
    int old = client_fd_;
    client_fd_ = fd;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        last_rx_us_ = esp_timer_get_time();
        state_ = HudState();
        session_ = -1;
    }
    revision_ = revision_ + 1;
    ESP_LOGI(TAG, "phone connected (fd %d)", fd);
    // One phone at a time: the newest connection wins
    if (old >= 0 && old != fd && server_ != nullptr) httpd_sess_trigger_close((httpd_handle_t)server_, old);
}

void HudLink::OnClose(int fd) {
    if (client_fd_ != fd) return;
    client_fd_ = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        last_rx_us_ = 0;
        state_ = HudState();
    }
    revision_ = revision_ + 1;
    ESP_LOGI(TAG, "phone disconnected");
}

static int Int(cJSON* obj, const char* key, int def) {
    cJSON* v = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(v) ? (int)v->valuedouble : def;
}

static std::string Str(cJSON* obj, const char* key) {
    cJSON* v = cJSON_GetObjectItem(obj, key);
    if (!cJSON_IsString(v)) return "";
    std::string s = v->valuestring;
    if (s.size() > 120) s.resize(120);  // 40 characters, up to 3 bytes each
    return s;
}

std::string HudLink::OnText(const char* text, size_t len) {
    cJSON* json = cJSON_ParseWithLength(text, len);
    if (!cJSON_IsObject(json)) {
        cJSON_Delete(json);
        return "";
    }
    std::string reply;
    std::string t = Str(json, "t");
    int64_t now = esp_timer_get_time();

    if (t == "ping") {
        cJSON* pong = cJSON_CreateObject();
        cJSON_AddNumberToObject(pong, "v", 1);
        cJSON_AddStringToObject(pong, "t", "pong");
        cJSON* ts = cJSON_GetObjectItem(json, "ts");
        if (cJSON_IsNumber(ts)) cJSON_AddNumberToObject(pong, "ts", ts->valuedouble);
        char* s = cJSON_PrintUnformatted(pong);
        if (s) reply = s;
        cJSON_free(s);
        cJSON_Delete(pong);
        std::lock_guard<std::mutex> lock(mutex_);
        last_rx_us_ = now;
    } else if (t == "hi") {
        std::lock_guard<std::mutex> lock(mutex_);
        int sess = Int(json, "sess", -1);
        if (sess != session_) state_ = HudState();  // new session: forget the old route
        session_ = sess;
        last_rx_us_ = now;
        ESP_LOGI(TAG, "hi from %s, session %d", Str(json, "app").c_str(), sess);
    } else if (t == "s") {
        HudState s;
        s.nav = Int(json, "nav", 0) != 0;
        s.spd = Int(json, "spd", 0);
        s.lim = Int(json, "lim", 0);
        s.over = Int(json, "over", 0) != 0;
        s.trn = Int(json, "trn", 0);
        s.trn2 = Int(json, "trn2", 0);
        s.dst = Int(json, "dst", -1);
        s.exit = Int(json, "exit", 0);
        cJSON* lan = cJSON_GetObjectItem(json, "lan");
        if (cJSON_IsArray(lan)) {
            cJSON* pair;
            cJSON_ArrayForEach(pair, lan) {
                if (!cJSON_IsArray(pair) || cJSON_GetArraySize(pair) < 2 || s.lanes.size() >= 12) continue;
                cJSON* a = cJSON_GetArrayItem(pair, 0);
                cJSON* b = cJSON_GetArrayItem(pair, 1);
                s.lanes.push_back({cJSON_IsNumber(a) ? a->valueint : 0, cJSON_IsNumber(b) ? b->valueint : 0});
            }
        }
        s.st = Str(json, "st");
        s.st2 = Str(json, "st2");
        s.eta = Str(json, "eta");
        s.rmin = Int(json, "rmin", 0);
        s.rm = Int(json, "rm", 0);
        if (s.rm == 0) {
            cJSON* rkm = cJSON_GetObjectItem(json, "rkm");
            if (cJSON_IsNumber(rkm)) s.rm = (int)(rkm->valuedouble * 1000);
        }
        s.avg = Int(json, "avg", 0) != 0;
        s.avg_left = Int(json, "avgL", 0);
        cJSON* alrs = cJSON_GetObjectItem(json, "alrs");
        if (cJSON_IsArray(alrs)) {
            cJSON* a;
            cJSON_ArrayForEach(a, alrs) {
                if (!cJSON_IsObject(a) || s.alerts.size() >= 4) continue;
                HudAlert al;
                al.kind = Int(a, "k", 0);
                al.dist = Int(a, "d", -1);
                al.value = Int(a, "v", 0);
                al.severity = Int(a, "s", 0);
                al.minutes = Int(a, "m", 0);
                if (al.kind != 0) s.alerts.push_back(al);
            }
        } else if (Int(json, "alr", 0) != 0) {
            HudAlert al;
            al.kind = Int(json, "alr", 0);
            al.dist = Int(json, "alrD", -1);
            al.value = Int(json, "alrV", 0);
            al.severity = Int(json, "alrS", 0);
            al.minutes = Int(json, "alrM", 0);
            s.alerts.push_back(al);
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = std::move(s);
            last_rx_us_ = now;
        }
        revision_ = revision_ + 1;
    } else if (t == "bye") {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = HudState();
        last_rx_us_ = now;
    } else {
        // cfg_*, error and anything newer: not used by this screen
        std::lock_guard<std::mutex> lock(mutex_);
        last_rx_us_ = now;
    }
    cJSON_Delete(json);
    return reply;
}
