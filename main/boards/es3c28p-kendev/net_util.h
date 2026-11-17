#ifndef ZERO_NET_UTIL_H
#define ZERO_NET_UTIL_H

#include <cstddef>
#include <string>

namespace zero {

// Free space and largest contiguous block of internal RAM and PSRAM.
struct HeapInfo {
    size_t internal_free;
    size_t internal_largest;
    size_t psram_free;
    size_t psram_largest;
};
HeapInfo GetHeapInfo();
std::string HeapInfoString();

// Call before opening an HTTP(S) connection. A TLS handshake needs one contiguous block, and
// free memory can be split into small pieces right after the previous connection closed, so
// this waits (up to max_wait_ms) for the largest blocks to grow back. Returns false if they
// are still short; callers go ahead anyway, but the log then shows memory, not the network,
// as the likely cause of a failure.
bool WaitForNetHeap(int max_wait_ms = 1000);

// GET a URL and return the body. Returns false (and fills error) on failure.
bool HttpGetText(const std::string& url, std::string& body, std::string& error);

// POST an application/x-www-form-urlencoded body. referer may be empty.
bool HttpPostForm(const std::string& url, const std::string& form, const std::string& referer,
                  std::string& body, std::string& error);

std::string UrlEncode(const std::string& value);

}  // namespace zero

#endif  // ZERO_NET_UTIL_H
