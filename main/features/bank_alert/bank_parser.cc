#include "bank_parser.h"

#include <cctype>
#include <cstring>

namespace bank {

static std::string Lower(const std::string& s) {
    std::string out = s;
    for (auto& c : out) {
        if ((unsigned char)c < 0x80) c = (char)std::tolower((unsigned char)c);
    }
    return out;
}

static bool HasBankKeyword(const std::string& lower) {
    static const char* kKeys[] = {
        "số dư", "so du", "sd:", "sd ", "biến động", "bien dong", "tk ", "tk:", "tài khoản",
        "tai khoan", "gd:", "gd ", "giao dịch", "giao dich", "nhận", "nhan tien", "ghi có",
        "ghi co", "chuyển tiền", "chuyen tien", "nd:", "nội dung", "noi dung", "balance",
        "account", "transaction", "credit", "debit",
    };
    for (const char* k : kKeys) {
        if (lower.find(k) != std::string::npos) return true;
    }
    return false;
}

// Currency suffix right after an amount (spaces already skipped)
static size_t CurrencyLen(const std::string& lower, size_t pos) {
    if (lower.compare(pos, 3, "vnd") == 0) return 3;
    if (lower.compare(pos, 2, "vn") == 0 &&
        (lower.compare(pos + 2, 2, "\xc4\x91") == 0 || lower.compare(pos + 2, 2, "\xc4\x90") == 0)) {
        return 4;  // "VNĐ" / "vnđ" ('đ' is 2 bytes in UTF-8)
    }
    if (lower.compare(pos, 2, "\xc4\x91") == 0) return 2;  // đ
    if (lower.compare(pos, 2, "\xc4\x90") == 0) return 2;  // Đ
    if (pos < lower.size() && lower[pos] == 'd') {
        // plain 'd' only if not followed by a letter (avoid "dong", "du")
        if (pos + 1 >= lower.size() || !std::isalpha((unsigned char)lower[pos + 1])) return 1;
        if (lower.compare(pos, 4, "dong") == 0) return 4;
    }
    return 0;
}

// Parse digits with '.' or ',' thousand separators. Returns digits consumed (0 = none).
static size_t ParseNumber(const std::string& s, size_t pos, uint64_t& value) {
    size_t i = pos;
    value = 0;
    int digits = 0;
    while (i < s.size()) {
        char c = s[i];
        if (c >= '0' && c <= '9') {
            if (digits < 15) value = value * 10 + (uint64_t)(c - '0');
            digits++;
            i++;
        } else if ((c == '.' || c == ',') && digits > 0 && i + 1 < s.size() &&
                   std::isdigit((unsigned char)s[i + 1])) {
            // thousand separator only when followed by exactly 3 digits
            size_t j = i + 1;
            int k = 0;
            while (j < s.size() && std::isdigit((unsigned char)s[j])) { j++; k++; }
            if (k != 3) break;
            i++;
        } else {
            break;
        }
    }
    return digits > 0 ? i - pos : 0;
}

static std::string ExtractContent(const std::string& text) {
    static const char* kMarks[] = {"ND:", "Nd:", "nd:", "Nội dung:", "Noi dung:", "ND ", "Content:", "Mô tả:", "Mo ta:"};
    for (const char* m : kMarks) {
        size_t p = text.find(m);
        // must start a word (avoid matching "ND " inside "VND ")
        while (p != std::string::npos && p > 0 && std::isalpha((unsigned char)text[p - 1])) {
            p = text.find(m, p + 1);
        }
        if (p == std::string::npos) continue;
        p += strlen(m);
        while (p < text.size() && text[p] == ' ') p++;
        size_t e = p;
        while (e < text.size() && text[e] != '\n' && text[e] != '|' && text[e] != ';') e++;
        std::string c = text.substr(p, e - p);
        while (!c.empty() && (c.back() == ' ' || c.back() == '.')) c.pop_back();
        if (c.size() > 80) c = c.substr(0, 80);
        return c;
    }
    return "";
}

Movement Parse(const std::string& text) {
    Movement m;
    std::string lower = Lower(text);
    if (!HasBankKeyword(lower)) return m;

    for (size_t i = 0; i < lower.size(); i++) {
        char sign = lower[i];
        if (sign != '+' && sign != '-') continue;
        size_t p = i + 1;
        while (p < lower.size() && lower[p] == ' ') p++;
        uint64_t value = 0;
        size_t n = ParseNumber(lower, p, value);
        if (n == 0) continue;
        size_t q = p + n;
        while (q < lower.size() && lower[q] == ' ') q++;
        if (CurrencyLen(lower, q) == 0) continue;
        if (value == 0) continue;
        m.found = true;
        m.incoming = sign == '+';
        m.amount = value;
        m.content = ExtractContent(text);
        return m;
    }
    return m;
}

}  // namespace bank
