#include "nav_maneuver.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <algorithm>

static std::string Lower(const std::string& in) {
    std::string s;
    s.reserve(in.size());
    for (unsigned char c : in) s += (char)tolower(c);
    // Common capitalised Vietnamese first letters (UTF-8)
    static const char* kPairs[][2] = {
        {"Đ", "đ"}, {"Ở", "ở"}, {"Ơ", "ơ"}, {"Ư", "ư"}, {"Ê", "ê"}, {"Â", "â"}, {"Ă", "ă"},
        {"Ô", "ô"}, {"Ấ", "ấ"}, {"Ố", "ố"}, {"Ế", "ế"}, {"Ý", "ý"}, {"Í", "í"}, {"Á", "á"},
    };
    for (auto& p : kPairs) {
        size_t pos = 0;
        std::string up = p[0], lo = p[1];
        while ((pos = s.find(up, pos)) != std::string::npos) {
            s.replace(pos, up.size(), lo);
            pos += lo.size();
        }
    }
    return s;
}

static bool Has(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos;
}

template <typename... T>
static bool HasAny(const std::string& s, T... needles) {
    return (Has(s, needles) || ...);
}

static int ExitNumber(const std::string& s) {
    // "lối ra thứ 2", "lối ra thứ hai", "2nd exit", "exit 2"
    static const char* kWords[] = {"nhất", "hai", "ba", "tư", "năm", "sáu"};
    size_t p = s.find("thứ ");
    if (p != std::string::npos) {
        p += strlen("thứ ");
        if (p < s.size() && isdigit((unsigned char)s[p])) return atoi(s.c_str() + p);
        for (int i = 0; i < 6; i++) {
            if (s.compare(p, strlen(kWords[i]), kWords[i]) == 0) return i + 1;
        }
    }
    for (size_t i = 0; i + 2 < s.size(); i++) {
        if (isdigit((unsigned char)s[i]) &&
            (s.compare(i + 1, 2, "st") == 0 || s.compare(i + 1, 2, "nd") == 0 ||
             s.compare(i + 1, 2, "rd") == 0 || s.compare(i + 1, 2, "th") == 0)) {
            return s[i] - '0';
        }
    }
    p = s.find("exit ");
    if (p != std::string::npos && p + 5 < s.size() && isdigit((unsigned char)s[p + 5])) {
        return atoi(s.c_str() + p + 5);
    }
    return 0;
}

ManeuverInfo ClassifyManeuver(const std::string& text) {
    ManeuverInfo r;
    std::string s = Lower(text);
    bool left = HasAny(s, "trái", "left");
    bool right = HasAny(s, "phải", "right");
    // Only look at the side mentioned FIRST ("rẽ phải rồi rẽ trái" -> right)
    if (left && right) {
        size_t l = std::min(s.find("trái"), s.find("left"));
        size_t rr = std::min(s.find("phải"), s.find("right"));
        left = l < rr;
        right = !left;
    }
    r.recognized = true;

    if (HasAny(s, "vòng xuyến", "bùng binh", "roundabout", "traffic circle", "rotary")) {
        r.type = Maneuver::kRoundabout;
        r.exit_number = ExitNumber(s);
    } else if (HasAny(s, "quay đầu", "quay lại", "u-turn", "u turn", "make a u")) {
        r.type = Maneuver::kUTurn;
    } else if (HasAny(s, "lên cầu", "cầu vượt", "đường dẫn lên", "lên đường cao tốc", "lên cao tốc",
                      "nhập vào đường cao tốc", "take the ramp", "onto the ramp", "ramp onto",
                      "onto the bridge", "flyover")) {
        r.type = Maneuver::kRampUp;
    } else if (HasAny(s, "xuống cầu", "đường dẫn xuống", "rời cầu", "lối ra", "rời đường cao tốc",
                      "take exit", "take the exit", "off-ramp", "off ramp")) {
        r.type = Maneuver::kRampDown;
    } else if (HasAny(s, "đến nơi", "điểm đến", "đã đến", "arrive", "destination")) {
        r.type = Maneuver::kArrive;
    } else if (HasAny(s, "ngoặt", "rẽ gấp", "sharp")) {
        r.type = left ? Maneuver::kSharpLeft : Maneuver::kSharpRight;
    } else if (HasAny(s, "chếch", "hơi rẽ", "rẽ nhẹ", "slight", "bear")) {
        r.type = left ? Maneuver::kSlightLeft : Maneuver::kSlightRight;
    } else if (HasAny(s, "giữ bên", "đi bên", "giữ làn", "keep", "nhập làn", "merge")) {
        r.type = left ? Maneuver::kKeepLeft : Maneuver::kKeepRight;
    } else if (HasAny(s, "rẽ", "queo", "quẹo", "turn") && (left || right)) {
        r.type = left ? Maneuver::kLeft : Maneuver::kRight;
    } else if (HasAny(s, "đi thẳng", "tiếp tục", "continue", "straight", "head ")) {
        r.type = Maneuver::kStraight;
    } else if (left || right) {
        r.type = left ? Maneuver::kLeft : Maneuver::kRight;
    } else {
        r.type = Maneuver::kStraight;
        r.recognized = false;
    }
    return r;
}

int ParseDistanceMeters(const std::string& text) {
    std::string s = Lower(text);
    for (size_t i = 0; i < s.size(); i++) {
        if (!isdigit((unsigned char)s[i])) continue;
        // number with ',' or '.' as decimal separator
        size_t j = i;
        double v = 0;
        while (j < s.size() && isdigit((unsigned char)s[j])) v = v * 10 + (s[j++] - '0');
        if (j + 1 < s.size() && (s[j] == ',' || s[j] == '.') && isdigit((unsigned char)s[j + 1])) {
            double f = 0.1;
            j++;
            while (j < s.size() && isdigit((unsigned char)s[j])) { v += (s[j++] - '0') * f; f /= 10; }
        }
        while (j < s.size() && s[j] == ' ') j++;
        std::string unit = s.substr(j, 2);
        if (unit == "km") return (int)(v * 1000 + 0.5);
        if (unit == "mi") return (int)(v * 1609 + 0.5);
        if (unit == "ft") return (int)(v * 0.3048 + 0.5);
        if (!unit.empty() && unit[0] == 'm') return (int)(v + 0.5);
        i = j;
    }
    return -1;
}
