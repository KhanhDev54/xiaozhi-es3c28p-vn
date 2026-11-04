#include "vietqr.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace vietqr {

struct Bank {
    const char* bin;
    const char* name;
    const char* aliases;  // lowercase, '|' separated, no spaces/diacritics
};

// NAPAS BINs of common Vietnamese banks
static const Bank kBanks[] = {
    {"970436", "Vietcombank", "vietcombank|vcb|ngoaithuong"},
    {"970415", "VietinBank", "vietinbank|ctg|congthuong"},
    {"970418", "BIDV", "bidv|dautuphattrien"},
    {"970405", "Agribank", "agribank|nongnghiep"},
    {"970422", "MB Bank", "mb|mbbank|quandoi"},
    {"970407", "Techcombank", "techcombank|tcb|kythuong"},
    {"970416", "ACB", "acb|achau"},
    {"970432", "VPBank", "vpbank|vpb|vietnamthinhvuong"},
    {"970423", "TPBank", "tpbank|tpb|tienphong"},
    {"970403", "Sacombank", "sacombank|stb|saigonthuongtin"},
    {"970441", "VIB", "vib|quocte"},
    {"970443", "SHB", "shb|saigonhanoi"},
    {"970437", "HDBank", "hdbank|hdb|phattriennhatphcm"},
    {"970448", "OCB", "ocb|phuongdong"},
    {"970426", "MSB", "msb|maritimebank|hanghai"},
    {"970440", "SeABank", "seabank|dongnama"},
    {"970431", "Eximbank", "eximbank|eib|xuatnhapkhau"},
    {"970449", "LPBank", "lpbank|lienvietpostbank|lienviet|locphat"},
    {"970428", "Nam A Bank", "namabank|nama"},
    {"970409", "Bac A Bank", "bacabank|baca"},
    {"970425", "ABBANK", "abbank|anbinh"},
    {"970452", "Kienlongbank", "kienlongbank|kienlong|klb"},
    {"970412", "PVcomBank", "pvcombank|pvcom"},
};

uint16_t Crc16(const std::string& data) {
    uint16_t crc = 0xFFFF;
    for (unsigned char c : data) {
        crc ^= (uint16_t)c << 8;
        for (int i = 0; i < 8; i++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static std::string Tlv(const char* id, const std::string& value) {
    char len[12];
    snprintf(len, sizeof(len), "%02d", (int)value.size());
    return std::string(id) + len + value;
}

static std::string AsciiOnly(const std::string& s, size_t max_len) {
    std::string out;
    for (unsigned char c : s) {
        if (c >= 0x20 && c < 0x7F) out += (char)c;
        if (out.size() >= max_len) break;
    }
    return out;
}

std::string BuildPayload(const std::string& bin, const std::string& account, uint64_t amount,
                         const std::string& message) {
    std::string beneficiary = Tlv("00", bin) + Tlv("01", account);
    std::string merchant = Tlv("00", "A000000727") + Tlv("01", beneficiary) + Tlv("02", "QRIBFTTA");

    std::string p;
    p += Tlv("00", "01");
    p += Tlv("01", amount > 0 ? "12" : "11");
    p += Tlv("38", merchant);
    p += Tlv("53", "704");
    if (amount > 0) {
        p += Tlv("54", std::to_string(amount));
    }
    p += Tlv("58", "VN");
    std::string msg = AsciiOnly(message, 25);
    if (!msg.empty()) {
        p += Tlv("62", Tlv("08", msg));
    }
    p += "6304";
    char crc[8];
    snprintf(crc, sizeof(crc), "%04X", Crc16(p));
    return p + crc;
}

static std::string Normalize(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c)) out += (char)std::tolower(c);
    }
    return out;
}

std::string BankBin(const std::string& name_or_bin) {
    std::string key = Normalize(name_or_bin);
    if (key.size() == 6 && std::all_of(key.begin(), key.end(), ::isdigit)) {
        return key;
    }
    // Pass 1: exact alias. Pass 2: input ends with an alias (handles "Ngân hàng MB",
    // where Vietnamese letters were dropped by Normalize) — longest alias wins.
    const char* best_bin = nullptr;
    size_t best_len = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (const auto& b : kBanks) {
            std::string aliases = b.aliases;
            size_t start = 0;
            while (start <= aliases.size()) {
                size_t end = aliases.find('|', start);
                if (end == std::string::npos) end = aliases.size();
                std::string alias = aliases.substr(start, end - start);
                start = end + 1;
                if (alias.empty()) continue;
                if (pass == 0 && alias == key) return b.bin;
                if (pass == 1 && key.size() > alias.size() &&
                    key.compare(key.size() - alias.size(), alias.size(), alias) == 0 &&
                    alias.size() > best_len) {
                    best_bin = b.bin;
                    best_len = alias.size();
                }
            }
        }
    }
    return best_bin ? best_bin : "";
}

std::string BankName(const std::string& bin) {
    for (const auto& b : kBanks) {
        if (bin == b.bin) return b.name;
    }
    return bin;
}

std::string FormatVnd(uint64_t amount) {
    std::string digits = std::to_string(amount);
    std::string out;
    int n = (int)digits.size();
    for (int i = 0; i < n; i++) {
        out += digits[i];
        if ((n - i - 1) % 3 == 0 && i != n - 1) out += '.';
    }
    return out + "đ";
}

}  // namespace vietqr
