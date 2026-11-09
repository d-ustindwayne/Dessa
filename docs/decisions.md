# Why Dessa is the way it is

The decisions that shaped Dessa, with what was weighed, so that later changes can
tell a reason from an accident. Newest at the end of each part.

## The language

| Decision | Why | What it costs |
|---|---|---|
| **Types in brackets: `var[Int] x`** | One shape for variables, functions and parameters, and the type comes first | Unfamiliar at first |
| **Lists are `<1, 2>`** (`[1, 2]` was first) | `[ ]` already means "a type goes here" everywhere else | `<` has three meanings, which is the parser's hardest part |
| **No negative indexes** | `list<-1>` is more often a bug than a wish | A longer way to write the last item |
| **No `do`/`while`, no `finally`** | The creator's preference; `loop` and several `catch` arms cover the rest | Clean-up must be written by hand |
| **`Int` is 64 bits, and overflow is an error** | A wrong number is worse than a stopped program | A branch on each operation |
| **`Int / Int` gives a decimal: `7 / 2` is `3.5`** | What a beginner expects | Past 2^53 it loses digits, so `idiv` exists for exact work |
| **`%` is Euclidean: `-7 % 3` is `2`** | Useful for indexes and clocks | Differs from C and Java |
| **`Int[16]` is how a number prints, not its size** | Sizes are a detail few programs need | No narrow integers |
| **Classes are shared; lists, sets and maps are copied** | Objects usually stand for one thing; a list passed to a function shouldn't change behind your back | Copying lists around needs care, though it is lazy |
| **Lambdas capture by copying** | No surprises with a variable changing later | A lambda needs an object to keep state |
| **Generics written `f:T`** (first refused, then added on 2026-10-26) | Libraries needed them, such as JSON into your own class | Colons mean more things |
| **`Big` is a library class, not a type** | The creator is against more data types | `Big` is slower than a built-in, and needs `new` |
| **`yes` and `no` remember their spelling** | Reads well in settings | A `Bul` is more than one bit |

## The compiler

| Decision | Why | What it costs |
|---|---|---|
| **Compile to C, not machine code** | Every system has a C compiler, and optimising is free | Needs a C compiler; compile time is mostly the C compiler's |
| **C first, C++ as a fallback** (2026-10-22) | C compiles faster and runs faster than the first C++ backend, and C++ libraries still work | Two backends to keep identical |
| **WebAssembly, not JavaScript** (the JS backend was removed on 2026-10-30) | The benchmark: WebAssembly was far faster, with one less backend | Needs a WebAssembly compiler (a 50 MB download), and a bridge to reach the page |
| **Users never see C errors** | They didn't write C | A failed build can only be reported as a Dessa bug |
| **Everything language-aware is in the compiler** | The editor and the compiler can't disagree | The editor starts the compiler often |
| **Reference counting, not a tracing collector** | No pauses, and memory goes back at once | Cycles leak, plus the cost of counting |
| **`setjmp` for errors in C** | Costs nothing until something is thrown, and C has nothing better | Temporaries leak when an error is caught |
| **Stack overflow through a handler, not a check on each call** (2026-11-05) | Function calls are the hot path of a language; a guard page costs nothing | It can't be caught with `catch`, only reported |
| **Limits on nesting, not a bigger stack** (2026-11-05) | A limit gives a clear message and the same answer on each system | Code past the limits must be split. The limits are far beyond hand-written code, though generated code could meet them. |

## Libraries and packages

| Decision | Why | What it costs |
|---|---|---|
| **A small core; the rest are riv packages** | The core stays small, and libraries can change on their own | `riv add` before using JSON |
| **riv shares packages by BitTorrent, with GitHub as a fallback** | No server to run, and the creator's interest in peer-to-peer | More machinery than a plain download; SHA-1 info hashes |
| **Libraries say what they mean in classes, with ready-made objects (`stats`, `words`)** (2026-11-05) | Dessa has no `static`, and a class keeps related functions together | One more name to import |
| **`NDArray` is written in Dessa, not C** (2026-11-05) | Faster to build, and works everywhere at once. C only where measuring shows the need. | Slower than NumPy, whose loops are in C |
| **`request` uses the `curl` program** | HTTPS without a TLS library inside Dessa | Needs curl (Windows 10+, macOS and most Linux have it) |
| **`webutils` has no JSON helpers** (2026-11-05) | It's in the core, and JSON is a package; the core doesn't depend on packages | One more line: `parseJson(r.text)` |

## Security

| Decision | Why | What it costs |
|---|---|---|
| **`request` passes everything in a curl config file** (2026-11-05) | A shell never sees what the program was given, so there's nothing to quote wrongly | A temporary folder for each request |
| **`execute` beside `run`, rather than replacing it** (2026-11-05) | `run` is still useful for your own commands; the safe one is there and documented first | Two ways to do it |
| **`http` listens only on this computer unless told otherwise** (2026-11-05) | A server shouldn't be public by accident | A breaking change, and one more step when deploying |
| **`SHA256SUMS` plus attestations, not our own signing keys** | No keys for one person to lose; GitHub's signing is checkable | Trusts GitHub's build platform |
| **PBKDF2, not Argon2id, for passwords** | Already there, accepted by OWASP at 600,000 rounds, and no new code to get wrong | Weaker against GPUs than Argon2id; on the list to revisit |
