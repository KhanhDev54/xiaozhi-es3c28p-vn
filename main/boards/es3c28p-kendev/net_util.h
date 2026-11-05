#ifndef ZERO_NET_UTIL_H
#define ZERO_NET_UTIL_H

#include <string>

namespace zero {

// GET a URL and return the body. Returns false (and fills error) on failure.
bool HttpGetText(const std::string& url, std::string& body, std::string& error);

// POST an application/x-www-form-urlencoded body. referer may be empty.
bool HttpPostForm(const std::string& url, const std::string& form, const std::string& referer,
                  std::string& body, std::string& error);

std::string UrlEncode(const std::string& value);

}  // namespace zero

#endif  // ZERO_NET_UTIL_H
