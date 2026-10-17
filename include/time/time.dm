/***
 * $("time") -- the Dessa part: dates and times in any time zone, with month
 * and day names in several languages.
 *
 *     { DateTime, dateNow, dateOf, parseDate } from $("time");
 *
 *     var[DateTime] now = dateNow();                         // here, now
 *     print(now.format("EEEE, d MMMM yyyy HH:mm"));          // Saturday, 17 October 2026 15:45
 *     print(now.format("EEEE d 'de' MMMM", "es"));           // sábado 17 de octubre
 *     print(now.toZone("America/New_York").format("HH:mm z"));
 *
 *     var[DateTime] exam = dateOf(2026, 11, 6, 9, 30, zone: "Asia/Manila");
 *     print(now.daysUntil(exam), "days to go");
 *     print(parseDate("2026-12-25T08:00:00+08:00").weekdayName("fil"));   // Biyernes
 *
 * A DateTime is an instant (milliseconds since 1970-01-01 UTC) seen from a
 * time zone. Zones: "local" (this computer), "UTC", an offset like "+08:00",
 * or an IANA name like "Asia/Manila" / "Europe/Paris" (daylight saving time
 * included). A DateTime never changes; add..., toZone, ... return new ones.
 *
 * Built-in languages for names: en, es, fr, de, it, pt, nl, fil, ja, zh, ko.
 * Add more with addLocale(new Locale(...)).
 *
 * The native helpers are in time.hpp (declared in time.ds).
 */

{ _epochMillis, _epochOf, _civil, _offsetMinutes, _isZone } from $("time");

// ============================================================
// Languages
// ============================================================

// Month and day names of a language. Days start on Monday.
class Locale {
    var[Str] code = "";
    var[Str<>] months = [];
    var[Str<>] shortMonths = [];
    var[Str<>] days = [];
    var[Str<>] shortDays = [];
    var[Str] am = "AM";
    var[Str] pm = "PM";
    var[Str] longDate = "d MMMM yyyy";   // the pattern longDate() uses

    init(code[Str], months[Str<>], shortMonths[Str<>], days[Str<>], shortDays[Str<>], am[Str]="AM", pm[Str]="PM",
         longDate[Str]="d MMMM yyyy") {
        if (months.length() != 12 || shortMonths.length() != 12) throw $"Locale '{code}' needs 12 month names";
        if (days.length() != 7 || shortDays.length() != 7) throw $"Locale '{code}' needs 7 day names (Monday first)";
        this.code = code;
        this.months = months;
        this.shortMonths = shortMonths;
        this.days = days;
        this.shortDays = shortDays;
        this.am = am;
        this.pm = pm;
        this.longDate = longDate;
    }
}

func[Str<>] _numbered(suffix[Str]) {
    var[Str<>] names = [];
    for (var[Int] i = 1; i <= 12; i++) names.push($"{i}{suffix}");
    return names;
}

