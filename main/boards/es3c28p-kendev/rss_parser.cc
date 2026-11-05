#include "rss_parser.h"

#include <cstring>

namespace zero {

static std::string TagContent(const std::string& block, const char* tag) {
    std::string open = std::string("<") + tag;
    std::string close = std::string("</") + tag + ">";
    size_t p = block.find(open);
    if (p == std::string::npos) return "";
    p = block.find('>', p);
    if (p == std::string::npos) return "";
    p++;
    size_t e = block.find(close, p);
    if (e == std::string::npos) return "";
    std::string s = block.substr(p, e - p);
    // unwrap CDATA
    size_t c = s.find("<![CDATA[");
    if (c != std::string::npos) {
        size_t ce = s.find("]]>", c);
        s = s.substr(c + 9, (ce == std::string::npos ? s.size() : ce) - (c + 9));
    }
    return s;
}

static void ReplaceAll(std::string& s, const char* from, const char* to) {
    size_t n = strlen(from);
    size_t p = 0;
    while ((p = s.find(from, p)) != std::string::npos) {
        s.replace(p, n, to);
        p += strlen(to);
    }
}

static std::string Clean(std::string s) {
    // decode the common entities (escaped HTML first so tags can be stripped)
    ReplaceAll(s, "&lt;", "<");
    ReplaceAll(s, "&gt;", ">");
    ReplaceAll(s, "&quot;", "\"");
    ReplaceAll(s, "&#39;", "'");
    ReplaceAll(s, "&apos;", "'");
    ReplaceAll(s, "&nbsp;", " ");
    // strip tags
    std::string out;
    bool in_tag = false;
    for (char ch : s) {
        if (ch == '<') { in_tag = true; continue; }
        if (ch == '>') { in_tag = false; out += ' '; continue; }
        if (!in_tag) out += ch;
    }
    ReplaceAll(out, "&amp;", "&");
    // collapse whitespace
    std::string r;
    bool space = false;
    for (char ch : out) {
        bool ws = ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t';
        if (ws) {
            if (!r.empty()) space = true;
        } else {
            if (space) r += ' ';
            space = false;
            r += ch;
        }
    }
    return r;
}

// Cut at max bytes without splitting a UTF-8 sequence
static std::string Truncate(const std::string& s, size_t max) {
    if (s.size() <= max) return s;
    size_t cut = max;
    while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) cut--;
    return s.substr(0, cut) + "…";
}

std::vector<RssItem> ParseRss(const std::string& xml, size_t max_items, size_t max_summary) {
    std::vector<RssItem> items;
    size_t pos = 0;
    while (items.size() < max_items) {
        size_t s = xml.find("<item", pos);
        if (s == std::string::npos) break;
        size_t e = xml.find("</item>", s);
        if (e == std::string::npos) break;
        std::string block = xml.substr(s, e - s);
        RssItem it;
        it.title = Clean(TagContent(block, "title"));
        it.summary = Truncate(Clean(TagContent(block, "description")), max_summary);
        it.pub_date = Clean(TagContent(block, "pubDate"));
        if (!it.title.empty()) items.push_back(it);
        pos = e + 7;
    }
    return items;
}

}  // namespace zero
