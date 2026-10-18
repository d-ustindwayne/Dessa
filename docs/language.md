# The Dessa language

Dessa is statically typed: every variable, parameter and function result has a
type, and mistakes are reported before the program runs. A program is turned
into C++ (and from there into a native program) or into JavaScript.

This page covers the whole language. For the libraries see
[libraries.md](libraries.md).

## A first program

```dessa
// hello.ds
var[Str] name = input("Your name: ");
print($"Hello, {name}!");
```

```
dessa run hello.ds
```

Statements end with `;`. Code at the top level of the file you run is the
program. Files you import only give their declarations; their top-level code
doesn't run.

## Comments

```dessa
// to the end of the line
/* a block */
/** documentation: shown when you hover over the name in VS Code */
```

## Types

| Type | What it holds | Example |
|---|---|---|
| `Int` | whole number, 64-bit | `42`, `-7`, `0xff`, `0b1010`, `0o17` |
| `Dbl` | decimal number, 64-bit | `3.14`, `2.0` |
| `Flt` | decimal number, 32-bit | `9.23f` |
| `Str` | text (UTF-8) | `"hello"` |
| `Chr` | one character | `'a'`, `'é'` |
| `Bul` | true or false | `true` |
| `Emp` | no value (a function that returns nothing) | |
| `Nil` | the empty value | `nil` |
| `T<>` | list of T | `[1, 2, 3]` |
| `T{}` | set of T (sorted, no duplicates) | `{3, 1, 2}` |
| `Map[K, V]` | keys to values, in the order they were added | `{"a": 1, "b": 2}` |
| `A \| B` | either an A or a B | `Int \| Str` |
| `T \| Nil` | a T or nothing | `Str \| Nil` |
| `Func[R, [P1, P2]]` | a function taking P1, P2 and returning R | `Func[Int, [Int]]` |
| `Int[16]` | an Int that prints in base 16 (also 2, 8) | prints `0xff` |
| `Obj` | any value (see [Obj](#obj)) | `new { name = "Ann" }` |

Nested types work as you would expect: `Int<><>` is a list of lists,
`(Int | Str)<>` is a list holding Ints and Strs, `Map[Str, Int<>]` maps text
to lists.

`Int` arithmetic never wraps around silently: going past the 64-bit range is
an error. For bigger numbers use `Big` from `$("math")`.

`/` between two Ints gives a decimal (`7 / 2` is `3.5`). Stored into an `Int`
it is cut to a whole number (`var[Int] x = 7 / 2;` gives 3). `%` never gives a
negative result (`-7 % 3` is 2). `^` is power (`2 ^ 10` is 1024).

## Variables

```dessa
var[Int] count = 0;          // can change
const[Dbl] RATE = 0.07;      // can't change
var[Str<>] names = [];
var[Int | Nil] found;         // starts as nil
```

## Text

```dessa
var[Str] who = "world";
print($"Hello, {who}! 2 + 2 = {2 + 2}");      // template string
print("tab\there, quote \", unicode \x41");
var[Str] sql = """
    SELECT name
    FROM users
    """;                                         // multi-line, indentation removed
```

Text counts characters, not bytes: `"héllo".length()` is 5 and `"héllo"<1>`
is `'é'`. Useful methods: `length`, `upper`, `lower`, `title`, `trim`, `includes`,
`startsWith`, `endsWith`, `indexOf`, `replace` (first match), `replaceAll`,
`split`, `substr(start, length)`, `repeat`, `chars`, `compare`, `toInt`,
`toDbl`. Decimals: `3.14159.toFixed(2)` is `"3.14"`.

`"Café Niño".plain()` removes accents (`"Cafe Nino"`). `==` is exact, so to
compare without case use `caseless`: `name.caseless() == "ANN".caseless()`.

## Conversions

```dessa
Int("42")      Dbl("2.5")     Str(42)      Bul("yes")     Chr(65)     Int('A')
```

A conversion that can't work is an error you can catch.

## Printing and reading

```dessa
print("a", 1, true);                  // a 1 true
print("a", "b", sep: ", ");           // a, b
print("Loading", end: "");            // no new line
var[Str] answer = input("Name? ");
```

`nil` prints as `Nil`. Whole decimals print without `.0` (`16 / 4` prints 4).

## Lists, sets and maps

```dessa
var[Int<>] xs = [5, 3, 8];
xs.push(1);
print(xs<0>, xs.length(), xs.includes(8));     // 5 4 true
xs.sort();                                       // [1, 3, 5, 8]
xs.sort((a[Int], b[Int]) := b - a);              // biggest first
var[Int<>] doubled = xs.map((x[Int]) := x * 2);
var[Int<>] big = xs.filter((x[Int]) := x > 3);
var[Int<>] all = [...xs, ...doubled, 100];       // spread

var[Str{}] tags = {"red", "blue", "red"};        // {"blue", "red"}
tags.add("green");

var[Map[Str, Int]] ages = {"Ann": 31};
ages<"Bea"> = 27;                                // add or replace
print(ages<"Ann">, ages.has("Cy"), ages.getOr("Cy", 0));
for (var[Str] name in ages) print(name, ages<name>);
```

Indexes start at 0. Negative indexes are errors, and so is a missing map key
(use `has`, `get` or `getOr` to check first).

Lists, sets and maps are values: assigning one or passing it to a function
gives a copy. Changing the copy doesn't change the original.

`zip(a, b)` pairs two lists up (`[[a0, b0], [a1, b1], ...]`), and
`zip(a, b, (x, y) := ...)` combines each pair.

## Control flow

```dessa
if (score >= 90) {
    print("A");
} else if (score >= 80) {
    print("B");
} else {
    print("C");
}

var[Str] label = count == 1 ? "item" : "items";

for (var[Int] i = 0; i < 3; i++) print(i);
for (var[Str] name in names) print(name);       // lists, sets, map keys, characters of a Str
while (running) { ... }
loop {                                          // until break
    if (done) break;
}

switch (choice) {
    case "1" := { start(); }
    case "2" := { stop(); }
    default := { print("unknown"); }
}
```

There is no do-while and no `finally`, by design.

## Functions

```dessa
func[Int] add(a[Int], b[Int]) {
    return a + b;
}

func[Str] greet(name[Str], greeting[Str]="Hello") {     // default value
    return $"{greeting}, {name}!";
}

func[Int] total(numbers[Int*]) {                          // any number of arguments
    var[Int] sum = 0;
    for (var[Int] n in numbers) sum += n;
    return sum;
}

print(add(2, 3), greet("Ann"), greet(greeting: "Hi", name: "Bea"), total(1, 2, 3), total(...[4, 5]));
```

Functions can be called before the place they are written. Parameters write
the type after the name (`name[Str]`); variables write it before
(`var[Str] name`).

## Lambdas

```dessa
var[Func[Int, [Int]]] square = (x[Int]) := x * x;
var[Func[Emp, [Str]]] shout = (text[Str]) := {
    print(text.upper());
};
print(square(4));

func[Func[Int, [Int]]] adder(amount[Int]) {
    return (x[Int]) := x + amount;          // captures amount
}
```

A lambda captures the variables it uses by value, at the moment it is made.
A lambda stored in a variable can call itself.

## Nil safety

A value that may be missing has a `| Nil` type, and Dessa makes you check it:

```dessa
var[Str | Nil] nickname = findNickname();
print(nickname ?? "none");                 // a fallback
print(nickname?.upper());                  // nil if nickname is nil
if (nickname != nil) {
    print(nickname.length());              // known to be a Str here
}
```

`is` checks a type and narrows the variable inside the `if`:

```dessa
var[Int | Str] value = 5;
if (value is Int) print(value + 1);
print(typeof(value));                       // "Int"
```

## Classes

```dessa
class Animal {
    var[Str] name = "";

    init(name[Str]) {
        this.name = name;
    }

    func[Str] speak() {
        return "...";
    }

    // print(animal) uses toStr() when the class has one
    func[Str] toStr() {
        return $"{this.name} says {this.speak()}";
    }
}

class Dog(Animal) {                     // Dog is an Animal
    var[Int] age = 0;

    init(name[Str], age[Int]) {
        super.init(name);               // must come first
        this.age = age;
    }

    func[Str] speak() {
        return "Woof";
    }
}

var[Animal] pet = new Dog("Rex", 3);
print(pet);                             // Rex says Woof
if (pet is Dog) print(pet.age);
```

- Objects are shared, not copied: two variables can point at the same object.
- A class without `init` takes its parent's `init`.
- Without a `toStr()`, an object prints as `Dog(name: "Rex", age: 3)`.

## Interfaces

An interface lists what a class must have. A class names it in square brackets:

```dessa
interface Describable {
    name[Str];
    func[Str] summary();
}

class[Describable] Product {
    var[Str] name = "";
    func[Str] summary() { return $"Product {this.name}"; }
}

class[Describable] Account(BaseAccount) { ... }    // interface and parent together
```

## Enums

```dessa
enum Color { RED, GREEN, BLUE(10) }

var[Color] c = Color.GREEN;
print(c, c.name(), c.value());          // GREEN GREEN 1
switch (c) {
    case RED := { print("stop"); }      // the enum name can be left out
    case GREEN := { print("go"); }
    default := { }
}
```

## Obj

An `Obj` holds any value: a number, text, a list, an instance of a class, or
an object made on the spot with `new { ... }` (like C#'s anonymous objects).

```dessa
var[Obj] reply = new { message = "Message to the world!", code = 200 };
print(reply.message);                   // Message to the world!
print(reply);                           // {message = "Message to the world!", code = 200}
reply.code = 404;                       // fields can be changed and added
var[Int] code = Int(reply.code);        // a field is an Obj too: convert it to use it

var[Obj<>] things = [1, "two", 3.5, new Dog("Rex")];
for (var[Obj] t in things) print(typeof(t), t);

var[Obj] pet = new Dog("Rex");
if (pet is Dog) print(pet.speak());     // after `is`, it is a Dog
```

Fields are looked up when the program runs, so a missing field is an error
then (`Obj has no field 'missing'`). The compiler still checks the rest: an
`Obj` can't be used as a `Str` or in `+` without converting it (`Str(x)`,
`Int(x)`) or checking it with `is`, and methods are only called after `is`.
`==` compares values, and two anonymous objects are equal when their fields
are. An `Obj` may hold `nil`.

A loop over `Obj` values may give its variable a class type; each item is
checked when the loop gets to it:

```dessa
var[Obj<>] pets = [new Dog("Rex"), new Dog("Bo")];
for (var[Dog] d in pets) print(d.speak());
```

Fields can also be reached by name: `fieldsOf(x)` lists them, `getField(x,
"name")` reads one and `setField(x, "name", value)` sets one. They work on
class instances and anonymous objects.

## Errors

```dessa
class NotFound {
    var[Str] what = "";
    init(what[Str]) { this.what = what; }
}

try {
    throw new NotFound("user 7");
} catch (e[NotFound]) {
    print("missing:", e.what);
} catch (e[Str]) {
    print("error:", e);              // runtime errors and thrown text
} catch {
    print("something else");         // anything left (must be last)
}

throw "plain text works too";
```

Runtime errors (an index out of range, a division by zero, an `Int`
overflow, a failed conversion) can be caught with `catch (e[Str])`. An error
nobody catches stops the program and shows where it happened:
`Error: Index 3 is out of range for list of length 2 at app.ds:12`.

## Modules

```dessa
$("utils.strings");                         // everything from utils/strings.ds
{ slugify, trim as clean } from $("utils.strings");   // only these (with a new name)
$("math");                                  // a library
{ sqrt, PI } from $("math");
$("math", "time");                          // several at once
```

A dotted path maps to folders: `$("a.b.c")` is `a/b/c.ds`, looked up next to
the importing file and then in the project folder. When there is no such file,
the name is a library: one that comes with Dessa (`math`, `time`, `os`, ...) or
a package installed with riv into `riv_modules/` (see [riv.md](riv.md)).

Your own file wins over a library with the same name, and the compiler warns
about it: with a `json.ds` in your project, `$("json")` is that file, not the
json library.

Dessa files can end in `.ds` (also `.dx`, `.dz`, `.dsx`, `.dxx`, `.dzx`, `.dds`,
`.dss`). A `.dm` file is a module: declarations only, it can be imported but
not run.

Names starting with `_` are private to their module: importing everything
leaves them out.

## Pages (.dhtml)

A `.dhtml` file is a web page where every `<script>` without a type holds
Dessa code. See [web.md](web.md).

## Operators

| Kind | Operators |
|---|---|
| Arithmetic | `+ - * / % ^` (power) |
| Comparison | `== != < > <= >=` |
| Logic | `&& \|\| !` |
| Bits (Int) | `& \| ^^` (xor) `<< >> ~` |
| Assignment | `= += -= *= /= %= ^=`, `++ --` |
| Nil | `??` (fallback), `?.` (safe member) |
| Other | `? :` (ternary), `is` (type check), `...` (spread) |

Comparisons can't be chained: write `0 < x && x < 10`.

## Reserved for later

`|>` (pipe), `with`, `@` and a few others are reserved words: using them is an
error now so that they can get a meaning later.
