# Libraries

Every library documents itself: hover over a name in VS Code, or ctrl+click an
import such as `$("math")` to open the library's file.

## Always available

`print`, `input`, `typeof`, `zip` and the conversions (`Int(...)`, `Str(...)`,
...) need no import. Every type has its own methods, such as
`"text".upper()`, `[3, 1].sort()` and `ages.getOr("Ann", 0)`. The full list is
in `runtime/decl/methods.ds`.

## Core libraries

These come with Dessa: `$("name")`.

### math

`sqrt`, `cbrt`, `pow`, `abs`, `min`, `max`, `clamp`, `floor`, `ceil`, `round`,
`sin`, `cos`, `tan`, `atan2`, `log`, `log10`, `exp`, `random`, `randomInt`,
`PI`, `E`.

Statistics on a `Dbl<>`: `sum`, `mean`, `median`, `mode`, `stdev`.

`Big` holds whole numbers of any size:

```dessa
{ Big } from $("math");
var[Big] f = new Big(1);
for (var[Int] i = 1; i <= 30; i++) f = f.mul(i);
print(f, f.sci());        // 265252859812191058636308480000000 2.65E32
```

### time

`now()` (seconds), `millis()`, `sleep(ms)`, `date()`, and `DateTime`:

```dessa
{ DateTime, dateNow, dateOf, parseDate } from $("time");
var[DateTime] now = dateNow();                                // this computer's time zone
print(now.format("EEEE, d MMMM yyyy HH:mm"));
print(now.toZone("America/New_York").format("HH:mm z"));
print(dateOf(2026, 12, 25, zone: "Asia/Manila").longDate("es"));   // 25 de diciembre de 2026
print(now.daysUntil(parseDate("2027-01-01")));
```

Time zones: `"local"`, `"UTC"`, an offset like `"+08:00"`, or a name like
`"Europe/Paris"` (daylight saving time included). Month and day names come in
en, es, fr, de, it, pt, nl, fil, ja, zh, ko, and you can add a language with
`addLocale(new Locale(...))`. Pattern letters: `yyyy MMMM MMM MM dd d EEEE EEE
HH hh mm ss SSS a Z z`; put other text in quotes (`'at'`).

### os

Files and the system: `readFile`, `writeFile`, `appendFile`, `fileExists`,
`isDir`, `listDir`, `makeDir`, `deleteFile`, `deleteAll`, `cwd`, `getEnv`,
`run(command)`.

### sys

`args()` (the program's arguments), `exit(code)`, `platform()`.

### webutils

The web page, for programs that run in a browser. See [web.md](web.md).

## Packages

Install these with `riv add <name>` (see [riv.md](riv.md)).

### json

```dessa
{ Json, parseJson, stringify, jsonObject } from $("json");
var[Json] data = parseJson("{\"name\": \"Ann\", \"tags\": [1, 2]}");
print(data.get("name").asStr(), data.get("tags").at(1).asInt());
print(stringify(data, 2));      // indented
```

### phl

PHL is a readable data format (a separate project). `parsePhl(text)` gives a
`Json`, and `toPhl(json)` writes PHL.

### sql

```dessa
$("sql");
var[Database] db = openSqlite("shop.db");           // or connectMssql("Server=...;Database=...;Trusted_Connection=yes")
db.execute("CREATE TABLE IF NOT EXISTS items (name TEXT, price REAL)");
db.execute("INSERT INTO items VALUES (?, ?)", "Mouse", 25.99);
for (var[Json] row in db.query("SELECT * FROM items WHERE price < ?", 50)) {
    print(row.get("name").asStr());
}
```

Values passed after the SQL fill the `?` places safely. `connect(...)` takes any
ODBC connection string, and `drivers()` lists the installed ODBC drivers.
Both accept `TrustCertificate=yes` as a shorter `TrustServerCertificate=yes`.

### request

`get`, `post`, `put`, `patch`, `delete`, `postJson`, and `request(method, url,
...)`. Each returns a `Response` with `status`, `ok` (true for 2xx), `text`
(the body), `headers`, `header(name)`, `json()` and `raiseForStatus()`. It uses the `curl` program, which Windows 10+, macOS and most Linux
systems have.

### ashwee

Desktop windows on Windows (Win32), Linux (GTK 3) and macOS (AppKit):

```dessa
{ Window, Label, TextBox } from $("ashwee");
var[Window] win = new Window("Greeter", 360, 140);
var[TextBox] name = win.input(20, 20, 200);
var[Label] greeting = win.label("", 20, 64, 320);
win.button("Greet", 236, 18).onClick(() := greeting.setText($"Hello, {name.text()}!"));
win.run();
```

Controls: `label`, `button`, `input`, `password`, `textArea`, `checkBox`,
`list`, plus `message(text)` for a message box. A full example is in
`tools/riv/packages/ashwee/examples/todo.ds`.

On Linux, programs need GTK 3 at run time (`libgtk-3-0` on Debian and Ubuntu,
`gtk3` on Fedora and Arch). Nothing extra is needed to build them.

### numbers

`toRoman`, `fromRoman`, `toWords` (78056 is "seventy-eight thousand
fifty-six"), `fromWords`, `excelColumn` (28 is "AB"), `excelColumnNumber`,
`sumDigits`, `digitalRoot`, `squareDigits`, `sortDigits`, `nearestToZero`,
`mixedFraction` ("24/5" is "4 4/5"), `convertTemperature(value, "C", "F")`.

### codes

Check digits: `upcCheckDigit` / `isValidUpc`, `isbn10CheckDigit` /
`isValidIsbn10`, `isbn13CheckDigit` / `isValidIsbn13`, `uspsCheckDigit` /
`isValidUsps`, `luhnCheckDigit` / `isValidCard` (credit cards). Ciphers:
`rot13`, `caesar(text, shift)`. Spaces and dashes in codes are ignored.

### colors

`textColor(text, "green")`, `background(text, "yellow")`, `red`, `green`,
`yellow`, `blue`, `magenta`, `cyan`, `stripColors`, `clearLines`, `rgbToHex`,
`hexToRgb`, `banner`.

### textfx

`box(lines...)`, `padLeft`, `padRight`, `isPalindrome`, `isAnagram`,
`isPangram`, `splitCamel` ("userName" is "user Name"), `countVowels`,
`countConsonants`, `likes(names)`.

## Writing your own

A library is a package: see [riv.md](riv.md#making-a-package).
