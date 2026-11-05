#include "lunar_calendar.h"

#include <cmath>
#include <cstdio>

namespace lunar {

static const double PI = 3.14159265358979323846;

static const char* kCan[10] = {"Giáp", "Ất", "Bính", "Đinh", "Mậu",
                                "Kỷ", "Canh", "Tân", "Nhâm", "Quý"};
static const char* kChi[12] = {"Tý", "Sửu", "Dần", "Mão", "Thìn", "Tỵ",
                                "Ngọ", "Mùi", "Thân", "Dậu", "Tuất", "Hợi"};

long JdFromDate(int dd, int mm, int yy) {
    long a = (14 - mm) / 12;
    long y = yy + 4800 - a;
    long m = mm + 12 * a - 3;
    long jd = dd + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
    if (jd < 2299161) {
        jd = dd + (153 * m + 2) / 5 + 365 * y + y / 4 - 32083;
    }
    return jd;
}

// Day number of the k-th new moon after 1/1/1900 (local time)
static long NewMoonDay(long k, double tz) {
    double T = k / 1236.85;
    double T2 = T * T;
    double T3 = T2 * T;
    double dr = PI / 180;
    double Jd1 = 2415020.75933 + 29.53058868 * k + 0.0001178 * T2 - 0.000000155 * T3;
    Jd1 += 0.00033 * std::sin((166.56 + 132.87 * T - 0.009173 * T2) * dr);
    double M = 359.2242 + 29.10535608 * k - 0.0000333 * T2 - 0.00000347 * T3;
    double Mpr = 306.0253 + 385.81691806 * k + 0.0107306 * T2 + 0.00001236 * T3;
    double F = 21.2964 + 390.67050646 * k - 0.0016528 * T2 - 0.00000239 * T3;
    double C1 = (0.1734 - 0.000393 * T) * std::sin(M * dr) + 0.0021 * std::sin(2 * dr * M);
    C1 = C1 - 0.4068 * std::sin(Mpr * dr) + 0.0161 * std::sin(dr * 2 * Mpr);
    C1 = C1 - 0.0004 * std::sin(dr * 3 * Mpr);
    C1 = C1 + 0.0104 * std::sin(dr * 2 * F) - 0.0051 * std::sin(dr * (M + Mpr));
    C1 = C1 - 0.0074 * std::sin(dr * (M - Mpr)) + 0.0004 * std::sin(dr * (2 * F + M));
    C1 = C1 - 0.0004 * std::sin(dr * (2 * F - M)) - 0.0006 * std::sin(dr * (2 * F + Mpr));
    C1 = C1 + 0.0010 * std::sin(dr * (2 * F - Mpr)) + 0.0005 * std::sin(dr * (2 * Mpr + M));
    double deltat;
    if (T < -11) {
        deltat = 0.001 + 0.000839 * T + 0.0002261 * T2 - 0.00000845 * T3 - 0.000000081 * T * T3;
    } else {
        deltat = -0.000278 + 0.000265 * T + 0.000262 * T2;
    }
    double JdNew = Jd1 + C1 - deltat;
    return (long)std::floor(JdNew + 0.5 + tz / 24);
}

// Sun longitude (0..11, 30-degree sectors) at local midnight of day jdn
static int SunLongitude(long jdn, double tz) {
    double T = (jdn - 2451545.5 - tz / 24) / 36525;
    double T2 = T * T;
    double dr = PI / 180;
    double M = 357.52910 + 35999.05030 * T - 0.0001559 * T2 - 0.00000048 * T * T2;
    double L0 = 280.46645 + 36000.76983 * T + 0.0003032 * T2;
    double DL = (1.914600 - 0.004817 * T - 0.000014 * T2) * std::sin(dr * M);
    DL += (0.019993 - 0.000101 * T) * std::sin(dr * 2 * M) + 0.000290 * std::sin(dr * 3 * M);
    double L = L0 + DL;
    L = L * dr;
    L = L - PI * 2 * std::floor(L / (PI * 2));
    return (int)std::floor(L / PI * 6);
}

static long LunarMonth11(int yy, double tz) {
    long off = JdFromDate(31, 12, yy) - 2415021;
    long k = (long)std::floor(off / 29.530588853);
    long nm = NewMoonDay(k, tz);
    int sunLong = SunLongitude(nm, tz);
    if (sunLong >= 9) {
        nm = NewMoonDay(k - 1, tz);
    }
    return nm;
}

static int LeapMonthOffset(long a11, double tz) {
    long k = (long)std::floor((a11 - 2415021.076998695) / 29.530588853 + 0.5);
    int last = 0;
    int i = 1;
    int arc = SunLongitude(NewMoonDay(k + i, tz), tz);
    do {
        last = arc;
        i++;
        arc = SunLongitude(NewMoonDay(k + i, tz), tz);
    } while (arc != last && i < 14);
    return i - 1;
}

LunarDate SolarToLunar(int dd, int mm, int yy, double tz) {
    long dayNumber = JdFromDate(dd, mm, yy);
    long k = (long)std::floor((dayNumber - 2415021.076998695) / 29.530588853);
    long monthStart = NewMoonDay(k + 1, tz);
    if (monthStart > dayNumber) {
        monthStart = NewMoonDay(k, tz);
    }
    long a11 = LunarMonth11(yy, tz);
    long b11 = a11;
    int lunarYear;
    if (a11 >= monthStart) {
        lunarYear = yy;
        a11 = LunarMonth11(yy - 1, tz);
    } else {
        lunarYear = yy + 1;
        b11 = LunarMonth11(yy + 1, tz);
    }
    LunarDate r;
    r.day = (int)(dayNumber - monthStart + 1);
    long diff = (long)std::floor((monthStart - a11) / 29.0);
    r.leap = false;
    int lunarMonth = (int)(diff + 11);
    if (b11 - a11 > 365) {
        int leapMonthDiff = LeapMonthOffset(a11, tz);
        if (diff >= leapMonthDiff) {
            lunarMonth = (int)(diff + 10);
            if (diff == leapMonthDiff) {
                r.leap = true;
            }
        }
    }
    if (lunarMonth > 12) {
        lunarMonth -= 12;
    }
    if (lunarMonth >= 11 && diff < 4) {
        lunarYear -= 1;
    }
    r.month = lunarMonth;
    r.year = lunarYear;
    return r;
}

std::string YearCanChi(int lunar_year) {
    return std::string(kCan[(lunar_year + 6) % 10]) + " " + kChi[(lunar_year + 8) % 12];
}

std::string DayCanChi(int dd, int mm, int yy) {
    long jd = JdFromDate(dd, mm, yy);
    return std::string(kCan[(jd + 9) % 10]) + " " + kChi[(jd + 1) % 12];
}

std::string MonthCanChi(int lunar_month, int lunar_year) {
    int can = (lunar_year * 12 + lunar_month + 3) % 10;
    int chi = (lunar_month + 1) % 12;
    return std::string(kCan[can]) + " " + kChi[chi];
}

std::string AuspiciousHours(int dd, int mm, int yy) {
    // Pattern of good hours (12 chars, '1' = hoàng đạo) per day-chi group
    static const char* kPattern[6] = {
        "110100101100",  // ngày Tý, Ngọ
        "001101001011",  // ngày Sửu, Mùi
        "110011010010",  // ngày Dần, Thân
        "101100110100",  // ngày Mão, Dậu
        "001011001101",  // ngày Thìn, Tuất
        "010010110011",  // ngày Tỵ, Hợi
    };
    long jd = JdFromDate(dd, mm, yy);
    int chiOfDay = (int)((jd + 1) % 12);
    const char* p = kPattern[chiOfDay % 6];
    std::string out;
    for (int i = 0; i < 12; i++) {
        if (p[i] == '1') {
            if (!out.empty()) out += ", ";
            out += kChi[i];
        }
    }
    return out;
}

std::string ShortText(const LunarDate& d) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%d/%d%s âm lịch", d.day, d.month, d.leap ? "N" : "");
    return buf;
}

}  // namespace lunar
