#include "info_services.h"
#include <cctype>

#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "mcp_server.h"
#include "net_util.h"
#include "finance_parse.h"
#include "rss_parser.h"
#include "settings.h"

#define TAG "InfoServices"

// ------------------------------------------------------------- weather helpers

static const char* WmoVi(int wmo) {
    switch (wmo) {
        case 0: return "Trời quang";
        case 1: return "Ít mây";
        case 2: return "Có mây";
        case 3: return "Nhiều mây";
        case 45: case 48: return "Sương mù";
        case 51: case 53: case 55: return "Mưa phùn";
        case 56: case 57: return "Mưa phùn lạnh";
        case 61: return "Mưa nhỏ";
        case 63: return "Mưa vừa";
        case 65: return "Mưa to";
        case 66: case 67: return "Mưa lạnh";
        case 71: case 73: case 75: case 77: return "Tuyết";
        case 80: return "Mưa rào nhẹ";
        case 81: return "Mưa rào";
        case 82: return "Mưa rào to";
        case 85: case 86: return "Mưa tuyết";
        case 95: return "Dông";
        case 96: case 99: return "Dông, mưa đá";
        default: return "Có mây";
    }
}

static const char* AqiLevel(int aqi) {
    if (aqi < 0) return "";
    if (aqi <= 50) return "Tốt";
    if (aqi <= 100) return "Trung bình";
    if (aqi <= 150) return "Kém";
    if (aqi <= 200) return "Xấu";
    if (aqi <= 300) return "Rất xấu";
    return "Nguy hại";
}

static std::string DayName(const char* ymd) {
    int y = 0, m = 0, d = 0;
    if (!ymd || sscanf(ymd, "%d-%d-%d", &y, &m, &d) != 3) return "";
    struct tm t = {};
    t.tm_year = y - 1900;
    t.tm_mon = m - 1;
    t.tm_mday = d;
    t.tm_hour = 12;
    mktime(&t);
    static const char* w[] = {"CN", "T2", "T3", "T4", "T5", "T6", "T7"};
    return w[t.tm_wday % 7];
}

static double ArrNum(cJSON* arr, int i) {
    cJSON* v = cJSON_IsArray(arr) ? cJSON_GetArrayItem(arr, i) : nullptr;
    return cJSON_IsNumber(v) ? v->valuedouble : NAN;
}

// ------------------------------------------------------------- weather

bool InfoServices::ResolveLocation() {
    if (location_ok_) return true;
    std::string body, err;
    Settings s("wifi", false);
    std::string city = s.GetString("weather_city", "");
    if (!city.empty() && city != "auto") {
        std::string url = "https://geocoding-api.open-meteo.com/v1/search?name=" + zero::UrlEncode(city) +
                          "&count=1&language=vi&format=json";
        if (zero::HttpGetText(url, body, err)) {
            cJSON* root = cJSON_Parse(body.c_str());
            cJSON* res = root ? cJSON_GetObjectItem(root, "results") : nullptr;
            cJSON* first = cJSON_IsArray(res) ? cJSON_GetArrayItem(res, 0) : nullptr;
            if (first) {
                cJSON* la = cJSON_GetObjectItem(first, "latitude");
                cJSON* lo = cJSON_GetObjectItem(first, "longitude");
                cJSON* nm = cJSON_GetObjectItem(first, "name");
                if (cJSON_IsNumber(la) && cJSON_IsNumber(lo)) {
                    lat_ = (float)la->valuedouble;
                    lon_ = (float)lo->valuedouble;
                    city_ = cJSON_IsString(nm) ? nm->valuestring : city;
                    location_ok_ = true;
                }
            }
            cJSON_Delete(root);
        }
    }
    if (!location_ok_ && zero::HttpGetText("https://ipwho.is/", body, err)) {
        cJSON* root = cJSON_Parse(body.c_str());
        if (root) {
            cJSON* la = cJSON_GetObjectItem(root, "latitude");
            cJSON* lo = cJSON_GetObjectItem(root, "longitude");
            cJSON* c = cJSON_GetObjectItem(root, "city");
            if (cJSON_IsTrue(cJSON_GetObjectItem(root, "success")) && cJSON_IsNumber(la) && cJSON_IsNumber(lo)) {
                lat_ = (float)la->valuedouble;
                lon_ = (float)lo->valuedouble;
                city_ = cJSON_IsString(c) ? c->valuestring : "";
                location_ok_ = true;
            }
            cJSON_Delete(root);
        }
    }
    if (!location_ok_) {
        ESP_LOGW(TAG, "Location unknown, using Hà Nội");
        lat_ = 21.0285f;
        lon_ = 105.8542f;
        city_ = "Hà Nội";
        location_ok_ = true;
    }
    return true;
}

