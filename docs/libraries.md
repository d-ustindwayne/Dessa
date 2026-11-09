# Libraries

Every library documents itself: hover over a name in VS Code, or ctrl+click an
import such as `$("math")` to open the library's file.

## Always available

`print`, `input`, `typeof`, `zip` and the conversions (`Int(...)`, `Str(...)`,
...) need no import. Every type has its own methods, such as
`"text".upper()`, `<3, 1>.sort()` and `ages.getOr("Ann", 0)`. The full list is
in `runtime/decl/methods.ds`.

## Core libraries

These come with Dessa: `$("name")`.

### math

`sqrt`, `cbrt`, `pow`, `abs`, `min`, `max`, `clamp`, `floor`, `ceil`, `round`,
`trunc`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`,
`tanh`, `log`, `log2`, `log10`, `exp`, `hypot`, `sign`, `gcd`, `lcm`, `random`,
`randomInt`, `PI`, `E`.

`idiv(a, b)` divides whole numbers exactly. `a / b` goes through a `Dbl`, so past
2^53 (about 9 quadrillion) it loses digits.

Statistics on a `Dbl<>`: `sum`, `mean`, `median`, `mode`, `stdev`.

`Big` holds whole numbers of any size:

```dessa
$("math") { Big }
var[Big] f = new Big(1);
for (var[Int] i = 1; i <= 30; i++) f = f.mul(i);
print(f, f.sci());        // 265252859812191058636308480000000 2.65E32
print(f + 1, f * 2, f > new Big(5), new Big(2) ^ 100, f % 7);
```

It has `add`, `sub`, `mul`, `div`, `mod`, `pow`, `negate`, `abs`, `gcd`, `lcm`,
`modPow`, `isqrt`, `compare`, `equals`, `sign`, `isEven`, `isZero`, `isNegative`,
`length`, `sci`, `toInt`, `toDbl` and `toStr`, and the operators
`+ - * / % ^ == < >`. For exact decimals and fractions, see `numbers` below.

### time

`now()` (seconds), `millis()`, `sleep(ms)`, `date()`, and `DateTime`:

```dessa
$("time") { DateTime, dateNow, dateOf, parseDate }
var[DateTime] now = dateNow();                                // this computer's time zone
print(now.format("EEEE, d MMMM yyyy HH:mm"));
print(now.toZone("America/New_York").format("HH:mm z"));
print(dateOf(2026, 12, 25, zone = "Asia/Manila").longDate("es"));   // 25 de diciembre de 2026
print(now.daysUntil(parseDate("2027-01-01")));
```

Time zones: `"local"`, `"UTC"`, an offset like `"+08:00"`, or a name like
`"Europe/Paris"` (daylight saving time included). Month and day names come in
en, es, fr, de, it, pt, nl, fil, ja, zh, ko, and you can add a language with
`addLocale(new Locale(...))`. Pattern letters: `yyyy MMMM MMM MM dd d EEEE EEE
HH hh mm ss SSS a Z z`; put other text in quotes (`'at'`).

### os

Files and the system: `readFile`, `writeFile`, `appendFile`, `copyFile`,
`moveFile`, `fileExists`, `isDir`, `fileSize`, `modifiedTime`, `listDir`,
`makeDir`, `deleteFile`, `deleteAll`, `cwd`, `getEnv`, `setEnv`, and the path
helpers `joinPath("logs", "a.txt")`, `baseName`, `dirName` and `extension`.

To start a program, use `execute`. No shell is involved, so its arguments can hold
anything safely:

```dessa
$("os") { execute, ProcessResult }
var[ProcessResult] r = execute("git", <"log", "--author", whoever>);
if (r.ok()) print(r.output);
```

`run(command)` hands a whole line to the shell. Use it for commands you wrote
yourself, and never put text from a user, a file or the network in it.

`open(path, mode)` gives a `File` to read or write a piece at a time. The mode
is `"r"` to read (the default), `"w"` to write (replacing what was there) or
`"a"` to add to the end:

```dessa
$("os") { open, File }
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
$("random") { randInt, pick, shuffle, seed }
print(randInt(1, 6));                          // 1 to 6
print(pick(<"rock", "paper", "scissors">));
print(shuffle(<1, 2, 3, 4, 5>));
seed(42);                                      // the same numbers every run
```

Also `rand()` (0 to 1), `randDbl(low, high)`, `chance(p)`, `sample(list,
count)`, `randStr(length)` (letters and digits) and `uuid()`. `pick`,
`shuffle` and `sample` take any list. A seed gives the same numbers in a native
program and in WebAssembly. For passwords and keys use `dcrypto` instead.

### dsds

Data structures, written in Dessa. All but `Graph` are generic: say what
they hold, e.g. `Stack:Str` (`Stack:Obj` holds anything):

```dessa
$("dsds") { Stack, Queue, PriorityQueue, Graph }
var[Stack:Str] undo = new Stack();
undo.push("typed a");
print(undo.pop());

