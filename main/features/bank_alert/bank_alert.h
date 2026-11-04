#ifndef BANK_ALERT_H
#define BANK_ALERT_H

// Bank transfer alerts from phone notifications (forwarded over BLE by Chronos).
// Keeps the last transactions in RAM only (cleared on reboot) — balances are never stored.

#include <string>

namespace bank {

// Register MCP tools (self.bank.get_recent, self.bank.set_alert). Call once.
void RegisterTools();

// Feed one phone notification text ("Title: message")
void OnNotification(const std::string& text);

}  // namespace bank

#endif  // BANK_ALERT_H
