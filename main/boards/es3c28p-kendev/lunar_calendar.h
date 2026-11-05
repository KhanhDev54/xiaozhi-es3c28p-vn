#ifndef LUNAR_CALENDAR_H
#define LUNAR_CALENDAR_H

// Vietnamese lunar calendar (Âm lịch), time zone UTC+7.
// Astronomical algorithm by Hồ Ngọc Đức (https://www.informatik.uni-leipzig.de/~duc/amlich/).
// Pure C++, no ESP-IDF dependency, so it can be unit-tested on a PC.

#include <string>

struct LunarDate {
    int day = 0;
    int month = 0;
    int year = 0;
    bool leap = false;  // tháng nhuận
};

namespace lunar {

// Solar (dương lịch) -> lunar (âm lịch). time_zone = 7 for Vietnam.
LunarDate SolarToLunar(int dd, int mm, int yyyy, double time_zone = 7.0);

// Julian day number of a solar date
long JdFromDate(int dd, int mm, int yyyy);

// "Bính Ngọ" — can chi of a lunar year
std::string YearCanChi(int lunar_year);
// "Nhâm Ngọ" — can chi of a solar day
std::string DayCanChi(int dd, int mm, int yyyy);
// "Mậu Tuất" — can chi of a lunar month (tháng 1 = Dần)
std::string MonthCanChi(int lunar_month, int lunar_year);

// Giờ hoàng đạo of a solar day, e.g. "Tý, Sửu, Thìn, Tỵ, Mùi, Tuất"
std::string AuspiciousHours(int dd, int mm, int yyyy);

// "26/9 âm lịch" or "1/4N âm lịch" (N = nhuận)
std::string ShortText(const LunarDate& d);

}  // namespace lunar

#endif  // LUNAR_CALENDAR_H