bool InfoServices::RefreshWeather(int max_age_s) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (weather_.valid && esp_timer_get_time() - weather_time_us_ < (int64_t)max_age_s * 1000000) {
            return true;
        }
    }
    ResolveLocation();

    char url[480];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m,is_day"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
             "&timezone=auto&forecast_days=4&wind_speed_unit=ms",
             lat_, lon_);
    std::string body, err;
    if (!zero::HttpGetText(url, body, err)) {
        ESP_LOGW(TAG, "Weather fetch failed: %s", err.c_str());
        return false;
    }
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) return false;

    WeatherNow w;
    w.city = city_;
    cJSON* cur = cJSON_GetObjectItem(root, "current");
    cJSON* t = cur ? cJSON_GetObjectItem(cur, "temperature_2m") : nullptr;
    cJSON* code = cur ? cJSON_GetObjectItem(cur, "weather_code") : nullptr;
    cJSON* is_day = cur ? cJSON_GetObjectItem(cur, "is_day") : nullptr;
    if (cJSON_IsNumber(is_day)) w.is_day = is_day->valueint != 0;
    if (cJSON_IsNumber(t) && cJSON_IsNumber(code)) {
        cJSON* h = cJSON_GetObjectItem(cur, "relative_humidity_2m");
        cJSON* fl = cJSON_GetObjectItem(cur, "apparent_temperature");
        cJSON* wi = cJSON_GetObjectItem(cur, "wind_speed_10m");
        w.temp = (int)lround(t->valuedouble);
        w.feels_like = cJSON_IsNumber(fl) ? (int)lround(fl->valuedouble) : w.temp;
        w.humidity = cJSON_IsNumber(h) ? h->valueint : 0;
        w.wind = cJSON_IsNumber(wi) ? (float)wi->valuedouble : 0;
        w.desc = WmoVi(code->valueint);
        w.code = code->valueint;
        w.valid = true;
    }
    cJSON* daily = cJSON_GetObjectItem(root, "daily");
    if (daily) {
        cJSON* times = cJSON_GetObjectItem(daily, "time");
        cJSON* codes = cJSON_GetObjectItem(daily, "weather_code");
        cJSON* tmax = cJSON_GetObjectItem(daily, "temperature_2m_max");
        cJSON* tmin = cJSON_GetObjectItem(daily, "temperature_2m_min");
        cJSON* rain = cJSON_GetObjectItem(daily, "precipitation_probability_max");
        int n = cJSON_IsArray(times) ? cJSON_GetArraySize(times) : 0;
        for (int i = 0; i < n; i++) {
            WeatherDay d;
            cJSON* td = cJSON_GetArrayItem(times, i);
            d.day = cJSON_IsString(td) ? DayName(td->valuestring) : "";
            double c = ArrNum(codes, i), hi = ArrNum(tmax, i), lo = ArrNum(tmin, i), r = ArrNum(rain, i);
            d.desc = WmoVi(std::isnan(c) ? 3 : (int)c);
            d.code = std::isnan(c) ? 3 : (int)c;
            d.tmax = std::isnan(hi) ? 0 : (int)lround(hi);
            d.tmin = std::isnan(lo) ? 0 : (int)lround(lo);
            d.rain_prob = std::isnan(r) ? 0 : (int)r;
            w.days.push_back(d);
        }
    }
    cJSON_Delete(root);
    if (!w.valid) return false;

    // Air quality (optional)
    snprintf(url, sizeof(url),
             "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f&longitude=%.4f"
             "&current=us_aqi&timezone=auto",
             lat_, lon_);
    if (zero::HttpGetText(url, body, err)) {
        cJSON* aroot = cJSON_Parse(body.c_str());
        cJSON* acur = aroot ? cJSON_GetObjectItem(aroot, "current") : nullptr;
        cJSON* aqi = acur ? cJSON_GetObjectItem(acur, "us_aqi") : nullptr;
        if (cJSON_IsNumber(aqi)) w.aqi = (int)lround(aqi->valuedouble);
        cJSON_Delete(aroot);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    weather_ = w;
    weather_time_us_ = esp_timer_get_time();
    ESP_LOGI(TAG, "Weather: %s %d°C %s AQI %d", w.city.c_str(), w.temp, w.desc.c_str(), w.aqi);
    return true;
}

