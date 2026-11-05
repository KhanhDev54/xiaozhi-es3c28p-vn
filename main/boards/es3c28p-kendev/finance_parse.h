#ifndef ZERO_FINANCE_PARSE_H
#define ZERO_FINANCE_PARSE_H

// Parsers for the gold price / exchange rate tools (host-testable, no ESP deps).

#include <string>
#include <vector>

namespace zero {

struct ExRate {
    std::string code;      // "USD"
    std::string name;      // "US DOLLAR"
    double buy_cash = 0;   // VND, 0 = not quoted
    double buy_transfer = 0;
    double sell = 0;
};

// Vietcombank XML (portal.vietcombank.com.vn/Usercontrols/TVPortal.TyGia/pXML.aspx):
// <ExrateList><DateTime>11/5/2026 5:42:39 PM</DateTime>
//   <Exrate CurrencyCode="USD" CurrencyName="US DOLLAR" Buy="25,390.00" Transfer="25,420.00" Sell="25,800.00" />
std::vector<ExRate> ParseVcbXml(const std::string& xml, std::string* datetime);

// "25,390.00" -> 25390; "-" -> 0
double ParseVcbNumber(const std::string& s);

}  // namespace zero

#endif
