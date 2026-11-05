#include "lan_server.h"

#include <cJSON.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <wifi_manager.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "bank_parser.h"
#include "alarm_manager.h"
#include "bank_store.h"
#include "vietqr.h"

#define TAG "LanServer"

static const size_t kMaxBody = 4096;

// ---------------------------------------------------------------- helpers

static std::string UrlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) &&
                   isxdigit((unsigned char)s[i + 2])) {
            out += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else {
            out += c;
        }
    }
    return out;
}

// key=value&key=value
static std::string FormValue(const std::string& form, const std::string& key) {
    size_t p = 0;
    while (p <= form.size()) {
        size_t e = form.find('&', p);
        if (e == std::string::npos) e = form.size();
        std::string pair = form.substr(p, e - p);
        size_t eq = pair.find('=');
        if (eq != std::string::npos && pair.substr(0, eq) == key) return UrlDecode(pair.substr(eq + 1));
        p = e + 1;
    }
    return "";
}

static std::string Query(httpd_req_t* req) {
    size_t len = httpd_req_get_url_query_len(req);
    if (len == 0) return "";
    std::string q(len + 1, '\0');
    if (httpd_req_get_url_query_str(req, q.data(), q.size()) != ESP_OK) return "";
    q.resize(len);
    return q;
}

static bool ReadBody(httpd_req_t* req, std::string& body) {
    if (req->content_len > kMaxBody) return false;
    body.resize(req->content_len);
    size_t got = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, body.data() + got, req->content_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) return false;
        got += r;
    }
    return true;
}

static std::string Html(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '&': o += "&amp;"; break;
            case '"': o += "&quot;"; break;
            default: o += c;
        }
    }
    return o;
}

static bool TokenOk(const std::string& t) {
    return !t.empty() && t == BankStore::GetInstance().Settings().token;
}

static std::string AsciiUpper(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c >= 0x20 && c < 0x7F) o += (char)toupper(c);
    }
    return o;
}

static const char* kHead =
    "<!doctype html><html lang=vi><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'><title>KenDev · Cài đặt loa</title>"
    "<style>body{font-family:system-ui,sans-serif;background:#0b1220;color:#f1f5f9;margin:0;padding:16px;max-width:560px}"
    "h1{font-size:22px}h2{font-size:16px;color:#60a5fa;margin-top:24px}"
    "fieldset{border:1px solid #1e293b;border-radius:12px;margin:0 0 12px;padding:12px}"
    "label{display:block;margin:8px 0 4px;color:#94a3b8;font-size:14px}"
    "input,select{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;border:1px solid #334155;"
    "background:#141c30;color:#f1f5f9;font-size:16px}"
    "input[type=checkbox]{width:auto;margin-right:8px}.row{display:flex;gap:8px}.row>*{flex:1}"
    "button{margin-top:16px;width:100%;padding:12px;border:0;border-radius:10px;background:#2563eb;color:#fff;font-size:16px}"
    "code{background:#141c30;padding:2px 6px;border-radius:6px}a{color:#60a5fa}small{color:#94a3b8}</style></head><body>";

// ---------------------------------------------------------------- pages

