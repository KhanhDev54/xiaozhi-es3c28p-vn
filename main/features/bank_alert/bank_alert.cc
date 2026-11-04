#include "bank_alert.h"
#include "bank_parser.h"

#include <cJSON.h>
#include <esp_log.h>
#include <ctime>
#include <deque>
#include <mutex>

#include "application.h"
#include "assets/lang_config.h"
#include "board.h"
#include "display.h"
#include "mcp_server.h"
#include "settings.h"
#include "features/alarm_clock/alarm_manager.h"
#include "features/vietqr/vietqr.h"

#define TAG "BankAlert"

namespace bank {

struct Record {
    time_t when;
    bool incoming;
    uint64_t amount;
    std::string content;
};

static std::mutex s_mutex;
static std::deque<Record> s_history;  // newest first
static const size_t kMaxHistory = 20;

static bool AlertEnabled(bool incoming) {
    Settings s("bank", false);
    if (!s.GetBool("enabled", true)) return false;
    return incoming || s.GetBool("outgoing", false);
}

void OnNotification(const std::string& text) {
    Movement m = Parse(text);
    if (!m.found) return;

    Record r{time(nullptr), m.incoming, m.amount, m.content};
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_history.push_front(r);
        if (s_history.size() > kMaxHistory) s_history.pop_back();
    }
    // Log only the amount, never the full text (it contains balances)
    ESP_LOGI(TAG, "%s %llu VND", m.incoming ? "+" : "-", (unsigned long long)m.amount);

    if (!AlertEnabled(m.incoming)) return;

    std::string line = std::string(m.incoming ? "+" : "-") + vietqr::FormatVnd(m.amount);
    if (!m.content.empty()) line += " · " + m.content;
    bool incoming = m.incoming;

    AlarmManager::GetInstance().WakeScreen(10);
    Application::GetInstance().Schedule([line, incoming]() {
        auto& app = Application::GetInstance();
        if (incoming) {
            app.PlaySound(Lang::Sounds::OGG_SUCCESS);  // "ting"
        }
        auto display = Board::GetInstance().GetDisplay();
        if (display) display->ShowNotification(line, 10000);
    });
}

void RegisterTools() {
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool("self.bank.get_recent",
        "Xem các giao dịch ngân hàng gần đây mà thiết bị nhận được từ thông báo điện thoại\n"
        "(qua app Chronos). Dùng khi người dùng hỏi: 'vừa nhận bao nhiêu tiền', 'hôm nay nhận được bao nhiêu',\n"
        "'giao dịch gần nhất'. Chỉ có dữ liệu từ lúc thiết bị bật, tối đa 20 giao dịch.\n"
        "Đọc số tiền bằng tiếng Việt tự nhiên (ví dụ 150000 -> 'một trăm năm mươi nghìn đồng').",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            std::lock_guard<std::mutex> lock(s_mutex);
            time_t now = time(nullptr);
            struct tm today;
            localtime_r(&now, &today);
            uint64_t in_today = 0, out_today = 0;
            cJSON* arr = cJSON_CreateArray();
            for (const auto& r : s_history) {
                struct tm t;
                localtime_r(&r.when, &t);
                bool same_day = t.tm_year == today.tm_year && t.tm_yday == today.tm_yday;
                if (same_day) (r.incoming ? in_today : out_today) += r.amount;
                char hhmm[8];
                strftime(hhmm, sizeof(hhmm), "%H:%M", &t);
                cJSON* o = cJSON_CreateObject();
                cJSON_AddStringToObject(o, "time", hhmm);
                cJSON_AddStringToObject(o, "type", r.incoming ? "in" : "out");
                cJSON_AddNumberToObject(o, "amount_vnd", (double)r.amount);
                cJSON_AddStringToObject(o, "content", r.content.c_str());
                cJSON_AddItemToArray(arr, o);
            }
            cJSON* root = cJSON_CreateObject();
            cJSON_AddItemToObject(root, "transactions", arr);
            cJSON_AddNumberToObject(root, "total_in_today_vnd", (double)in_today);
            cJSON_AddNumberToObject(root, "total_out_today_vnd", (double)out_today);
            return root;
        });

    mcp.AddTool("self.bank.set_alert",
        "Bật/tắt báo chuyển khoản trên loa (tiếng 'ting' + hiện số tiền).\n"
        "enabled: bật/tắt toàn bộ; outgoing: có báo cả tiền ra (trừ tiền) không.",
        PropertyList({
            Property("enabled", kPropertyTypeBoolean),
            Property("outgoing", kPropertyTypeBoolean, false),
        }),
        [](const PropertyList& p) -> ReturnValue {
            Settings s("bank", true);
            s.SetBool("enabled", p["enabled"].value<bool>());
            s.SetBool("outgoing", p["outgoing"].value<bool>());
            return true;
        });
}

}  // namespace bank
