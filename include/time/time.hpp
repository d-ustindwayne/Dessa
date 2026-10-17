// $(<time>)
#pragma once
#include "dessa_runtime.hpp"
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <thread>

// IANA time zones ("Asia/Manila"): C++20's tz database where the standard
// library has it (libstdc++ 13+, MSVC); otherwise the system's zoneinfo
// files through TZ + localtime_r (macOS / libc++).
#if (defined(_GLIBCXX_RELEASE) && _GLIBCXX_RELEASE >= 13) || defined(_MSC_VER)
#define DESSA_TZDB 1
#else
#define DESSA_TZDB 0
#endif

namespace dessa::time {

// ---- Calendar math (days since 1970-01-01, proleptic Gregorian) ----
// From Howard Hinnant's "chrono-compatible low-level date algorithms".
inline Int days_from_civil(Int y, Int m, Int d) {
    y -= m <= 2;
    const Int era = (y >= 0 ? y : y - 399) / 400;
    const Int yoe = y - era * 400;
    const Int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const Int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

inline void civil_from_days(Int z, Int& y, Int& m, Int& d) {
    z += 719468;
    const Int era = (z >= 0 ? z : z - 146096) / 146097;
    const Int doe = z - era * 146097;
    const Int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const Int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const Int mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = yoe + era * 400 + (m <= 2);
}

inline Int floor_div(Int a, Int b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }

// Milliseconds since 1970-01-01 00:00 UTC.
inline Int _epochMillis() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// The UTC instant (ms) of a date and time on the UTC calendar.
inline Int _epochOf(Int year, Int month, Int day, Int hour, Int minute, Int second, Int ms) {
    return ((days_from_civil(year, month, day) * 24 + hour) * 60 + minute) * 60000 + second * 1000 + ms;
}

// {year, month, day, hour, minute, second, millisecond, weekday (1 = Monday), day of the year}
// of instant `utc` shifted by `offsetMinutes`.
inline List<Int> _civil(Int utc, Int offsetMinutes) {
    const Int local = utc + offsetMinutes * 60000;
    const Int days = floor_div(local, 86400000);
    Int rest = local - days * 86400000;
    Int y, m, d;
    civil_from_days(days, y, m, d);
    const Int weekday = ((days % 7 + 7) % 7 + 3) % 7 + 1;   // 1970-01-01 was a Thursday
    const Int yearDay = days - days_from_civil(y, 1, 1) + 1;
    const Int hour = rest / 3600000;
    rest %= 3600000;
    return {y, m, d, hour, rest / 60000, (rest % 60000) / 1000, rest % 1000, weekday, yearDay};
}

namespace detail {

// "+08:00", "-0530", "+8" -> minutes; false if `zone` isn't an offset
inline bool parse_offset(const Str& zone, Int& minutes) {
    if (zone.size() < 2 || (zone[0] != '+' && zone[0] != '-')) return false;
    Str digits;
    for (size_t i = 1; i < zone.size(); ++i) {
        if (zone[i] == ':') continue;
        if (zone[i] < '0' || zone[i] > '9') return false;
        digits += zone[i];
    }
    if (digits.empty() || digits.size() > 4) return false;
    Int hours = 0, mins = 0;
    if (digits.size() <= 2) hours = std::stoll(digits);
    else {
        hours = std::stoll(digits.substr(0, digits.size() - 2));
        mins = std::stoll(digits.substr(digits.size() - 2));
    }
    if (hours > 14 || mins > 59) return false;
    minutes = (zone[0] == '-' ? -1 : 1) * (hours * 60 + mins);
    return true;
}

// Offset of the computer's own time zone at instant `utc`.
inline Int local_offset(Int utc) {
    std::time_t t = static_cast<std::time_t>(floor_div(utc, 1000));
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    const Int asUtc = (days_from_civil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday) * 86400 +
                       local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec);
    return (asUtc - static_cast<Int>(t)) / 60;
}

[[noreturn]] inline void unknown_zone(const Str& zone) {
    fail("Unknown time zone '" + zone + "' (use a name like \"Asia/Manila\" or \"America/New_York\", "
         "\"UTC\", \"local\", or an offset like \"+08:00\")");
}

} // namespace detail

// Minutes east of UTC of `zone` at instant `utc`: "UTC", "local", "+08:00" or
// an IANA name such as "Asia/Manila" (daylight saving time included).
inline Int _offsetMinutes(const Str& zone, Int utc) {
    Int minutes = 0;
    if (zone == "UTC" || zone == "Z" || zone == "GMT" || zone == "Etc/UTC") return 0;
    if (zone == "local" || zone.empty()) return detail::local_offset(utc);
    if (detail::parse_offset(zone, minutes)) return minutes;
#if DESSA_TZDB
    try {
        const auto* tz = std::chrono::locate_zone(zone);
        const auto at = std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds(utc));
        return std::chrono::duration_cast<std::chrono::minutes>(tz->get_info(at).offset).count();
    } catch (const std::runtime_error&) {
        detail::unknown_zone(zone);
    }
#elif !defined(_WIN32)
    // The zoneinfo file must exist: an unknown TZ silently means UTC
    std::error_code ec;
    if (zone.find("..") != Str::npos || !std::filesystem::exists("/usr/share/zoneinfo/" + zone, ec)) detail::unknown_zone(zone);
    const char* old = std::getenv("TZ");
    Str saved = old ? old : "";
    setenv("TZ", zone.c_str(), 1);
    tzset();
    minutes = detail::local_offset(utc);
    if (old) setenv("TZ", saved.c_str(), 1);
    else unsetenv("TZ");
    tzset();
    return minutes;
#else
    detail::unknown_zone(zone);
#endif
}

// Is `zone` a time zone _offsetMinutes understands?
inline Bul _isZone(const Str& zone) {
    try {
        _offsetMinutes(zone, _epochMillis());
        return true;
    } catch (...) {
        return false;
    }
}

// Seconds since the Unix epoch, with fractions.
inline Dbl now() {
    using namespace std::chrono;
    return duration<Dbl>(system_clock::now().time_since_epoch()).count();
}

// Milliseconds from a steady clock; use differences to time code.
inline Int millis() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

inline void sleep(Int ms) {
    if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// Local date and time, "YYYY-MM-DD HH:MM:SS".
inline Str date() {
    std::time_t t = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%d %H:%M:%S", &local);
    return buffer;
}

} // namespace dessa::time
