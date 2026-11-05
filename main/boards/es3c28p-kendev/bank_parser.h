#ifndef BANK_PARSER_H
#define BANK_PARSER_H

// Extract a money movement from a bank app notification text.
// Pure C++ (host-testable). Formats differ per bank, so the parser is
// heuristic: a signed amount followed by VND / đ / d, plus a banking keyword.

#include <cstdint>
#include <string>

namespace bank {

struct Movement {
    bool found = false;
    bool incoming = false;  // true = tiền vào (+), false = tiền ra (-)
    uint64_t amount = 0;    // VND
    std::string content;    // nội dung chuyển khoản (best effort, may be empty)
};

Movement Parse(const std::string& text);

// Best-effort extras from the same text
int64_t ParseBalance(const std::string& text);        // amount after "SD"/"Số dư", -1 if none
std::string ParseAccountTail(const std::string& text); // last 4 digits after "TK", "" if none
std::string GuessBank(const std::string& text);        // "Vietcombank", "" if unknown

}  // namespace bank

#endif  // BANK_PARSER_H
