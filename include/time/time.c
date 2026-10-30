/* $("time") -- the native functions that C programs and WebAssembly call
 * (dsx_time_<name>, see src/codegen/cglue.hpp): the calendar and time zones
 * for class DateTime in time.dm. now, millis, sleep and date are in the
 * runtime.
 *
 * IANA time zones ("Asia/Manila") come from the system: the zoneinfo files
 * on Linux and macOS, Windows' own ICU (icu.dll, Windows 10 1903 and newer),
 * and in WebAssembly the JavaScript engine's Intl. */
#ifdef __wasm__
#include "dessa_web.h"
#else
#include "dessa_rt.h"
#endif
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

/* ---- Calendar math (days since 1970-01-01, proleptic Gregorian) ----
 * From Howard Hinnant's "chrono-compatible low-level date algorithms". */
static ds_int days_from_civil(ds_int y, ds_int m, ds_int d) {
    y -= m <= 2;
    const ds_int era = (y >= 0 ? y : y - 399) / 400;
    const ds_int yoe = y - era * 400;
    const ds_int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const ds_int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(ds_int z, ds_int* y, ds_int* m, ds_int* d) {
    z += 719468;
    const ds_int era = (z >= 0 ? z : z - 146096) / 146097;
    const ds_int doe = z - era * 146097;
    const ds_int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const ds_int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const ds_int mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = yoe + era * 400 + (*m <= 2);
}

static ds_int floor_div(ds_int a, ds_int b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }

/* Milliseconds since 1970-01-01 00:00 UTC */
ds_int dsx_time__epochMillis(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (ds_int)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* The UTC instant (ms) of a date and time on the UTC calendar */
ds_int dsx_time__epochOf(ds_int year, ds_int month, ds_int day, ds_int hour, ds_int minute, ds_int second, ds_int ms) {
    return ((days_from_civil(year, month, day) * 24 + hour) * 60 + minute) * 60000 + second * 1000 + ms;
}

/* {year, month, day, hour, minute, second, millisecond, weekday (1 = Monday),
 * day of the year} of instant `utc` shifted by `offsetMinutes` */
ds_list* dsx_time__civil(ds_int utc, ds_int offsetMinutes) {
    const ds_int local = utc + offsetMinutes * 60000;
    const ds_int days = floor_div(local, 86400000);
    ds_int rest = local - days * 86400000;
    ds_int y, m, d;
    civil_from_days(days, &y, &m, &d);
    const ds_int weekday = ((days % 7 + 7) % 7 + 3) % 7 + 1;   /* 1970-01-01 was a Thursday */
    const ds_int yearDay = days - days_from_civil(y, 1, 1) + 1;
    const ds_int hour = rest / 3600000;
    rest %= 3600000;
    const ds_int parts[9] = {y, m, d, hour, rest / 60000, (rest % 60000) / 1000, rest % 1000, weekday, yearDay};
    ds_list* out = ds_list_new(&ds_elem_int, 9);
    memcpy(out->data, parts, sizeof parts);
    out->len = 9;
    return out;
}

/* "+08:00", "-0530", "+8" -> minutes; false if `zone` isn't an offset */
static bool parse_offset(const char* zone, ds_int* minutes) {
    size_t n = strlen(zone);
    if (n < 2 || (zone[0] != '+' && zone[0] != '-')) return false;
    char digits[8];
    size_t count = 0;
    for (size_t i = 1; i < n; ++i) {
        if (zone[i] == ':') continue;
        if (zone[i] < '0' || zone[i] > '9') return false;
        if (count == 5) return false;   /* more than 4 digits */
        digits[count++] = zone[i];
    }
    if (count == 0 || count > 4) return false;
    ds_int hours = 0, mins = 0;
    if (count <= 2) {
        for (size_t i = 0; i < count; ++i) hours = hours * 10 + (digits[i] - '0');
    } else {
        for (size_t i = 0; i < count - 2; ++i) hours = hours * 10 + (digits[i] - '0');
        mins = (digits[count - 2] - '0') * 10 + (digits[count - 1] - '0');
    }
    if (hours > 14 || mins > 59) return false;
    *minutes = (zone[0] == '-' ? -1 : 1) * (hours * 60 + mins);
    return true;
}

#ifndef __wasm__
/* Offset of the computer's own time zone at instant `utc` */
static ds_int local_offset(ds_int utc) {
    time_t t = (time_t)floor_div(utc, 1000);
    struct tm local;
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    const ds_int asUtc = days_from_civil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday) * 86400 + local.tm_hour * 3600 +
                         local.tm_min * 60 + local.tm_sec;
    return (asUtc - (ds_int)t) / 60;
}
#endif

#if defined(__wasm__)
/* Intl through the bridge (dessa_wasm.js: _tzOffset): NaN when it doesn't know the zone */
static bool named_offset(const char* zone, ds_int utc, ds_int* minutes) {
    ds_str* name = ds_str_cstr(zone);
    dsw_push_str(name);
    dsw_push_num((double)utc);
    dsw_call(NULL, "_tzOffset");
    ds_release(name);
    double m = dsw_take_num();
    if (m != m) return false;
    *minutes = (ds_int)m;
    return true;
}
#elif defined(_WIN32)
/* Windows' ICU (icu.dll), loaded the first time a zone is asked for */
typedef void* (*ucal_open_fn)(const uint16_t* zone, int32_t len, const char* locale, int32_t type, int32_t* status);
typedef void (*ucal_set_millis_fn)(void* cal, double millis, int32_t* status);
typedef int32_t (*ucal_get_fn)(void* cal, int32_t field, int32_t* status);
typedef void (*ucal_close_fn)(void* cal);
typedef int32_t (*ucal_canonical_fn)(const uint16_t* id, int32_t len, uint16_t* out, int32_t cap, int8_t* system, int32_t* status);
static struct {
    bool tried;
    ucal_open_fn open;
    ucal_set_millis_fn setMillis;
    ucal_get_fn get;
    ucal_close_fn close;
    ucal_canonical_fn canonical;
} icu;

static bool load_icu(void) {
    if (icu.tried) return icu.open != NULL;
    icu.tried = true;
    HMODULE dll = LoadLibraryA("icu.dll");
    if (!dll) return false;
    icu.open = (ucal_open_fn)(void*)GetProcAddress(dll, "ucal_open");
    icu.setMillis = (ucal_set_millis_fn)(void*)GetProcAddress(dll, "ucal_setMillis");
    icu.get = (ucal_get_fn)(void*)GetProcAddress(dll, "ucal_get");
    icu.close = (ucal_close_fn)(void*)GetProcAddress(dll, "ucal_close");
    icu.canonical = (ucal_canonical_fn)(void*)GetProcAddress(dll, "ucal_getCanonicalTimeZoneID");
    if (!icu.open || !icu.setMillis || !icu.get || !icu.close || !icu.canonical) icu.open = NULL;
    return icu.open != NULL;
}

static bool named_offset(const char* zone, ds_int utc, ds_int* minutes) {
    if (!load_icu()) return false;
    uint16_t id[128], canonical[128];
    int32_t len = 0;
    for (; zone[len] && len < 127; ++len) id[len] = (uint8_t)zone[len];
    if (zone[len]) return false;
    int32_t status = 0;
    int8_t system = 0;
    icu.canonical(id, len, canonical, 128, &system, &status);
    if (status > 0 || !system) return false;   /* not a zone ICU knows */
    void* cal = icu.open(id, len, "en_US", 1 /* Gregorian */, &status);
    if (status > 0 || !cal) return false;
    icu.setMillis(cal, (double)utc, &status);
    const int32_t zoneMs = icu.get(cal, 15 /* UCAL_ZONE_OFFSET */, &status);
    const int32_t dstMs = icu.get(cal, 16 /* UCAL_DST_OFFSET */, &status);
    icu.close(cal);
    if (status > 0) return false;
    *minutes = (zoneMs + dstMs) / 60000;
    return true;
}
#else
/* The zoneinfo files, through TZ and localtime_r */
static bool named_offset(const char* zone, ds_int utc, ds_int* minutes) {
    if (strstr(zone, "..")) return false;
    char path[512];
    snprintf(path, sizeof path, "/usr/share/zoneinfo/%s", zone);
    struct stat st;
    if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)) return false;
    const char* old = getenv("TZ");
    char* saved = old ? strdup(old) : NULL;
    setenv("TZ", zone, 1);
    tzset();
    *minutes = local_offset(utc);
    if (saved) {
        setenv("TZ", saved, 1);
        free(saved);
    } else {
        unsetenv("TZ");
    }
    tzset();
    return true;
}
#endif

