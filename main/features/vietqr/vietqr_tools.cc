#include "vietqr_tools.h"
#include "vietqr.h"

#include <cJSON.h>
#include <esp_log.h>

#include "mcp_server.h"
#include "settings.h"
#include "features/QRCode/qrcode_display.h"

#define TAG "VietQR"

namespace vietqr {

static const char* kNs = "vietqr";

void RegisterTools() {
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool("self.vietqr.set_account",
        "Lưu tài khoản ngân hàng để tạo mã QR nhận tiền (VietQR).\n"
        "Dùng khi người dùng nói: 'đặt tài khoản nhận tiền', 'lưu số tài khoản', 'cài QR nhận tiền'.\n"
        "Hỏi lại người dùng nếu thiếu ngân hàng hoặc số tài khoản. KHÔNG tự bịa số tài khoản.\n"
        "Args:\n"
        "  bank: tên ngắn của ngân hàng KHÔNG dấu, ví dụ 'Vietcombank', 'MB', 'Techcombank', 'BIDV',\n"
        "        'VietinBank', 'Agribank', 'ACB', 'VPBank', 'TPBank', 'Sacombank'... hoặc mã BIN 6 số\n"
        "  account_number: số tài khoản (chỉ chữ số)\n"
        "  account_name: tên chủ tài khoản, IN HOA KHÔNG DẤU (tùy chọn)",
        PropertyList({
            Property("bank", kPropertyTypeString),
            Property("account_number", kPropertyTypeString),
            Property("account_name", kPropertyTypeString, ""),
        }),
        [](const PropertyList& props) -> ReturnValue {
            std::string bank = props["bank"].value<std::string>();
            std::string account = props["account_number"].value<std::string>();
            std::string name = props["account_name"].value<std::string>();
            std::string digits;
            for (char c : account) {
                if (c >= '0' && c <= '9') digits += c;
            }
            std::string bin = BankBin(bank);
            if (bin.empty()) {
                return "{\"success\": false, \"message\": \"Không nhận ra ngân hàng. Hãy dùng tên ngắn không dấu như Vietcombank, MB, Techcombank hoặc mã BIN 6 số.\"}";
            }
            if (digits.size() < 6 || digits.size() > 19) {
                return "{\"success\": false, \"message\": \"Số tài khoản không hợp lệ.\"}";
            }
            Settings s(kNs, true);
            s.SetString("bin", bin);
            s.SetString("account", digits);
            s.SetString("name", name);
            ESP_LOGI(TAG, "Saved account %s / %s", BankName(bin).c_str(), digits.c_str());
            return "{\"success\": true, \"message\": \"Đã lưu tài khoản " + BankName(bin) + " " + digits + "\"}";
        });

    mcp.AddTool("self.vietqr.show",
        "Hiện mã QR nhận tiền (VietQR) trên màn hình để người khác quét chuyển khoản.\n"
        "Dùng khi người dùng nói: 'hiện QR nhận tiền', 'mã QR chuyển khoản', 'cho mình QR 50 nghìn'.\n"
        "Args:\n"
        "  amount: số tiền VND (số nguyên, 0 = để người chuyển tự nhập). Ví dụ '50 nghìn' = 50000, '1 triệu 2' = 1200000\n"
        "  message: nội dung chuyển khoản, KHÔNG DẤU, tối đa 25 ký tự (tùy chọn)\n"
        "Mã QR tự ẩn khi người dùng bắt đầu nói chuyện lại.",
        PropertyList({
            Property("amount", kPropertyTypeInteger, 0, 0, 2000000000),
            Property("message", kPropertyTypeString, ""),
        }),
        [](const PropertyList& props) -> ReturnValue {
            Settings s(kNs, false);
            std::string bin = s.GetString("bin");
            std::string account = s.GetString("account");
            std::string name = s.GetString("name");
            if (bin.empty() || account.empty()) {
                return "{\"success\": false, \"message\": \"Chưa có tài khoản nhận tiền. Hãy hỏi người dùng ngân hàng và số tài khoản rồi gọi self.vietqr.set_account.\"}";
            }
            int amount = props["amount"].value<int>();
            std::string message = props["message"].value<std::string>();
            std::string payload = BuildPayload(bin, account, amount > 0 ? (uint64_t)amount : 0, message);

            std::string caption = BankName(bin) + " " + account;
            if (amount > 0) caption += " · " + FormatVnd((uint64_t)amount);
            bool ok = qrcode::QRCodeDisplay::GetInstance().Show(payload, caption);
            ESP_LOGI(TAG, "Show QR (%s): %s", ok ? "ok" : "fail", payload.c_str());

            cJSON* json = cJSON_CreateObject();
            cJSON_AddBoolToObject(json, "success", ok);
            cJSON_AddStringToObject(json, "bank", BankName(bin).c_str());
            cJSON_AddStringToObject(json, "account_number", account.c_str());
            cJSON_AddStringToObject(json, "account_name", name.c_str());
            if (amount > 0) cJSON_AddStringToObject(json, "amount", FormatVnd((uint64_t)amount).c_str());
            return json;
        });

    mcp.AddTool("self.vietqr.get_account",
        "Cho biết tài khoản nhận tiền (VietQR) đang lưu trên thiết bị.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            Settings s(kNs, false);
            std::string bin = s.GetString("bin");
            cJSON* json = cJSON_CreateObject();
            cJSON_AddBoolToObject(json, "configured", !bin.empty());
            if (!bin.empty()) {
                cJSON_AddStringToObject(json, "bank", BankName(bin).c_str());
                cJSON_AddStringToObject(json, "account_number", s.GetString("account").c_str());
                cJSON_AddStringToObject(json, "account_name", s.GetString("name").c_str());
            }
            return json;
        });

    mcp.AddTool("self.vietqr.clear_account",
        "Xoá tài khoản nhận tiền (VietQR) đã lưu trên thiết bị. Chỉ dùng khi người dùng yêu cầu rõ ràng.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            Settings s(kNs, true);
            s.SetString("bin", "");
            s.SetString("account", "");
            s.SetString("name", "");
            return "{\"success\": true}";
        });
}

}  // namespace vietqr