static std::string SetupPage(const std::string& token, const std::string& notice) {
    auto& store = BankStore::GetInstance();
    auto s = store.Settings();
    auto accounts = store.Accounts();
    auto& am = AlarmManager::GetInstance();
    std::string h = kHead;
    h += "<h1>Cài đặt loa KenDev</h1>";
    if (!notice.empty()) h += "<p style='color:#4ade80'>" + Html(notice) + "</p>";
    h += "<form method=post action='/setup'><input type=hidden name=t value='" + Html(token) + "'>";

    h += "<h2>Tài khoản nhận tiền (QR)</h2><small>Chỉ lưu trên loa. Để trống số tài khoản để xoá.</small>";
    for (int i = 0; i < 3; i++) {
        QrAccount a = i < (int)accounts.size() ? accounts[i] : QrAccount();
        std::string n = std::to_string(i);
        h += "<fieldset><label>Ngân hàng " + std::to_string(i + 1) + "</label><select name=b" + n + ">";
        for (int k = 0; k < vietqr::BankCount(); k++) {
            std::string bin = vietqr::BankBinAt(k);
            h += "<option value=" + bin + (bin == a.bin ? " selected" : "") + ">" + vietqr::BankNameAt(k) + "</option>";
        }
        h += "</select><label>Số tài khoản</label><input name=n" + n + " inputmode=numeric value='" + Html(a.number) +
             "'><label>Tên chủ tài khoản (không dấu)</label><input name=h" + n + " value='" + Html(a.holder) +
             "'></fieldset>";
    }

    h += "<h2>Màn hình ban đêm</h2><fieldset>";
    h += std::string("<label><input type=checkbox name=night value=1") + (am.night_enabled() ? " checked" : "") +
         ">Tự bật theo giờ</label>";
    h += "<div class=row><div><label>Từ (giờ)</label><input type=number min=0 max=23 name=ns value=" +
         std::to_string(am.night_start()) + "></div><div><label>Đến (giờ)</label><input type=number min=0 max=23 name=ne value=" +
         std::to_string(am.night_end()) + "></div></div></fieldset>";

    h += "<h2>Khác</h2><fieldset>";
    h += std::string("<label><input type=checkbox name=read value=1") + (s.read_tx ? " checked" : "") +
         ">Báo âm thanh khi có giao dịch</label>";
    h += std::string("<label><input type=checkbox name=home value=1") + (s.auto_home ? " checked" : "") +
         ">Tự về đồng hồ khi để yên</label></fieldset>";
    h += "<button>Lưu</button></form>";

    h += "<h2>Báo chuyển khoản qua mạng LAN</h2><p><small>Gửi <code>POST</code> tới "
         "<code>/api/tx?t=" + Html(token) + "</code> với nội dung là tin nhắn/thông báo của ngân hàng "
         "(văn bản thường), hoặc JSON <code>{\"text\":\"...\"}</code>. Dùng được với Phím tắt (Shortcuts) trên iPhone "
         "hoặc ứng dụng chuyển tiếp thông báo trên Android.</small></p>";
    h += "<p><a href='/tx.csv?t=" + Html(token) + "'>Tải lịch sử giao dịch (CSV, mở bằng Excel)</a></p>";
    h += "</body></html>";
    return h;
}

static std::string LoginPage() {
    std::string h = kHead;
    h += "<h1>Cài đặt loa KenDev</h1><p>Nhập mã hiện trên loa (Menu → Cài đặt), hoặc quét mã QR trên loa.</p>"
         "<form method=get action='/setup'><label>Mã</label><input name=t autocomplete=off><button>Mở</button></form></body></html>";
    return h;
}

static esp_err_t SendHtml(httpd_req_t* req, const std::string& html) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, html.data(), html.size());
}

static esp_err_t SendJson(httpd_req_t* req, const char* status, const std::string& json) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.data(), json.size());
}

static esp_err_t HandleRoot(httpd_req_t* req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/setup");
    return httpd_resp_send(req, nullptr, 0);
}

static esp_err_t HandleSetupGet(httpd_req_t* req) {
    std::string t = FormValue(Query(req), "t");
    if (!TokenOk(t)) return SendHtml(req, LoginPage());
    return SendHtml(req, SetupPage(t, ""));
}

static esp_err_t HandleSetupPost(httpd_req_t* req) {
    std::string body;
    if (!ReadBody(req, body)) return SendJson(req, "400 Bad Request", "{\"ok\":false}");
    std::string t = FormValue(body, "t");
    if (!TokenOk(t)) return SendHtml(req, LoginPage());

    auto& store = BankStore::GetInstance();
    std::vector<QrAccount> list;
    for (int i = 0; i < 3; i++) {
        std::string n = std::to_string(i);
        QrAccount a;
        a.bin = FormValue(body, "b" + n);
        for (char c : FormValue(body, "n" + n)) {
            if (isdigit((unsigned char)c)) a.number += c;
        }
        a.holder = AsciiUpper(FormValue(body, "h" + n));
        if (a.bin.size() == 6 && a.number.size() >= 4) list.push_back(a);
    }
    store.SetAccounts(list);

    auto s = store.Settings();
    AlarmManager::GetInstance().SetNightMode(FormValue(body, "night") == "1",
                                             atoi(FormValue(body, "ns").c_str()) % 24,
                                             atoi(FormValue(body, "ne").c_str()) % 24);
    s.read_tx = FormValue(body, "read") == "1";
    s.auto_home = FormValue(body, "home") == "1";
    store.SaveSettings(s);
    LanServer::GetInstance().NotifySettingsChanged();
    ESP_LOGI(TAG, "Settings saved from LAN (%u accounts)", (unsigned)list.size());
    return SendHtml(req, SetupPage(t, "Đã lưu."));
}

