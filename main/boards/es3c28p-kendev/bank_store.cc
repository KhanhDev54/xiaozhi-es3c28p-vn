#include "bank_store.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_random.h>
#include <nvs.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "settings.h"

#define TAG "BankStore"

static const char* kNs = "kendev";
static const size_t kMaxTx = 120;
static const size_t kMaxRaw = 120;

std::string FormatThousands(int64_t v) {
    if (v < 0) v = -v;
    std::string s = std::to_string((long long)v), out;
    int n = (int)s.size();
    for (int i = 0; i < n; i++) {
        out += s[i];
        int left = n - 1 - i;
        if (left > 0 && left % 3 == 0) out += '.';
    }
    return out;
}

static std::string Str(cJSON* o, const char* key) {
    cJSON* v = cJSON_GetObjectItem(o, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}

static int64_t Num(cJSON* o, const char* key, int64_t def) {
    cJSON* v = cJSON_GetObjectItem(o, key);
    return cJSON_IsNumber(v) ? (int64_t)v->valuedouble : def;
}

void BankStore::Load() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (loaded_) return;
    loaded_ = true;

    {
        ::Settings s(kNs, true);
        settings_.auto_home = s.GetBool("auto_home", true);
        settings_.read_tx = s.GetBool("read_tx", true);
        settings_.rotate180 = s.GetBool("rot180", false);
        settings_.led_color = s.GetInt("led", 0);
        settings_.token = s.GetString("token", "");
        if (settings_.token.empty()) {
            char buf[12];
            snprintf(buf, sizeof(buf), "%08lx", (unsigned long)esp_random());
            settings_.token = buf;
            s.SetString("token", settings_.token);
        }

        std::string acc = s.GetString("qr_acc", "[]");
        cJSON* arr = cJSON_Parse(acc.c_str());
        if (cJSON_IsArray(arr)) {
            cJSON* it;
            cJSON_ArrayForEach(it, arr) {
                QrAccount a;
                a.bin = Str(it, "b");
                a.number = Str(it, "n");
                a.holder = Str(it, "h");
                if (!a.bin.empty() && !a.number.empty()) accounts_.push_back(a);
            }
        }
        cJSON_Delete(arr);
    }

    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
        size_t len = 0;
        if (nvs_get_blob(h, "tx", nullptr, &len) == ESP_OK && len > 0) {
            std::string buf(len, '\0');
            if (nvs_get_blob(h, "tx", buf.data(), &len) == ESP_OK) {
                cJSON* arr = cJSON_Parse(buf.c_str());
                if (cJSON_IsArray(arr)) {
                    cJSON* it;
                    cJSON_ArrayForEach(it, arr) {
                        BankTx t;
                        t.ts = Num(it, "t", 0);
                        t.amount = Num(it, "a", 0);
                        t.balance = Num(it, "s", -1);
                        t.bank = Str(it, "b");
                        t.account = Str(it, "k");
                        t.name = Str(it, "n");
                        t.content = Str(it, "c");
                        t.raw = Str(it, "r");
                        t.source = Str(it, "o");
                        tx_.push_back(t);
                    }
                }
                cJSON_Delete(arr);
            }
        }
        nvs_close(h);
    }
    ESP_LOGI(TAG, "Loaded %u transactions, %u QR accounts", (unsigned)tx_.size(), (unsigned)accounts_.size());
}

void BankStore::SaveTx() {
    cJSON* arr = cJSON_CreateArray();
    for (const auto& t : tx_) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "t", (double)t.ts);
        cJSON_AddNumberToObject(o, "a", (double)t.amount);
        if (t.balance >= 0) cJSON_AddNumberToObject(o, "s", (double)t.balance);
        cJSON_AddStringToObject(o, "b", t.bank.c_str());
        if (!t.account.empty()) cJSON_AddStringToObject(o, "k", t.account.c_str());
        if (!t.name.empty()) cJSON_AddStringToObject(o, "n", t.name.c_str());
        if (!t.content.empty()) cJSON_AddStringToObject(o, "c", t.content.c_str());
        if (!t.raw.empty()) cJSON_AddStringToObject(o, "r", t.raw.c_str());
        if (!t.source.empty()) cJSON_AddStringToObject(o, "o", t.source.c_str());
        cJSON_AddItemToArray(arr, o);
    }
    char* json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (!json) return;
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READWRITE, &h) == ESP_OK) {
        esp_err_t err = nvs_set_blob(h, "tx", json, strlen(json) + 1);
        if (err != ESP_OK) ESP_LOGW(TAG, "Save tx failed: %s", esp_err_to_name(err));
        nvs_commit(h);
        nvs_close(h);
    }
    cJSON_free(json);
}