var[PriorityQueue:Int] tasks = new PriorityQueue();  // smallest first
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
$("dcrypto") { sha256, hashPassword, verifyPassword, encrypt, decrypt }
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

The web page, for programs that run in a browser: the browser's own objects, and
utilities on top of them (building elements safely, events, forms, addresses,
storage, routes, values that update the page). See [web.md](web.md). A `<canvas>`
is drawn on through `canvas.getContext("2d")` (a `Context2D`, with the browser's
names: `fillRect`, `arc`, `fillText`, `drawImage`, ...); the `canvaslib` package
below is the simpler way.

## Packages

Install these with `riv add <name>` (see [riv.md](riv.md)).

### json

```dessa
$("json") { Json, parseJson, stringify, jsonObject }
var[Json] data = parseJson("{\"name\": \"Ann\", \"tags\": [1, 2]}");
print(data.get("name").asStr(), data.get("tags").at(1).asInt());
print(stringify(data, 2));      // indented
```

JSON can go straight into your classes and back. Each field gets the key with
the same name (case and `_` don't matter); the class needs an `init()`
without arguments:

```dessa
$("json");
class User {
    var[Str] name = "";
    var[Int] age = 0;
    var[Str<>] tags = <>;
}
var[User] ann = parseJsonAs:User("{\"name\": \"Ann\", \"age\": 31}");
var[User<>] all = parseJsonListAs:User("[{\"name\": \"Bo\"}, {\"name\": \"Cy\"}]");
var[User] di = parseJson(text).get("user").to:User();   // a part of a bigger JSON value
print(toJson(ann));                                      // {"name":"Ann","age":31,"tags":[]}
```

Fields that are `Str`, `Int`, `Dbl`, `Flt`, `Bul`, `Json` or lists of `Str`,
`Int`, `Dbl`, `Bul` are filled; a value that doesn't fit its field is a
`JsonError` naming the key.

Numbers keep their exact text, so `asInt()` and `asBig()` give every digit back,
and writing gives the number as it came, however long:

```dessa
var[Json] doc = parseJson("{\"id\": 9007199254740993, \"user\": {\"tags\": [\"a\"]}}");
doc.get("id").asInt();             // 9007199254740993
doc.path("user.tags.0").asStr();   // "a"; a JSON null if any part is missing
doc.intOr("age", 0);               // a default when the key is missing or null
base.merge(changes);               // objects, deeply
doc == other;                      // the same value, in any key order
```

`parseJsonLines` and `toJsonLines` read and write one value per line. Nesting
past 512 levels is a `JsonError` (`parseJson(text, maxDepth)` changes that), so
text from elsewhere can't use up the stack.

`JsonSchema` checks the shape of a value, with this much of JSON Schema: `type`,
`required`, `properties`, `additionalProperties: false`, `items`, `enum`,
`minimum`, `maximum`, `minLength`, `maxLength`, `minItems`, `maxItems`:

```dessa
var[JsonSchema] user = new JsonSchema("""{"type": "object", "required": ["name"],
    "properties": {"age": {"type": "integer", "minimum": 0}}}""");
print(user.check(parseJson("{\"age\": -1}")));   // <"name: missing", "age: -1 is below the minimum 0">
user.require(data);                              // a JsonError with all of them
```


### sql

```dessa
$("sql");
var[Database] db = openSqlite("shop.db");           // or connectMssql("Server=...;Database=...;Trusted_Connection=yes")
db.execute("CREATE TABLE IF NOT EXISTS items (name TEXT, price REAL)");
db.execute("INSERT INTO items VALUES (?, ?)", "Mouse", 25.99);
for (var[Obj] row in db.queryAll("SELECT * FROM items WHERE price < ?", 50)) {
    print(row.name, row.price);
}
var[Obj] cheapest = db.query("SELECT name FROM items ORDER BY price");   // the first row, or nil
```

| Method | Gives |
|---|---|
| `queryAll(sql, ...)` | every row, as objects (`row.name`) |
| `query(sql, ...)` | the first row, or nil |
| `queryAllAs:T(sql, ...)` | every row, as objects of your class `T` (a `T<>`) |
| `queryAs:T(sql, ...)` | the first row as your class, or nil (a `T \| Nil`) |
| `execute(sql, ...)` | how many rows changed (INSERT, UPDATE, DELETE, CREATE ...) |

Rows can also go straight into objects of your own class. The columns fill
the fields with the same name (case and `_` don't matter, so `first_name`
fills `firstName`), and a typed loop variable checks each one:

```dessa
class Person {
    var[Int] id = 0;
    var[Str] firstName = "";
    var[Bul] active = false;
}
for (var[Person] p in db.queryAllAs:Person("SELECT * FROM people WHERE id > ?", 10)) {
    print(p.firstName);
}
var[Person<>] everyone = db.queryAllAs:Person("SELECT * FROM people");
var[Person | Nil] one = db.queryAs:Person("SELECT * FROM people WHERE id = ?", 1);
if (one != nil) print(one.firstName);
```

The class needs an `init()` without arguments (or no `init`; parameters
with defaults are fine). 0/1 columns fill `Bul` fields, and a column that
doesn't fit its field is an error naming the column.

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

### http

A small web server (native programs; WebAssembly can't open one). Only this
computer can reach it unless you say otherwise: `new Server(8080, "0.0.0.0")`.

```dessa
$("http") { Server, Request, text, html, json }
var[Server] server = new Server(8080);
server.get("/", (req[Request]) := html("<h1>Hello from Dessa</h1>"));
server.get("/hello/:name", (req[Request]) := text($"Hello, {req.params<"name">}!"));
server.post("/echo", (req[Request]) := json(req.body));
server.listen();
```

A `Request` has `method`, `path`, `query`, `headers`, `body`, `params`, `form()`
and `cookies()`. Answers are `text`, `html`, `json`, `redirect` or `new
Response(status, body, type)`, with `.header(name, value)` and `.cookie(name,
value)` to add to them. `*` at the end of a route takes the rest of the path.
No route: 404; a handler that throws: 500, with the reason printed on the server
(`server.debug = true` sends it to the client too, while developing).

`server.files("/assets", "public")` serves a folder, and nothing outside it or
hidden. `server.use((req[Request]) := ...)` runs before every route, and answers
in its place when it gives a `Response` (a login check, say).

Clients are read side by side, so a slow one doesn't hold up the others: it has
`server.timeoutSeconds` (10) to send a request of at most `server.maxBytes` (8 MB).
Handlers run one at a time. `server.handle(new Request("GET", "/hello/Ann"))`
answers without the network, for tests. There is no HTTPS: put a reverse proxy
in front of anything public. See [security.md](security.md).

### regex

Regular expressions, written in Dessa (they work everywhere):

```dessa
$("regex") { Regex, Match }
var[Regex] date = new Regex("""(\d{4})-(\d\d)-(\d\d)""");
date.test("due 2026-11-02")                    // true
date.find("due 2026-11-02")?.group(1)          // "2026"
date.replaceAll("2026-11-02", "$3/$2/$1")      // "02/11/2026"
new Regex(""",\s*""").split("a, b,c")         // <"a", "b", "c">
```

`test`, `matches` (the whole text), `find` (a `Match`: `text`, `start`, `end`,
`group(n)`), `findAll`, `replace`, `replaceAll` (`$0`, `$1`, `$$`), `split`,
and `escape(text)`. Patterns: `. [abc] [^a-z] \d \w \s \b ^ $ * + ? {n,m}`
(add `?` for as few as possible), groups `( )` and `(?: )`, `|`. Flags: `"i"`
(ignore case), `"m"` (`^ $` per line), `"s"` (`.` takes newlines). Write
patterns in `"""triple quotes"""`, where a backslash stays a backslash.

### argparse

Command-line arguments with `--help` made for you:

```dessa
$("argparse") { ArgParser, Args }
$("sys") { args }
var[ArgParser] parser = new ArgParser("greet", "Says hello");
parser.flag("loud", "l", "shout it");
parser.option("name", "n", "who to greet", "World");
parser.positional("file", "where to write it", false);
var[Args] a = parser.parse(args());
print(a.has("loud"), a.get("name"), a.find("file"));
```

Options are written `--name Ann`, `--name=Ann` or `-n Ann`; short flags join
(`-lv`). `rest(name, help)` takes the words left over (`a.list(name)`),
`getInt` / `getDbl` read numbers. A mistake prints what is wrong with the usage
line and exits with code 2.

### chimb

Tests for your code:

```dessa
$("chimb") { test, expect, expectTrue, expectThrows, done }
test("adds", () := { expect(2 + 3).toBe(5); });
test("lists", () := { expect(<1, 2>.map((x[Int]) := x * 2)).toBe(<2, 4>); });
test("errors", () := { expectThrows(() := { var[Int<>] e = <>; print(e<0>); }); });
done();
```

Each test prints `ok` or `FAIL` with what went wrong. `expect(x)` has `toBe`,
`notToBe` and `toBeIn`; there are also `expectTrue`, `expectFalse`,
`expectNear` (decimals) and `fail(message)`. `done()` prints the totals and
exits with code 1 when a test failed.

`group(name, body)` keeps tests together. Inside it, `beforeEach` and `afterEach`
run around each of its tests, also the failed ones.

`dessa test` runs every `*_test.ds` and `*.test.ds` file under the folder (not
`riv_modules/` or `build/`) and adds up the results.

### canvaslib

Drawing and simple games in a page (`.dhtml`):

```dessa
$("canvaslib") { Canvas, rgb }
var[Canvas] c = new Canvas(400, 300);
var[Dbl] x = 20;
c.onFrame((dt[Dbl]) := {
    if (c.isKeyDown("ArrowRight")) x += 120 * dt;
    c.background("#203040");
    c.fill("tomato");
    c.circle(x, 150, 20);
});
```

Shapes: `rect`, `rectOutline`, `circle`, `circleOutline`, `line`, `polygon`,
`text`, `image(url, x, y)`; colours with `fill`, `stroke(color, width)`,
`alpha`, `background`, `clear`. Input: `mouseX`, `mouseY`, `mouseDown`,
`isKeyDown(key)`, `onClick`, `onKey`. `onFrame(draw, fps)` animates, `stop()`
ends it; `c.context` is the browser's own drawing object.

### ashwee

Desktop windows on Windows (Win32), Linux (GTK 3) and macOS (AppKit):

```dessa
$("ashwee") { Window, Label, TextBox }
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

Numbers beyond what `Int` and `Dbl` can do, with each kind of work in its own class.

**Exact numbers of any size.** `Decimal` is for money and anywhere `0.1` must be
exactly `0.1`; `Fraction` keeps fractions as fractions. Both have the operators
`+ - * / == <`:

```dessa
$("numbers") { Decimal, Fraction }
new Decimal("0.1") + new Decimal("0.2");     // 0.3
new Decimal("19.99") * 3;                    // 59.97
new Decimal(1).div(3, 5);                    // 0.33333
new Decimal("2.675").round(2);               // 2.68
new Fraction(1, 3) + new Fraction(1, 6);     // 1/2
new Fraction("24/5").mixed();                // "4 4/5"
```

`Complex` has `+ - * /`, `magnitude`, `angle`, `conjugate`, `pow` and `squareRoot`.

**`NDArray`** is an array of `Dbl` with any number of dimensions, in the manner of
NumPy. Operators work element by element, and shapes are matched by NumPy's rule
("broadcasting"):

```dessa
$("numbers") { NDArray, array, array2d, arange, zeros, eye }
var[NDArray] a = array(<1.0, 2.0, 3.0>);
a * 2;                                  // <2, 4, 6>
a + a;                                  // <2, 4, 6>
var[NDArray] m = arange(0, 6).reshape(<2, 3>);
m.sum();                                // 15
m.sumAlong(0);                          // <3, 5, 7>: each column
m.at(1, 2);                             // 5
m.select(m.greater(2));                 // <3, 4, 5>
array2d(<<2.0, 1.0>, <1.0, 3.0>>).inverse();
```

| Part | What it has |
|---|---|
| Making | `array`, `array2d`, `zeros`, `ones`, `full`, `eye`, `arange`, `linspace`, `concat` |
| Shape | `shape`, `ndim`, `size`, `reshape` (one size may be -1), `flatten`, `transpose`, `row`, `column`, `at`, `set`, `item`, `toList` |
| Element by element | `+ - * / ^`, `absolute`, `squareRoot`, `exponential`, `logarithm`, `sine`, `cosine`, `clip`, `rounded`, `map` |
| Summing up | `sum`, `mean`, `std`, `variance`, `min`, `max`, `median`, `product`, each also `...Along(axis)`; `percentile`, `argmin`, `argmax`, `cumsum`, `sorted`, `unique` |
| Masks | `greater`, `less`, `equal`, `select(mask)` |
| Matrices | `dot`, `inner`, `det`, `inverse`, `solve` |

It's written in Dessa, so large arrays are slower than NumPy, whose loops are in C.

**Ready-made objects**, for work that keeps no state of its own:

| Object | What it has |
|---|---|
| `stats` | `sum`, `mean`, `median`, `mode`, `variance`, `stdev`, `smallest`, `largest`, `range`, `percentile`, `covariance`, `correlation`, `histogram`, `zscores` |
| `primes` | `isPrime`, `upTo`, `next`, `factorize`, for numbers of any size |
| `words` | `words.of(78056)` is "seventy-eight thousand fifty-six", up to about 10^3000: `words.of("1000000000000000000000")` is "one sextillion". `words.parse(...)` reads them back. |
| `roman` | `roman.of(1994)`, `roman.parse("MCMXCIV")` |
| `digits` | `of`, `count`, `sum`, `root`, `squares`, `sorted`, `reversed`, `isPalindrome`, of any size |
| `radix` | `encode(255, 16)`, `decode("ff", 16)`, any base from 2 to 36, of any size |
| `units` | `convert(10, "km", "mi")`: length, mass, time, area, volume, speed, data, temperature |
| `columns` | `name(28)` is "AB", `number("AB")` is 28 |
| `numformat` | `commas`, `ordinal`, `percent`, `compact` (1.5M), `bytes` (1.5 MB) |

Mistakes are a `NumberError`. The functions of version 1 (`toRoman`, `toWords`,
`sumDigits`, `excelColumn`, ...) still work; they will go in a later version.

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
