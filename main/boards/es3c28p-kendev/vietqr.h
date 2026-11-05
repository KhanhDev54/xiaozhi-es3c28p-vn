#ifndef VIETQR_H
#define VIETQR_H

// VietQR (NAPAS 247) payload builder — EMVCo Merchant-Presented QR.
// Pure C++, no ESP-IDF dependency (host-testable). MCP tools live in vietqr_tools.cc.

#include <cstdint>
#include <string>

namespace vietqr {

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF), as required by EMVCo tag 63
uint16_t Crc16(const std::string& data);

// Build the QR text. amount = 0 -> static QR (payer types the amount).
// message: transfer content (ASCII, max 25 chars kept).
std::string BuildPayload(const std::string& bank_bin, const std::string& account_number,
                         uint64_t amount = 0, const std::string& message = "");

// Map a bank name / short name ("vietcombank", "vcb", "mb", "techcombank"...) to its NAPAS BIN.
// Accepts a 6-digit BIN directly. Returns "" if unknown.
std::string BankBin(const std::string& bank_name_or_bin);

// Short display name for a BIN ("Vietcombank"), or the BIN itself if unknown.
std::string BankName(const std::string& bin);

// Bank table, for pickers
int BankCount();
const char* BankBinAt(int i);
const char* BankNameAt(int i);

// "150.000đ"
std::string FormatVnd(uint64_t amount);

}  // namespace vietqr

#endif  // VIETQR_H
