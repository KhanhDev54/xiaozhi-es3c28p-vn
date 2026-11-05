#ifndef ZERO_RSS_PARSER_H
#define ZERO_RSS_PARSER_H

// Minimal RSS 2.0 item extractor (title, description, pubDate). Pure C++, host-testable.

#include <string>
#include <vector>

namespace zero {

struct RssItem {
    std::string title;
    std::string summary;   // description with HTML tags stripped
    std::string pub_date;  // raw pubDate
};

std::vector<RssItem> ParseRss(const std::string& xml, size_t max_items, size_t max_summary = 220);

}  // namespace zero

#endif  // ZERO_RSS_PARSER_H