/* Minutes east of UTC of `zone` at instant `utc`; false for an unknown zone */
static bool zone_offset(const char* zone, ds_int utc, ds_int* minutes) {
    if (strcmp(zone, "UTC") == 0 || strcmp(zone, "Z") == 0 || strcmp(zone, "GMT") == 0 || strcmp(zone, "Etc/UTC") == 0) {
        *minutes = 0;
        return true;
    }
    if (zone[0] == 0 || strcmp(zone, "local") == 0) {
#ifdef __wasm__
        return named_offset("local", utc, minutes);
#else
        *minutes = local_offset(utc);
        return true;
#endif
    }
    if (parse_offset(zone, minutes)) return true;
    return named_offset(zone, utc, minutes);
}

ds_int dsx_time__offsetMinutes(ds_str* zone, ds_int utc) {
    ds_int minutes = 0;
    if (!zone_offset(zone->data, utc, &minutes)) {
        ds_fail("Unknown time zone '%s' (use a name like \"Asia/Manila\" or \"America/New_York\", "
                "\"UTC\", \"local\", or an offset like \"+08:00\")",
                zone->data);
    }
    return minutes;
}

ds_bul dsx_time__isZone(ds_str* zone) {
    ds_int minutes = 0;
    return zone_offset(zone->data, dsx_time__epochMillis(), &minutes);
}
