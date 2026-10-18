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

`open(path, mode)` gives a `File` to read or write a piece at a time. The mode
is `"r"` to read (the default), `"w"` to write (replacing what was there) or
`"a"` to add to the end:

```dessa
{ open, File } from $("os");
var[File] log = open("log.txt", "a");
log.writeLine("started");
log.close();

var[File] f = open("log.txt");
while (!f.atEnd()) print(f.readLine());
f.close();
```

A `File` has `readLine()` (nil at the end), `lines()`, `read()` (the rest),
`atEnd()`, `write(text)`, `writeLine(text)`, `flush()`, `isOpen()` and
`close()`.

### random

```dessa
{ randInt, pick, shuffle, seed } from $("random");
print(randInt(1, 6));                          // 1 to 6
print(pick(["rock", "paper", "scissors"]));
print(shuffle([1, 2, 3, 4, 5]));
seed(42);                                      // the same numbers every run
```

Also `rand()` (0 to 1), `randDbl(low, high)`, `chance(p)`, `sample(list,
count)`, `randStr(length)` (letters and digits) and `uuid()`. `pick`,
`shuffle` and `sample` take any list. A seed gives the same numbers in a native
program and in JavaScript. For passwords and keys use `dcrypto` instead.

### dsds

Data structures, written in Dessa. They hold `Obj` values, so anything fits:

```dessa
{ Stack, Queue, PriorityQueue, Graph } from $("dsds");
var[Stack] undo = new Stack();
undo.push("typed a");
print(undo.pop());

var[PriorityQueue] tasks = new PriorityQueue();      // smallest first
tasks.push(5); tasks.push(1);
print(tasks.pop());                                  // 1

var[Graph] roads = new Graph();
roads.addEdge("Cebu", "Manila", 570);
roads.addEdge("Manila", "Baguio", 250);
print(roads.shortestPath("Cebu", "Baguio"), roads.distance("Cebu", "Baguio"));
```

| Class | Main methods |
|---|---|
| `Stack` | `push`, `pop`, `peek` |
| `Queue` | `enqueue`, `dequeue`, `peek` |
| `Deque` | `pushFront`, `pushBack`, `popFront`, `popBack`, `peekFront`, `peekBack` |
| `LinkedList` | `addFirst`, `addLast`, `get`, `set`, `insertAt`, `removeAt`, `removeFirst`, `removeLast`, `indexOf`, `includes`, `reverse` |
| `PriorityQueue` | `push`, `pop`, `peek` (give it a compare function for another order) |
| `SearchTree` | `add`, `has`, `remove`, `min`, `max`, `toList` (sorted) |
| `Graph` | `addNode`, `addEdge`, `neighbors`, `bfs`, `dfs`, `hasPath`, `shortestPath`, `distance` (`new Graph(true)` for one-way edges) |

All of them have `size()`, `isEmpty()`, `clear()` and `toList()`.
`compareValues(a, b)` is the order they use: numbers by value, text
alphabetically.

### dcrypto

```dessa
{ sha256, hashPassword, verifyPassword, encrypt, decrypt } from $("dcrypto");
print(sha256("hello"));

var[Str] stored = hashPassword("s3cret");       // save this, not the password
print(verifyPassword("s3cret", stored));        // true

var[Str] secret = encrypt("meet at noon", "my password");
print(decrypt(secret, "my password"));
```

Hashes: `md5`, `sha1`, `sha256`, `sha512` (as hex) and `hmac(key, text)`
(HMAC-SHA256). `hashPassword` uses PBKDF2-SHA256 with a random salt.
`encrypt` uses AES-256-GCM with a key made from the password, so a wrong
password or changed text makes `decrypt` fail. Also `toBase64`, `fromBase64`,
`toHex`, `fromHex` and `token(bytes)` (secure random hex). md5 and sha1 are
for checksums only.

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

Rows can also go straight into objects of your own class. The columns fill
the fields with the same name (case and `_` don't matter, so `first_name`
fills `firstName`), and a typed loop variable checks each one:

```dessa
class Person {
    var[Int] id = 0;
    var[Str] firstName = "";
    var[Bul] active = false;
}
for (var[Person] p in db.queryAs(() := new Person(), "SELECT * FROM people WHERE id > ?", 10)) {
    print(p.firstName);
}
var[Obj] one = db.queryOneAs(() := new Person(), "SELECT * FROM people WHERE id = ?", 1);
if (one is Person) print(one.firstName);
```

`() := new Person()` tells `queryAs` what to make (there are no generics).
0/1 columns fill `Bul` fields, and a column that doesn't fit its field is an
error naming the column.

Values passed after the SQL fill the `?` places safely. `connect(...)` takes any
ODBC connection string, and `drivers()` lists the installed ODBC drivers.
Both accept `TrustCertificate=yes` as a shorter `TrustServerCertificate=yes`.

### torrent

Shares folders with the BitTorrent protocol; riv uses it. `makeTorrent(folder,
name, trackers, webseeds)`, `download(torrent, target, peers, options)` and
`seed(torrents, folders, port, seconds)`. Peers are found through public
trackers, on the local network, or from a list; web seeds fill in when no one
shares a piece.

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
