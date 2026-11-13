#ifndef KENDEV_BANK_STORE_H
#define KENDEV_BANK_STORE_H

// Bank movements received by the speaker (LAN /api/tx), VietQR accounts and the
// KenDev device settings. Everything lives in NVS (namespace "kendev"); bank
// account numbers never go into the repo.

#include <cstdint>
#include <ctime>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

struct BankTx {
    int64_t ts = 0;          // unix time
    int64_t amount = 0;      // VND, signed (+ in, - out)
    int64_t balance = -1;    // balance after, -1 = unknown
    std::string bank;        // "Vietcombank"
    std::string account;     // last digits, "6789"
    std::string name;        // sender (incoming only)
    std::string content;     // transfer content / reference
    std::string raw;         // original notification text (trimmed)
    std::string source;      // "LAN", "Máy chủ"
};

struct QrAccount {
    std::string bin;         // NAPAS BIN
    std::string number;
    std::string holder;      // ASCII, upper case
};

struct KendevSettings {
    bool auto_home = true;   // return to the clock after an app is idle
    bool read_tx = true;     // "ting" + read incoming amounts aloud
    bool rotate180 = false;
    int led_color = 0;       // index into the control panel palette, 0 = off
    std::string token;       // LAN API token
};

class BankStore {
public:
    static BankStore& GetInstance() {
        static BankStore instance;
        return instance;
    }

    void Load();

    // Transactions (newest first)
    void Add(const BankTx& tx);
    std::vector<BankTx> List(size_t max = 0);
    bool Latest(BankTx& out);
    int Revision() const { return revision_; }  // bumps on every Add
    bool unread() const { return unread_; }
    void MarkRead() { unread_ = false; }
    // Sum of + and - amounts between [from, to)
    void Totals(int64_t from, int64_t to, int64_t& in, int64_t& out, int& count);
    std::string Csv();
    void ClearAll();

    // Called (from the caller's task) after Add
    void SetListener(std::function<void(const BankTx&)> cb) { listener_ = std::move(cb); }

    // VietQR accounts (max 3)
    std::vector<QrAccount> Accounts();
    void SetAccounts(const std::vector<QrAccount>& list);

    KendevSettings Settings();
    void SaveSettings(const KendevSettings& s);

private:
    BankStore() = default;
    void SaveTx();

    std::mutex mutex_;
    bool loaded_ = false;
    std::vector<BankTx> tx_;
    std::vector<QrAccount> accounts_;
    KendevSettings settings_;
    std::function<void(const BankTx&)> listener_;
    volatile int revision_ = 0;
    volatile bool unread_ = false;
};

// "150.000" (no currency sign), sign handled by caller
std::string FormatThousands(int64_t v);

#endif  // KENDEV_BANK_STORE_H
