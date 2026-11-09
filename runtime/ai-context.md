# Dessa, for coding agents

Dessa is a statically typed language that compiles to C, C++ or WebAssembly. Its syntax looks familiar
but differs from other languages in ways that are easy to get wrong. Don't guess: when unsure, run the
checks below. Files are `.ds` (also `.dx .dz .dsx .dxx .dzx .dds .dss`), `.dm` for modules, `.xds .xdz .xdx`
for components, `.dhtml` for pages.

## Check your work

```
dessa check file.ds --json     errors and warnings, nothing is built
dessa run file.ds -- args      compile and run
dessa test                     runs every *_test.ds and *.test.ds (chimb package)
dessa fix file.ds              rewrites old syntax
dessa context <library>        what a library has: names, examples. Look before you use one.
riv add <package>              installs a package into riv_modules/
```

Always run `dessa check` after editing. Its errors are the truth; this page is only a summary.

## The syntax people get wrong

```
// Types go in brackets, before the name for variables, after it for parameters
var[Int] count = 0;
var[Int] x = 1, y = 2;              // declares both
const[Str] NAME = "Ann";
func[Int] add(a[Int], b[Int] = 1) { return a + b; }
func[Emp] log(items[Str*]) { }      // Emp: returns nothing. Str*: variadic, received as Str<>

// Lists use <>, for the literal, the type and the index. Not [ ]
var[Int<>] numbers = <1, 2, 3>;
numbers<0> = 9;
var[Int<><>] grid = <<1, 2>, <3>>;
// There's no negative indexing: numbers<-1> is an error. Use numbers.last()

// Maps and sets
var[Map[Str, Int]] ages = {"Ann": 30};      // {:} is empty
ages<"Bo"> = 25;
var[Str{}] names = {"a", "b"};              // set

// Lambdas use :=, and can't have defaults or variadics. They capture by value.
var[Func[Int, [Int]]] double = (n[Int]) := n * 2;    // Func[Return, [Params]]
const[Int] fact = (n[Int]) := n <= 1 ? 1 : n * fact(n - 1);   // const[R]: its return type

// Named arguments use =. Not :
print("a", "b", sep = ", ");
greet(name = "Ann");

// Imports
$("math");                     // everything
$("math") { sqrt, PI as pi };  // some names
$("math" as m);                // m.sqrt(2)
$("my.helpers");               // my/helpers.ds, next to this file or in the project root

// Classes: this, init, (Parent), super(...), toStr
class Dog(Animal) {
    var[Str] name;
    init(name[Str]) {
        super(name);           // first statement
        this.name = name;      // fields always through this.
    }
    @override
    func[Str] toStr() { return this.name; }
}
var[Dog] d = new Dog("Rex");
class[Shape] Square { }        // implements the interface Shape
interface Shape { name[Str]; func[Dbl] area(); }
enum Level { LOW, HIGH }       // Level.LOW

// Generics use :T
class Box:T { var[T] value; init(v[T]) { this.value = v; } }
var[Box:Int] b = new Box(1);
func[T] first:T(items[T<>]) { return items<0>; }
class Pair:[K, V] { }

// Nil is only for types that say so
var[Str | Nil] maybe = nil;
maybe?.length();  maybe ?? "other";  maybe ?= "set if nil";  maybe ?! "throw if nil";
if (maybe != nil) { maybe.length(); }        // narrowed

// Switch has := and blocks, with no fallthrough
switch (x) {
    case 1 := { }
    default := { }
}

// Text
$"Hello {name}";                  // template string
"""multi
line""";
var[Chr] c = 'x';

// Errors. Only Str or class instances can be thrown; your own extend Error
class TooBig(Error) {}
try { throw new TooBig("no"); } catch (e[TooBig]) { print(e.message); } catch (e[Str]) { }
func[Int !! TooBig] f() { }        // !! lists what it may throw
```

## Async

```
async func[Str] load(id[Int]) { await delay(10); return Str(id); }     // gives a Task:Str
func[Int] async other() { return 1; }                                  // the same, written the other way
async func[Emp] main() {                                               // gives a Task:Emp
    var[Task:Str] a = load(1);
    print(await a, await all(<load(2), load(3)>));
}
main();                         // what is waiting goes on after the program's last line
```

No lambdas can be async, and `await` isn't for lambdas. One thread, no parallelism.

## What is different from what you may assume

| Instead of | Dessa has |
|---|---|
| `len(x)`, `x.size`, `x.count` | `x.length()` |
| `contains` | `includes` (maps: `has`) |
| `self` | `this` |
| `constructor`, `__init__` | `init` |
| `toString` | `toStr` |
| `null`, `None` | `nil`, shown as `Nil` |
| `int`, `string`, `bool`, `float`, `double`, `char`, `void` | `Int`, `Str`, `Bul`, `Flt`, `Dbl`, `Chr`, `Emp` |
| `**`, `^` as XOR | `^` is a power, `^^` is XOR |
| `do-while`, `finally` | they don't exist |
| `while (true)` | `loop { }` is usual |
| `static` | it doesn't exist; use a top-level function |

- `Int / Int` gives a decimal: `7 / 2` is `3.5`, and `var[Int] x = 7 / 2` is `3`.
- `%` is never negative: `-7 % 3` is `2`.
- `Int` is 64 bits, and going past that throws an `OverflowError`. Bigger numbers: `Big` in `math`.
- Conversions look like calls: `Int("42")`, `Str(42)`, `Dbl("2.5")`, `Bul(1)`, `Chr(65)`.
- Text counts characters, not bytes. `Str` is UTF-8.
- Lists, sets and maps are values: assigning or passing one copies it. Objects are references.
- A file's top-level code runs only in the file you run, not in files it imports.
- `yes`/`no` are `true`/`false` too, printed that way. A `dessa.toml` may allow only one pair.
- `_name` means private to a library by convention.
- `|>`: `x |> f(a)` is `f(x, a)`.
- A `func` inside a function is a lambda: no defaults, captures by value.

## Pages and components

`.dhtml` pages: a plain `<script>` is Dessa, compiled to WebAssembly, with `webutils` ready to use.

`.xds` files hold components, used as tags in any page with `<script src="app.xds"></script>`, through
`dessa run page.html`, `dessa serve` or `dessa build page.html`:

```
$("webutils") { Children }
comp Card(title[Str], child[Children], onClick[Func[Emp] | Nil] = nil) {
    @(<div class="card" on:click={onClick}><h2>{title}</h2>{child}</div>)
}
```

`@(...)` returns its markup, and nothing can follow it in that block. Attributes are `"text"` or `{value}`.
Every tag ends: `<br />`. Tags with a capital are components. Text in `{x}` is escaped. In these files
`var[Xml] data = @(<a>{x}</a>)` makes XML instead, with `$("xml") { Xml }`.