WeatherNow InfoServices::GetWeather() {
    std::lock_guard<std::mutex> lock(mutex_);
    return weather_;
}

// ------------------------------------------------------------- news

struct NewsFeed {
    const char* key;
    const char* name;
    const char* url;
};

static const NewsFeed kFeeds[] = {
    {"latest", "Tin mới nhất", "https://vnexpress.net/rss/tin-moi-nhat.rss"},
    {"current", "Thời sự", "https://vnexpress.net/rss/thoi-su.rss"},
    {"world", "Thế giới", "https://vnexpress.net/rss/the-gioi.rss"},
    {"business", "Kinh doanh", "https://vnexpress.net/rss/kinh-doanh.rss"},
    {"sports", "Thể thao", "https://vnexpress.net/rss/the-thao.rss"},
    {"entertainment", "Giải trí", "https://vnexpress.net/rss/giai-tri.rss"},
    {"tech", "Khoa học công nghệ", "https://vnexpress.net/rss/khoa-hoc-cong-nghe.rss"},
    {"health", "Sức khỏe", "https://vnexpress.net/rss/suc-khoe.rss"},
    {"education", "Giáo dục", "https://vnexpress.net/rss/giao-duc.rss"},
    {"law", "Pháp luật", "https://vnexpress.net/rss/phap-luat.rss"},
};

// ------------------------------------------------------------- MCP tools