static esp_err_t HandleTx(httpd_req_t* req) {
    std::string t = FormValue(Query(req), "t");
    std::string body;
    if (!ReadBody(req, body)) return SendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"body\"}");

    BankTx tx;
    std::string text = body;
    int64_t amount = 0;
    bool has_amount = false;
    cJSON* json = cJSON_Parse(body.c_str());
    if (cJSON_IsObject(json)) {
        auto str = [json](const char* k) {
            cJSON* v = cJSON_GetObjectItem(json, k);
            return cJSON_IsString(v) ? std::string(v->valuestring) : std::string();
        };
        if (t.empty()) t = str("token");
        text = str("text");
        tx.bank = str("bank");
        tx.name = AsciiUpper(str("name"));
        tx.content = str("content");
        tx.account = str("account");
        cJSON* a = cJSON_GetObjectItem(json, "amount");
        if (cJSON_IsNumber(a)) {
            amount = (int64_t)a->valuedouble;
            has_amount = true;
        }
        cJSON* b = cJSON_GetObjectItem(json, "balance");
        if (cJSON_IsNumber(b)) tx.balance = (int64_t)b->valuedouble;
    }
    cJSON_Delete(json);
    if (!TokenOk(t)) return SendJson(req, "403 Forbidden", "{\"ok\":false,\"error\":\"token\"}");

    if (!has_amount) {
        auto m = bank::Parse(text);
        if (!m.found) return SendJson(req, "422 Unprocessable Entity", "{\"ok\":false,\"error\":\"no amount\"}");
        amount = m.incoming ? (int64_t)m.amount : -(int64_t)m.amount;
        if (tx.content.empty()) tx.content = m.content;
    }
    if (tx.bank.empty()) tx.bank = bank::GuessBank(text);
    if (tx.bank.empty()) tx.bank = "Ngân hàng";
    if (tx.account.empty()) tx.account = bank::ParseAccountTail(text);
    if (tx.balance < 0) tx.balance = bank::ParseBalance(text);
    tx.amount = amount;
    tx.raw = text;
    tx.source = "LAN";
    BankStore::GetInstance().Add(tx);
    return SendJson(req, "200 OK", "{\"ok\":true,\"amount\":" + std::to_string((long long)amount) + "}");
}

static esp_err_t HandleCsv(httpd_req_t* req) {
    std::string t = FormValue(Query(req), "t");
    if (!TokenOk(t)) return SendJson(req, "403 Forbidden", "{\"ok\":false,\"error\":\"token\"}");
    std::string csv = BankStore::GetInstance().Csv();
    httpd_resp_set_type(req, "text/csv; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"lich-su-giao-dich.csv\"");
    return httpd_resp_send(req, csv.data(), csv.size());
}

// ---------------------------------------------------------------- lifecycle

void LanServer::EnsureStarted() {
    if (server_ != nullptr) return;
    auto& wifi = WifiManager::GetInstance();
    if (!wifi.IsConnected()) return;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 8;
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    httpd_handle_t handle = nullptr;
    esp_err_t err = httpd_start(&handle, &config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return;
    }
    const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = HandleRoot, .user_ctx = nullptr},
        {.uri = "/setup", .method = HTTP_GET, .handler = HandleSetupGet, .user_ctx = nullptr},
        {.uri = "/setup", .method = HTTP_POST, .handler = HandleSetupPost, .user_ctx = nullptr},
        {.uri = "/api/tx", .method = HTTP_POST, .handler = HandleTx, .user_ctx = nullptr},
        {.uri = "/tx.csv", .method = HTTP_GET, .handler = HandleCsv, .user_ctx = nullptr},
    };
    for (const auto& u : uris) httpd_register_uri_handler(handle, &u);
    server_ = handle;
    ESP_LOGI(TAG, "LAN server on http://%s/setup", wifi.GetIpAddress().c_str());
}

std::string LanServer::SetupUrl() {
    if (server_ == nullptr) return "";
    return "http://" + WifiManager::GetInstance().GetIpAddress() + "/setup?t=" + BankStore::GetInstance().Settings().token;
}
