#include "net_util.h"

#include <cctype>
#include <cstdio>

#include <memory>

#include <http.h>

#include "board.h"

namespace zero {

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
        http = network->CreateHttp(0);
        http->SetHeader("User-Agent", "Mozilla/5.0 (xiaozhi-s3zero-oled-vn)");
        if (auto opened = http->Open("GET", url); !opened) {
            error = "open failed: " + opened.error().ToString();
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
    auto http = network->CreateHttp(0);
    http->SetHeader("User-Agent", "Mozilla/5.0 (xiaozhi-s3zero-oled-vn)");
    http->SetHeader("Content-Type", "application/x-www-form-urlencoded; charset=UTF-8");
    http->SetHeader("X-Requested-With", "XMLHttpRequest");
    if (!referer.empty()) http->SetHeader("Referer", referer);
    http->SetContent(std::string(form));
    if (auto opened = http->Open("POST", url); !opened) {
        error = "open failed: " + opened.error().ToString();
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