void BankStore::Add(const BankTx& in) {
    BankTx tx = in;
    if (tx.ts == 0) tx.ts = time(nullptr);
    if (tx.raw.size() > kMaxRaw) tx.raw = tx.raw.substr(0, kMaxRaw);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tx_.insert(tx_.begin(), tx);
        if (tx_.size() > kMaxTx) tx_.resize(kMaxTx);
        SaveTx();
        revision_ = revision_ + 1;
        unread_ = true;
    }
    ESP_LOGI(TAG, "New tx %lld %s", (long long)tx.amount, tx.bank.c_str());
    if (listener_) listener_(tx);
}

std::vector<BankTx> BankStore::List(size_t max) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (max == 0 || max >= tx_.size()) return tx_;
    return std::vector<BankTx>(tx_.begin(), tx_.begin() + max);
}

bool BankStore::Latest(BankTx& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (tx_.empty()) return false;
    out = tx_.front();
    return true;
}

void BankStore::Totals(int64_t from, int64_t to, int64_t& in, int64_t& out, int& count) {
    std::lock_guard<std::mutex> lock(mutex_);
    in = out = 0;
    count = 0;
    for (const auto& t : tx_) {
        if (t.ts < from || t.ts >= to) continue;
        count++;
        if (t.amount >= 0) in += t.amount;
        else out += -t.amount;
    }
}

static std::string CsvField(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"') o += "\"\"";
        else if (c == '\n' || c == '\r') o += ' ';
        else o += c;
    }
    return o + "\"";
}

std::string BankStore::Csv() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string out = "\xEF\xBB\xBFThoi gian,So tien,Ngan hang,Tai khoan,Nguoi gui,Noi dung,So du sau,Nguon\r\n";
    for (const auto& t : tx_) {
        time_t ts = (time_t)t.ts;
        struct tm tmv;
        localtime_r(&ts, &tmv);
        char when[32];
        strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &tmv);
        out += std::string(when) + "," + std::to_string((long long)t.amount) + "," + CsvField(t.bank) + "," +
               CsvField(t.account) + "," + CsvField(t.name) + "," + CsvField(t.content) + "," +
               (t.balance >= 0 ? std::to_string((long long)t.balance) : "") + "," + CsvField(t.source) + "\r\n";
    }
    return out;
}

void BankStore::ClearAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    tx_.clear();
    SaveTx();
    revision_ = revision_ + 1;
    unread_ = false;
}

std::vector<QrAccount> BankStore::Accounts() {
    std::lock_guard<std::mutex> lock(mutex_);
    return accounts_;
}

void BankStore::SetAccounts(const std::vector<QrAccount>& list) {
    std::lock_guard<std::mutex> lock(mutex_);
    accounts_.clear();
    cJSON* arr = cJSON_CreateArray();
    for (const auto& a : list) {
        if (a.bin.empty() || a.number.empty()) continue;
        if (accounts_.size() >= 3) break;
        accounts_.push_back(a);
        cJSON* o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "b", a.bin.c_str());
        cJSON_AddStringToObject(o, "n", a.number.c_str());
        cJSON_AddStringToObject(o, "h", a.holder.c_str());
        cJSON_AddItemToArray(arr, o);
    }
    char* json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (json) {
        ::Settings s(kNs, true);
        s.SetString("qr_acc", json);
        cJSON_free(json);
    }
}

KendevSettings BankStore::Settings() {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_;
}

void BankStore::SaveSettings(const KendevSettings& v) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = v;
    ::Settings s(kNs, true);
    s.SetBool("auto_home", v.auto_home);
    s.SetBool("read_tx", v.read_tx);
    s.SetBool("rot180", v.rotate180);
    s.SetInt("led", v.led_color);
    if (!v.token.empty()) s.SetString("token", v.token);
}
