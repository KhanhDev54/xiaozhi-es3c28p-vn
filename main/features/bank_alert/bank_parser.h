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

}  // namespace bank

#endif  // BANK_PARSER_H