var[Map[Str, Locale]] _locales = {
    "en": new Locale("en",
        ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"],
        ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"],
        ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"],
        ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"], "AM", "PM", "MMMM d, yyyy"),
    "es": new Locale("es",
        ["enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre"],
        ["ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sept", "oct", "nov", "dic"],
        ["lunes", "martes", "miércoles", "jueves", "viernes", "sábado", "domingo"],
        ["lun", "mar", "mié", "jue", "vie", "sáb", "dom"], "a. m.", "p. m.", "d 'de' MMMM 'de' yyyy"),
    "fr": new Locale("fr",
        ["janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août", "septembre", "octobre", "novembre", "décembre"],
        ["janv.", "févr.", "mars", "avr.", "mai", "juin", "juil.", "août", "sept.", "oct.", "nov.", "déc."],
        ["lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi", "dimanche"],
        ["lun.", "mar.", "mer.", "jeu.", "ven.", "sam.", "dim."], "AM", "PM", "d MMMM yyyy"),
    "de": new Locale("de",
        ["Januar", "Februar", "März", "April", "Mai", "Juni", "Juli", "August", "September", "Oktober", "November", "Dezember"],
        ["Jan.", "Feb.", "März", "Apr.", "Mai", "Juni", "Juli", "Aug.", "Sept.", "Okt.", "Nov.", "Dez."],
        ["Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag", "Sonntag"],
        ["Mo.", "Di.", "Mi.", "Do.", "Fr.", "Sa.", "So."], "AM", "PM", "d. MMMM yyyy"),
    "it": new Locale("it",
        ["gennaio", "febbraio", "marzo", "aprile", "maggio", "giugno", "luglio", "agosto", "settembre", "ottobre", "novembre", "dicembre"],
        ["gen", "feb", "mar", "apr", "mag", "giu", "lug", "ago", "set", "ott", "nov", "dic"],
        ["lunedì", "martedì", "mercoledì", "giovedì", "venerdì", "sabato", "domenica"],
        ["lun", "mar", "mer", "gio", "ven", "sab", "dom"], "AM", "PM", "d MMMM yyyy"),
    "pt": new Locale("pt",
        ["janeiro", "fevereiro", "março", "abril", "maio", "junho", "julho", "agosto", "setembro", "outubro", "novembro", "dezembro"],
        ["jan", "fev", "mar", "abr", "mai", "jun", "jul", "ago", "set", "out", "nov", "dez"],
        ["segunda-feira", "terça-feira", "quarta-feira", "quinta-feira", "sexta-feira", "sábado", "domingo"],
        ["seg", "ter", "qua", "qui", "sex", "sáb", "dom"], "AM", "PM", "d 'de' MMMM 'de' yyyy"),
    "nl": new Locale("nl",
        ["januari", "februari", "maart", "april", "mei", "juni", "juli", "augustus", "september", "oktober", "november", "december"],
        ["jan", "feb", "mrt", "apr", "mei", "jun", "jul", "aug", "sep", "okt", "nov", "dec"],
        ["maandag", "dinsdag", "woensdag", "donderdag", "vrijdag", "zaterdag", "zondag"],
        ["ma", "di", "wo", "do", "vr", "za", "zo"], "AM", "PM", "d MMMM yyyy"),
    "fil": new Locale("fil",
        ["Enero", "Pebrero", "Marso", "Abril", "Mayo", "Hunyo", "Hulyo", "Agosto", "Setyembre", "Oktubre", "Nobyembre", "Disyembre"],
        ["Ene", "Peb", "Mar", "Abr", "May", "Hun", "Hul", "Ago", "Set", "Okt", "Nob", "Dis"],
        ["Lunes", "Martes", "Miyerkules", "Huwebes", "Biyernes", "Sabado", "Linggo"],
        ["Lun", "Mar", "Miy", "Huw", "Biy", "Sab", "Lin"], "AM", "PM", "MMMM d, yyyy"),
    "ja": new Locale("ja", _numbered("月"), _numbered("月"),
        ["月曜日", "火曜日", "水曜日", "木曜日", "金曜日", "土曜日", "日曜日"],
        ["月", "火", "水", "木", "金", "土", "日"], "午前", "午後", "yyyy年M月d日"),
    "zh": new Locale("zh",
        ["一月", "二月", "三月", "四月", "五月", "六月", "七月", "八月", "九月", "十月", "十一月", "十二月"], _numbered("月"),
        ["星期一", "星期二", "星期三", "星期四", "星期五", "星期六", "星期日"],
        ["周一", "周二", "周三", "周四", "周五", "周六", "周日"], "上午", "下午", "yyyy年M月d日"),
    "ko": new Locale("ko", _numbered("월"), _numbered("월"),
        ["월요일", "화요일", "수요일", "목요일", "금요일", "토요일", "일요일"],
        ["월", "화", "수", "목", "금", "토", "일"], "오전", "오후", "yyyy년 M월 d일")
};

// Adds (or replaces) a language for format(..., locale).
func[Emp] addLocale(locale[Locale]) {
    _locales<locale.code> = locale;
}

// The codes of the known languages: ["en", "es", ...]
func[Str<>] localeCodes() {
    return _locales.keys();
}

func[Locale] _locale(code[Str]) {
    if (_locales.has(code)) return _locales<code>;
    // "es-MX" -> "es"
    var[Str] base = code.split("-")<0>;
    if (_locales.has(base)) return _locales<base>;
    throw $"Unknown locale '{code}' (known: {_locales.keys().join(", ")}; add one with addLocale)";
}

// ============================================================
// Calendar helpers
// ============================================================

func[Bul] isLeapYear(year[Int]) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

// 28 to 31
func[Int] daysInMonth(year[Int], month[Int]) {
    if (month == 2) return isLeapYear(year) ? 29 : 28;
    if (month == 4 || month == 6 || month == 9 || month == 11) return 30;
    return 31;
}

func[Str] _pad(value[Int], width[Int]) {
    var[Str] text = Str(value < 0 ? -value : value);
    while (text.length() < width) text = "0" + text;
    return value < 0 ? "-" + text : text;
}

// Whole division rounding down: -7 / 2 -> -4
func[Int] _floorDiv(a[Int], b[Int]) {
    var[Int] q = Int(a / b);
    if (q * b > a) q--;
    return q;
}

func[Str] _offsetText(minutes[Int]) {
    var[Str] sign = minutes < 0 ? "-" : "+";
    var[Int] m = minutes < 0 ? -minutes : minutes;
    return $"{sign}{_pad(Int(m / 60), 2)}:{_pad(m % 60, 2)}";
}

func[Emp] _checkZone(zone[Str]) {
    if (!_isZone(zone)) {
        throw $"Unknown time zone '{zone}' (use a name like \"Asia/Manila\", \"UTC\", \"local\" or an offset like \"+08:00\")";
    }
}

// The instant whose wall-clock time in `zone` is the given one.
func[DateTime] _fromWallClock(year[Int], month[Int], day[Int], hour[Int], minute[Int], second[Int], ms[Int], zone[Str]) {
    var[Int] wall = _epochOf(year, month, day, hour, minute, second, ms);
    var[Int] guess = wall - _offsetMinutes(zone, wall) * 60000;
    return new DateTime(wall - _offsetMinutes(zone, guess) * 60000, zone);
}

// ============================================================
// DateTime
// ============================================================

class DateTime {
    var[Int] millis = 0;    // the instant: milliseconds since 1970-01-01 00:00 UTC
    var[Str] zone = "UTC";
    var[Int] offset = 0;    // minutes ahead of UTC in this zone at this instant

    init(millis[Int]=0, zone[Str]="UTC") {
        this.millis = millis;
        this.zone = zone;
        this.offset = _offsetMinutes(zone, millis);
    }

    func[Int<>] _parts() { return _civil(this.millis, this.offset); }

    // ---- Parts, in this zone ----
    func[Int] year() { return this._parts()<0>; }
    func[Int] month() { return this._parts()<1>; }          // 1 to 12
    func[Int] day() { return this._parts()<2>; }            // 1 to 31
    func[Int] hour() { return this._parts()<3>; }           // 0 to 23
    func[Int] minute() { return this._parts()<4>; }
    func[Int] second() { return this._parts()<5>; }
    func[Int] millisecond() { return this._parts()<6>; }
    func[Int] weekday() { return this._parts()<7>; }        // 1 = Monday ... 7 = Sunday
    func[Int] dayOfYear() { return this._parts()<8>; }      // 1 to 366

    func[Str] monthName(locale[Str]="en") { return _locale(locale).months<this.month() - 1>; }
    func[Str] weekdayName(locale[Str]="en") { return _locale(locale).days<this.weekday() - 1>; }

    // ---- Moving in time ----
    // Exact amounts of time:
    func[DateTime] addMillis(amount[Int]) { return new DateTime(this.millis + amount, this.zone); }
    func[DateTime] addSeconds(amount[Int]) { return this.addMillis(amount * 1000); }
    func[DateTime] addMinutes(amount[Int]) { return this.addMillis(amount * 60000); }
    func[DateTime] addHours(amount[Int]) { return this.addMillis(amount * 3600000); }

    // Calendar steps: the clock time stays the same, even across daylight saving changes.
    func[DateTime] addDays(amount[Int]) {
        var[Int<>] p = this._parts();
        return _fromWallClock(p<0>, p<1>, p<2> + amount, p<3>, p<4>, p<5>, p<6>, this.zone);
    }
    func[DateTime] addWeeks(amount[Int]) { return this.addDays(amount * 7); }

    // 31 January + 1 month is 28 (or 29) February.
    func[DateTime] addMonths(amount[Int]) {
        var[Int<>] p = this._parts();
        var[Int] total = p<0> * 12 + p<1> - 1 + amount;
        var[Int] year = _floorDiv(total, 12);
        var[Int] month = total - year * 12 + 1;
        var[Int] day = p<2> > daysInMonth(year, month) ? daysInMonth(year, month) : p<2>;
        return _fromWallClock(year, month, day, p<3>, p<4>, p<5>, p<6>, this.zone);
    }
    func[DateTime] addYears(amount[Int]) { return this.addMonths(amount * 12); }

    // Midnight at the start of this day, in this zone.
    func[DateTime] startOfDay() {
        var[Int<>] p = this._parts();
        return _fromWallClock(p<0>, p<1>, p<2>, 0, 0, 0, 0, this.zone);
    }

    // The same instant seen from another zone.
    func[DateTime] toZone(zone[Str]) {
        _checkZone(zone);
        return new DateTime(this.millis, zone);
    }
    func[DateTime] toUtc() { return new DateTime(this.millis, "UTC"); }
    func[DateTime] toLocal() { return new DateTime(this.millis, "local"); }

    // ---- Comparing ----
    func[Int] millisUntil(other[DateTime]) { return other.millis - this.millis; }
    func[Int] secondsUntil(other[DateTime]) { return Int(this.millisUntil(other) / 1000); }
    func[Int] minutesUntil(other[DateTime]) { return Int(this.millisUntil(other) / 60000); }
    func[Int] hoursUntil(other[DateTime]) { return Int(this.millisUntil(other) / 3600000); }
    // Whole days (24 hours) from this to `other`; negative if `other` is earlier.
    func[Int] daysUntil(other[DateTime]) { return Int(this.millisUntil(other) / 86400000); }

    // -1 if this is earlier, 1 if later, 0 if the same instant (zones don't matter).
    func[Int] compare(other[DateTime]) {
        if (this.millis < other.millis) return -1;
        return this.millis > other.millis ? 1 : 0;
    }
    func[Bul] isBefore(other[DateTime]) { return this.millis < other.millis; }
    func[Bul] isAfter(other[DateTime]) { return this.millis > other.millis; }
    func[Bul] equals(other[DateTime]) { return this.millis == other.millis; }

    // ---- Text ----
    // Pattern letters (repeat them for longer forms), anything in '...' is copied:
    //   yyyy 2026   yy 26        MMMM October  MMM Oct  MM 10  M 10
    //   dd 07       d 7          EEEE Saturday EEE Sat         D day of the year
    //   HH 15       H 15         hh 03         h 3      a PM
    //   mm 05       m 5          ss 09         s 9      SSS milliseconds
    //   Z +08:00    z the zone name
    // Names come from `locale`: "en", "es", "fr", "de", "it", "pt", "nl", "fil", "ja", "zh", "ko", ...
    func[Str] format(pattern[Str]="yyyy-MM-dd HH:mm:ss", locale[Str]="en") {
        var[Locale] names = _locale(locale);
        var[Int<>] p = this._parts();
        var[Chr<>] chars = pattern.chars();
        var[Str] out = "";
        var[Int] i = 0;
        while (i < chars.length()) {
            var[Chr] letter = chars<i>;
            if (letter == '\'') {
                var[Int] j = i + 1;
                while (j < chars.length() && chars<j> != '\'') {
                    out += chars<j>;
                    j++;
                }
                if (j == i + 1 && j < chars.length()) out += "'";   // '' is a quote
                i = j + 1;
                continue;
            }
            if (!letter.isAlpha() || letter.code() > 127) {   // only a-z, A-Z are pattern letters
                out += letter;
                i++;
                continue;
            }
            var[Int] j = i;
            while (j < chars.length() && chars<j> == letter) j++;
            out += this._field(letter, j - i, p, names);
            i = j;
        }
        return out;
    }

    func[Str] _field(letter[Chr], count[Int], p[Int<>], names[Locale]) {
        var[Int] hour12 = p<3> % 12 == 0 ? 12 : p<3> % 12;
        switch (letter) {
            case 'y' := { return count == 2 ? _pad(p<0> % 100, 2) : _pad(p<0>, count); }
            case 'M' := {
                if (count >= 4) return names.months<p<1> - 1>;
                if (count == 3) return names.shortMonths<p<1> - 1>;
                return _pad(p<1>, count);
            }
            case 'd' := { return _pad(p<2>, count); }
            case 'E' := { return count >= 4 ? names.days<p<7> - 1> : names.shortDays<p<7> - 1>; }
            case 'D' := { return _pad(p<8>, count); }
            case 'H' := { return _pad(p<3>, count); }
            case 'h' := { return _pad(hour12, count); }
            case 'm' := { return _pad(p<4>, count); }
            case 's' := { return _pad(p<5>, count); }
            case 'S' := { return _pad(p<6>, 3).substr(0, count < 3 ? count : 3); }
            case 'a' := { return p<3> < 12 ? names.am : names.pm; }
            case 'Z' := { return _offsetText(this.offset); }
            case 'z' := { return this.zone; }
            default := { throw $"Unknown letter '{letter}' in a date pattern (put text in quotes: 'at')"; }
        }
    }

    // The language's usual long date: "October 17, 2026", "17 de octubre de 2026", "2026年10月17日"
    func[Str] longDate(locale[Str]="en") {
        return this.format(_locale(locale).longDate, locale);
    }

    // ISO 8601: 2026-10-17T15:45:00.000+08:00
    func[Str] toIso() {
        return this.format("yyyy-MM-dd'T'HH:mm:ss.SSS") + (this.offset == 0 && this.zone == "UTC" ? "Z" : _offsetText(this.offset));
    }

    func[Str] toStr() { return this.toIso(); }
}

// ============================================================
// Making DateTimes
// ============================================================

// The current date and time in `zone` ("local" = this computer's zone).
func[DateTime] dateNow(zone[Str]="local") {
    _checkZone(zone);
    return new DateTime(_epochMillis(), zone);
}

// A date and time on the calendar of `zone`. Error if it doesn't exist (31 April).
func[DateTime] dateOf(year[Int], month[Int], day[Int], hour[Int]=0, minute[Int]=0, second[Int]=0, zone[Str]="local") {
    if (month < 1 || month > 12) throw $"Month {month} doesn't exist (1 to 12)";
    if (day < 1 || day > daysInMonth(year, month)) throw $"Day {day} doesn't exist in {year}-{_pad(month, 2)} ({daysInMonth(year, month)} days)";
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        throw $"{hour}:{_pad(minute, 2)}:{_pad(second, 2)} is not a time of day";
    }
    _checkZone(zone);
    return _fromWallClock(year, month, day, hour, minute, second, 0, zone);
}

