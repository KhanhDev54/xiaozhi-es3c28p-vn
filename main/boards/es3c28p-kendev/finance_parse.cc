#include "finance_parse.h"

#include <cstdlib>

namespace zero {

static std::string Attr(const std::string& tag, const char* name) {
    std::string key = std::string(name) + "=\"";
    size_t p = tag.find(key);
    if (p == std::string::npos) return "";
    p += key.size();
    size_t e = tag.find('"', p);
    if (e == std::string::npos) return "";
    return tag.substr(p, e - p);
}

double ParseVcbNumber(const std::string& s) {
    std::string digits;
    for (char c : s) {
        if ((c >= '0' && c <= '9') || c == '.') digits += c;
    }
    return digits.empty() ? 0 : atof(digits.c_str());
}

std::vector<ExRate> ParseVcbXml(const std::string& xml, std::string* datetime) {
    std::vector<ExRate> out;
    if (datetime) {
        size_t a = xml.find("<DateTime>");
        size_t b = xml.find("</DateTime>");
        if (a != std::string::npos && b != std::string::npos && b > a) {
            *datetime = xml.substr(a + 10, b - a - 10);
        }
    }
    size_t pos = 0;
    while ((pos = xml.find("<Exrate ", pos)) != std::string::npos) {
        size_t end = xml.find('>', pos);
        if (end == std::string::npos) break;
        std::string tag = xml.substr(pos, end - pos);
        ExRate r;
        r.code = Attr(tag, "CurrencyCode");
        r.name = Attr(tag, "CurrencyName");
        // trim trailing spaces in the name
        while (!r.name.empty() && r.name.back() == ' ') r.name.pop_back();
        r.buy_cash = ParseVcbNumber(Attr(tag, "Buy"));
        r.buy_transfer = ParseVcbNumber(Attr(tag, "Transfer"));
        r.sell = ParseVcbNumber(Attr(tag, "Sell"));
        if (!r.code.empty()) out.push_back(r);
        pos = end;
    }
    return out;
}

}  // namespace zero
