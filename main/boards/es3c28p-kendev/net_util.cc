#include "net_util.h"

#include <cctype>
#include <cstdio>

#include <memory>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <http.h>

#include "board.h"

namespace zero {

static const char* TAG = "net_util";

// Minimum largest-block sizes before opening a connection. These are estimates, not measured on
// the board: with CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y the TLS record buffers come from PSRAM
// (up to ~16 KB in + ~4 KB out plus handshake state), while sockets and Wi-Fi buffers stay in
// internal RAM.
static constexpr size_t kMinInternalBlock = 6 * 1024;
static constexpr size_t kMinPsramBlock = 32 * 1024;

HeapInfo GetHeapInfo() {
    HeapInfo h;
    h.internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    h.internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    h.psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    h.psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    return h;
}

std::string HeapInfoString() {
    HeapInfo h = GetHeapInfo();
    char buf[96];
    snprintf(buf, sizeof(buf), "RAM %uK free / %uK block, PSRAM %uK free / %uK block",
             (unsigned)(h.internal_free / 1024), (unsigned)(h.internal_largest / 1024),
             (unsigned)(h.psram_free / 1024), (unsigned)(h.psram_largest / 1024));
    return buf;
}

static bool HeapReady(const HeapInfo& h) {
    if (h.internal_largest < kMinInternalBlock) return false;
    // Boards built without PSRAM report 0 here; only the internal check applies then.
    if (h.psram_free > 0 && h.psram_largest < kMinPsramBlock) return false;
    return true;
}

bool WaitForNetHeap(int max_wait_ms) {
    int waited = 0;
    while (!HeapReady(GetHeapInfo())) {
        if (waited >= max_wait_ms) {
            ESP_LOGW(TAG, "Low memory before connecting (%s)", HeapInfoString().c_str());
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        waited += 100;
    }
    if (waited > 0) ESP_LOGI(TAG, "Waited %d ms for memory before connecting", waited);
    return true;
}

bool HttpGetText(const std::string& start_url, std::string& body, std::string& error) {
    auto network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        error = "no network";
        return false;
    }
    std::string url = start_url;
    std::unique_ptr<Http> http;
    int code = 0;
    for (int hop = 0; hop < 4; hop++) {
        WaitForNetHeap();
        http = network->CreateHttp(0);
        http->SetHeader("User-Agent", "Mozilla/5.0 (xiaozhi-s3zero-oled-vn)");
        if (auto opened = http->Open("GET", url); !opened) {
            error = "open failed: " + opened.error().ToString();
            ESP_LOGW(TAG, "GET %s: %s (%s)", url.c_str(), error.c_str(), HeapInfoString().c_str());
            return false;
        }
        auto status = http->GetStatusCode();
        if (!status) {
            error = "status failed: " + status.error().ToString();
            http->Close();
            return false;
        }
        code = *status;
        if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
            std::string loc = http->GetResponseHeader("Location");
            if (loc.empty()) loc = http->GetResponseHeader("location");
            http->Close();
            if (loc.empty()) break;
            if (loc[0] == '/') {  // relative: keep scheme://host
                size_t p = url.find("://");
                size_t h = p == std::string::npos ? std::string::npos : url.find('/', p + 3);
                loc = (h == std::string::npos ? url : url.substr(0, h)) + loc;
            }
            url = loc;
            continue;
        }
        break;
    }
    if (code != 200) {
        error = "HTTP " + std::to_string(code);
        http->Close();
        return false;
    }
    body = http->ReadAll();
    http->Close();
    if (body.empty()) {
        error = "empty body";
        return false;
    }
    return true;
}

bool HttpPostForm(const std::string& url, const std::string& form, const std::string& referer,
                  std::string& body, std::string& error) {
    auto network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        error = "no network";
        return false;
    }
    WaitForNetHeap();
    auto http = network->CreateHttp(0);
    http->SetHeader("User-Agent", "Mozilla/5.0 (xiaozhi-s3zero-oled-vn)");
    http->SetHeader("Content-Type", "application/x-www-form-urlencoded; charset=UTF-8");
    http->SetHeader("X-Requested-With", "XMLHttpRequest");
    if (!referer.empty()) http->SetHeader("Referer", referer);
    http->SetContent(std::string(form));
    if (auto opened = http->Open("POST", url); !opened) {
        error = "open failed: " + opened.error().ToString();
        ESP_LOGW(TAG, "POST %s: %s (%s)", url.c_str(), error.c_str(), HeapInfoString().c_str());
        return false;
    }
    auto status = http->GetStatusCode();
    if (!status || *status != 200) {
        error = status ? "HTTP " + std::to_string(*status) : "status failed";
        http->Close();
        return false;
    }
    body = http->ReadAll();
    http->Close();
    if (body.empty()) {
        error = "empty body";
        return false;
    }
    return true;
}

std::string UrlEncode(const std::string& value) {
    std::string out;
    char buf[12];
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

}  // namespace zero