// The instant `millis` milliseconds after 1970-01-01 UTC, seen from `zone`.
func[DateTime] fromMillis(millis[Int], zone[Str]="UTC") {
    _checkZone(zone);
    return new DateTime(millis, zone);
}

func[Int] _number(text[Str], what[Str], whole[Str]) {
    for (var[Chr] c in text.chars()) {
        if (!c.isDigit()) throw $"'{whole}' is not a date like 2026-10-17 or 2026-10-17T15:45:00+08:00 (bad {what})";
    }
    if (text.isEmpty()) throw $"'{whole}' is not a date like 2026-10-17 or 2026-10-17T15:45:00+08:00 (missing {what})";
    return text.toInt();
}

// Reads ISO 8601 text: "2026-10-17", "2026-10-17 15:45", "2026-10-17T15:45:30.250Z",
// "2026-10-17T15:45:00+08:00". Without a zone in the text, it's a time in `zone`.
func[DateTime] parseDate(text[Str], zone[Str]="local") {
    var[Str] s = text.trim();
    var[Str] datePart = s;
    var[Str] timePart = "";
    var[Str] zonePart = "";
    var[Int] sep = s.indexOf("T") >= 0 ? s.indexOf("T") : s.indexOf(" ");
    if (sep >= 0) {
        datePart = s.substr(0, sep);
        timePart = s.substr(sep + 1, s.length() - sep - 1);
    }
    if (timePart.endsWith("Z")) {
        zonePart = "UTC";
        timePart = timePart.substr(0, timePart.length() - 1);
    } else {
        var[Int] plus = timePart.indexOf("+") >= 0 ? timePart.indexOf("+") : timePart.indexOf("-");
        if (plus >= 0) {
            zonePart = timePart.substr(plus, timePart.length() - plus);
            timePart = timePart.substr(0, plus);
        }
    }
    var[Str<>] d = datePart.split("-");
    if (d.length() != 3) throw $"'{text}' is not a date like 2026-10-17 or 2026-10-17T15:45:00+08:00";
    var[Int] hour = 0;
    var[Int] minute = 0;
    var[Int] second = 0;
    var[Int] ms = 0;
    if (!timePart.isEmpty()) {
        var[Str<>] t = timePart.split(":");
        if (t.length() < 2 || t.length() > 3) throw $"'{text}' has a bad time (use HH:mm or HH:mm:ss)";
        hour = _number(t<0>, "hour", text);
        minute = _number(t<1>, "minute", text);
        if (t.length() == 3) {
            var[Str<>] fraction = t<2>.split(".");
            second = _number(fraction<0>, "second", text);
            if (fraction.length() > 1) ms = _number((fraction<1> + "00").substr(0, 3), "milliseconds", text);
        }
    }
    var[DateTime] result = dateOf(_number(d<0>, "year", text), _number(d<1>, "month", text), _number(d<2>, "day", text),
                                  hour, minute, second, zonePart.isEmpty() ? zone : zonePart);
    return ms == 0 ? result : result.addMillis(ms);
}
