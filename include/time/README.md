# time

The clock, waiting, and `DateTime`: dates with time zones and month names in several languages.

**Install:** Comes with Dessa, nothing to install.

**Runs in:** Native programs and WebAssembly (pages and Node).

## Example

```dessa
$("time") { DateTime, millis, dateNow, dateOf, parseDate }

var[Int] started = millis();
var[DateTime] now = dateNow();
print(now.format("EEEE, d MMMM yyyy HH:mm"));
print(now.toZone("America/New_York").format("HH:mm z"));
print(dateOf(2026, 12, 25).longDate("es"));
print(now.daysUntil(parseDate("2027-01-01")));
print(millis() - started);
```

## What it has

| Part | Names |
|---|---|
| Clock | `now()` (seconds), `millis()`, `sleep(ms)`, `date()` |
| Making a date | `dateNow(zone)`, `dateOf(year, month, day, hour, minute, second, zone)`, `parseDate(text, zone)` |
| Languages | `addLocale(new Locale(...))` |

Time zones: `"local"`, `"UTC"`, an offset like `"+08:00"`, or a name like
`"Europe/Paris"` (daylight saving time included).

Month and day names come in en, es, fr, de, it, pt, nl, fil, ja, zh and ko.

Pattern letters: `yyyy MMMM MMM MM dd d EEEE EEE HH hh mm ss SSS a Z z`. Put other
text in quotes (`'at'`).
