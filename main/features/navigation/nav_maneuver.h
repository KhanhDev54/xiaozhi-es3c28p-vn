#ifndef NAV_MANEUVER_H
#define NAV_MANEUVER_H

// Turn-by-turn helpers that do not depend on LVGL (host-testable):
// classify the instruction text forwarded by Chronos from Google Maps
// ("Rẽ trái vào Lê Lợi", "Tại vòng xuyến, đi theo lối ra thứ 2", "Turn right"...)
// and parse the distance to the next maneuver ("200 m", "1,2 km").

#include <string>

enum class Maneuver {
    kStraight,
    kLeft, kRight,
    kSlightLeft, kSlightRight,
    kSharpLeft, kSharpRight,
    kKeepLeft, kKeepRight,
    kUTurn,
    kRoundabout,
    kRampUp,    // lên cầu / đường dẫn lên
    kRampDown,  // xuống cầu / lối ra
    kArrive,
};

struct ManeuverInfo {
    Maneuver type = Maneuver::kStraight;
    int exit_number = 0;  // roundabout exit (1..n), 0 = unknown
    bool recognized = false;
};

ManeuverInfo ClassifyManeuver(const std::string& text);

// Distance in meters, or -1 if the text holds no distance
int ParseDistanceMeters(const std::string& text);

#endif  // NAV_MANEUVER_H