bool InfoServices::FetchGold(const std::string& want, std::vector<GoldRow>& rows, std::string& date_out) {
    rows.clear();
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char date[32];
    snprintf(date, sizeof(date), "%02d/%02d/%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
    date_out = date;
    std::string body, err;
    std::string form = std::string("method=GetSJCGoldPriceByDate&toDate=") + zero::UrlEncode(date);
    if (!zero::HttpPostForm("https://sjc.com.vn/GoldPrice/Services/PriceService.ashx", form,
                            "https://sjc.com.vn/", body, err)) {
        ESP_LOGW(TAG, "SJC fetch failed: %s", err.c_str());
        return false;
    }
    cJSON* root = cJSON_Parse(body.c_str());
    body.clear();
    body.shrink_to_fit();
    cJSON* data = root ? cJSON_GetObjectItem(root, "data") : nullptr;
    if (!cJSON_IsArray(data)) {
        ESP_LOGW(TAG, "SJC: unexpected response");
        if (root) cJSON_Delete(root);
        return false;
    }
    for (int pass = 0; pass < 2 && rows.empty(); pass++) {
        cJSON* it;
        cJSON_ArrayForEach(it, data) {
            if (rows.size() >= 8) break;
            cJSON* type = cJSON_GetObjectItem(it, "TypeName");
            cJSON* branch = cJSON_GetObjectItem(it, "BranchName");
            cJSON* buy = cJSON_GetObjectItem(it, "BuyValue");
            cJSON* sell = cJSON_GetObjectItem(it, "SellValue");
            if (!cJSON_IsString(type) || !cJSON_IsNumber(buy) || !cJSON_IsNumber(sell)) continue;
            std::string b = cJSON_IsString(branch) ? branch->valuestring : "";
            if (pass == 0 && !want.empty() && b.find(want) == std::string::npos) continue;
            rows.push_back({type->valuestring, b, buy->valuedouble, sell->valuedouble});
        }
    }
    cJSON_Delete(root);
    return !rows.empty();
}

void InfoServices::RegisterTools() {
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool("self.weather.get",
        "Thời tiết THỰC TẾ tại vị trí của thiết bị (Open-Meteo): hiện tại + dự báo 3 ngày tới,\n"
        "kèm chất lượng không khí AQI. Dùng khi người dùng hỏi thời tiết, nhiệt độ, có mưa không,\n"
        "ngày mai thế nào. KHÔNG tự đoán thời tiết nếu tool lỗi.",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            if (!RefreshWeather(600)) {
                return "{\"success\": false, \"message\": \"Không lấy được dữ liệu thời tiết\"}";
            }
            WeatherNow w = GetWeather();
            cJSON* o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "city", w.city.c_str());
            cJSON_AddNumberToObject(o, "temp_c", w.temp);
            cJSON_AddNumberToObject(o, "feels_like_c", w.feels_like);
            cJSON_AddNumberToObject(o, "humidity_percent", w.humidity);
            cJSON_AddNumberToObject(o, "wind_m_s", w.wind);
            cJSON_AddStringToObject(o, "condition", w.desc.c_str());
            if (w.aqi >= 0) {
                cJSON_AddNumberToObject(o, "us_aqi", w.aqi);
                cJSON_AddStringToObject(o, "aqi_level", AqiLevel(w.aqi));
            }
            cJSON* days = cJSON_CreateArray();
            for (size_t i = 0; i < w.days.size(); i++) {
                cJSON* d = cJSON_CreateObject();
                cJSON_AddStringToObject(d, "day", i == 0 ? "hôm nay" : (i == 1 ? "ngày mai" : w.days[i].day.c_str()));
                cJSON_AddStringToObject(d, "condition", w.days[i].desc.c_str());
                cJSON_AddNumberToObject(d, "min_c", w.days[i].tmin);
                cJSON_AddNumberToObject(d, "max_c", w.days[i].tmax);
                cJSON_AddNumberToObject(d, "rain_chance_percent", w.days[i].rain_prob);
                cJSON_AddItemToArray(days, d);
            }
            cJSON_AddItemToObject(o, "forecast", days);
            return o;
        });

    mcp.AddTool("self.finance.gold_price",
        "Giá vàng SJC HÔM NAY lấy trực tiếp từ website Công ty Vàng bạc Đá quý Sài Gòn (sjc.com.vn).\n"
        "Dùng khi người dùng hỏi giá vàng, vàng miếng SJC, vàng nhẫn. Đơn vị: đồng/lượng.\n"
        "Đọc giá theo kiểu tự nhiên, ví dụ 148500000 -> 'một trăm bốn mươi tám triệu rưỡi'.\n"
        "Chỉ đọc số liệu nhận được, KHÔNG tự đoán giá nếu tool lỗi. Nói rõ nguồn là SJC.\n"
        "Args:\n"
        "  branch: khu vực, mặc định 'Hồ Chí Minh' (có thể 'Hà Nội', 'Đà Nẵng'...)",
        PropertyList({Property("branch", kPropertyTypeString, "Hồ Chí Minh")}),
        [this](const PropertyList& p) -> ReturnValue {
            std::vector<GoldRow> rows;
            std::string date;
            if (!FetchGold(p["branch"].value<std::string>(), rows, date)) {
                return "{\"success\": false, \"message\": \"Không lấy được giá vàng từ SJC\"}";
            }
            cJSON* items = cJSON_CreateArray();
            for (const auto& r : rows) {
                cJSON* o = cJSON_CreateObject();
                cJSON_AddStringToObject(o, "type", r.type.c_str());
                cJSON_AddStringToObject(o, "branch", r.branch.c_str());
                cJSON_AddNumberToObject(o, "buy_vnd_per_luong", r.buy);
                cJSON_AddNumberToObject(o, "sell_vnd_per_luong", r.sell);
                cJSON_AddItemToArray(items, o);
            }
            cJSON* out = cJSON_CreateObject();
            cJSON_AddStringToObject(out, "source", "SJC (sjc.com.vn)");
            cJSON_AddStringToObject(out, "date", date.c_str());
            cJSON_AddItemToObject(out, "prices", items);
            return out;
        });

    mcp.AddTool("self.finance.exchange_rate",
        "Tỷ giá ngoại tệ HÔM NAY của ngân hàng Vietcombank (portal.vietcombank.com.vn).\n"
        "Dùng khi người dùng hỏi tỷ giá đô la, giá USD, euro, yên Nhật, won, nhân dân tệ...\n"
        "Trả về giá mua tiền mặt, mua chuyển khoản và giá bán (đồng). Nói rõ nguồn là Vietcombank.\n"
        "KHÔNG tự đoán tỷ giá nếu tool lỗi.\n"
        "Args:\n"
        "  currency: mã tiền tệ: USD (mặc định), EUR, JPY, KRW, CNY, GBP, AUD, SGD, THB...\n"
        "            hoặc ALL để lấy các loại phổ biến",
        PropertyList({Property("currency", kPropertyTypeString, "USD")}),
        [](const PropertyList& p) -> ReturnValue {
            std::string body, err;
            if (!zero::HttpGetText("https://portal.vietcombank.com.vn/Usercontrols/TVPortal.TyGia/pXML.aspx",
                                   body, err)) {
                ESP_LOGW(TAG, "VCB fetch failed: %s", err.c_str());
                return "{\"success\": false, \"message\": \"Không lấy được tỷ giá từ Vietcombank\"}";
            }
            std::string when;
            auto rates = zero::ParseVcbXml(body, &when);
            body.clear();
            body.shrink_to_fit();
            std::string want = p["currency"].value<std::string>();
            for (auto& c : want) c = (char)toupper((unsigned char)c);
            static const char* kCommon[] = {"USD", "EUR", "JPY", "KRW", "CNY", "GBP", "AUD", "SGD"};
            cJSON* items = cJSON_CreateArray();
            for (const auto& r : rates) {
                bool take = false;
                if (want == "ALL" || want.empty()) {
                    for (auto* c : kCommon) take |= (r.code == c);
                } else {
                    take = (r.code == want);
                }
                if (!take) continue;
                cJSON* o = cJSON_CreateObject();
                cJSON_AddStringToObject(o, "currency", r.code.c_str());
                cJSON_AddStringToObject(o, "name", r.name.c_str());
                if (r.buy_cash > 0) cJSON_AddNumberToObject(o, "buy_cash_vnd", r.buy_cash);
                if (r.buy_transfer > 0) cJSON_AddNumberToObject(o, "buy_transfer_vnd", r.buy_transfer);
                if (r.sell > 0) cJSON_AddNumberToObject(o, "sell_vnd", r.sell);
                cJSON_AddItemToArray(items, o);
            }
            if (cJSON_GetArraySize(items) == 0) {
                cJSON_Delete(items);
                return "{\"success\": false, \"message\": \"Vietcombank không niêm yết loại tiền này\"}";
            }
            cJSON* out = cJSON_CreateObject();
            cJSON_AddStringToObject(out, "source", "Vietcombank");
            cJSON_AddStringToObject(out, "updated", when.c_str());
            cJSON_AddItemToObject(out, "rates", items);
            return out;
        });

    mcp.AddTool("self.news.get",
        "Lấy tin tức MỚI NHẤT từ báo VnExpress (RSS) để đọc cho người dùng.\n"
        "Dùng khi người dùng hỏi: 'tin tức hôm nay', 'có tin gì mới', 'tin thể thao', 'tin thế giới'...\n"
        "Chỉ đọc lại tiêu đề/tóm tắt nhận được, KHÔNG thêm chi tiết không có trong kết quả.\n"
        "Args:\n"
        "  category: latest (mặc định), current (thời sự), world (thế giới), business (kinh doanh),\n"
        "            sports (thể thao), entertainment (giải trí), tech (khoa học công nghệ),\n"
        "            health (sức khỏe), education (giáo dục), law (pháp luật)\n"
        "  count: số tin (1-8, mặc định 5)",
        PropertyList({
            Property("category", kPropertyTypeString, "latest"),
            Property("count", kPropertyTypeInteger, 5, 1, 8),
        }),
        [](const PropertyList& p) -> ReturnValue {
            std::string cat = p["category"].value<std::string>();
            int count = p["count"].value<int>();
            const NewsFeed* feed = &kFeeds[0];
            for (const auto& f : kFeeds) {
                if (cat == f.key) feed = &f;
            }
            std::string body, err;
            if (!zero::HttpGetText(feed->url, body, err)) {
                ESP_LOGW(TAG, "News fetch failed: %s", err.c_str());
                return "{\"success\": false, \"message\": \"Không tải được tin tức\"}";
            }
            auto items = zero::ParseRss(body, (size_t)count);
            body.clear();
            body.shrink_to_fit();
            cJSON* o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "source", "VnExpress");
            cJSON_AddStringToObject(o, "category", feed->name);
            cJSON* arr = cJSON_CreateArray();
            for (const auto& it : items) {
                cJSON* n = cJSON_CreateObject();
                cJSON_AddStringToObject(n, "title", it.title.c_str());
                cJSON_AddStringToObject(n, "summary", it.summary.c_str());
                cJSON_AddStringToObject(n, "published", it.pub_date.c_str());
                cJSON_AddItemToArray(arr, n);
            }
            cJSON_AddItemToObject(o, "items", arr);
            return o;
        });
}
