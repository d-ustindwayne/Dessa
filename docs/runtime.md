# How a Dessa program runs

What happens underneath a compiled program: how values look in memory, when
they are freed, and how errors travel. For the compiler's own structure, see
[architecture.md](architecture.md). This page follows the C runtime
(`runtime/dessa_rt.h`, `dessa_rt.c`), which native programs and WebAssembly both
use. The C++ runtime behaves the same, by other means.

## Values

| Dessa | In C | On the heap? |
|---|---|---|
| `Int` | `int64_t` | no |
| `Dbl`, `Flt` | `double`, `float` | no |
| `Chr` | `uint32_t` (a Unicode code point) | no |
| `Bul` | `uint8_t`: bit 0 the value, bit 1 "spelled yes/no" | no |
| enums | `int64_t` | no |
| `Str`, lists, sets, maps, functions, class instances | a pointer to a counted block | yes |
| `Int \| Nil`, `Bul \| Nil` and the like | `{bool has; T value}` | no |
| `Foo \| Nil`, `Str \| Nil` | the same pointer, `NULL` for nil | yes |
| `Obj`, `Int \| Str` | a `ds_head*`; numbers in a small box (`ds_box`) | yes |

Every block on the heap starts the same way:

```c
typedef struct ds_head {
    int64_t refs;            // how many places hold this
    const ds_type* type;     // what it is
} ds_head;
```

`ds_type` gives its name, its kind (`DS_K_STR`, `DS_K_LIST`, ...), how to free
it, how to print it and, for classes, the parent class and method tables. `is`
and `typeof` read this at run time.

```
 var[Item] a = new Item("Mouse");         a ──► ┌───────────────┐
 var[Item] b = a;                         b ──► │ refs = 2      │
                                                │ type ─────────┼─► dsk_Item (name, drop, str, parent, ...)
                                                │ name ─────────┼─► ┌ refs, type, "Mouse" ┐
                                                └───────────────┘
```

**Text** (`ds_str`) is UTF-8 with its length in bytes. `length()`, `s<i>` and
`substr` count characters, and find their place by walking the bytes.

**Lists** (`ds_list`) hold `len`, `cap`, a description of their items (`et`:
`ds_elem_int`, `ds_elem_str`, `ds_elem_ref`, ...) and the items side by side, so
an `Int<>` is a plain array of `int64_t`. **Sets** are lists kept sorted.

**Maps** (`ds_map`) keep keys and values in arrays, in the order they were added,
beside a hash index of positions. So iterating is simple and keeps insertion order.

**Functions** (`ds_fn`) are a code pointer and a block of captured values. Captures
are copies, so a lambda can't change a local of the function around it (it sees
globals live).

**Classes** are structs: the head, then the fields, parents' first. Calls go
through the class's method table, except where the generator can tell which
method it is.

## Memory

Dessa counts references. It has no garbage collector that pauses a program.

- A place that holds a value counts it: `ds_retain` adds one, `ds_release` takes
  one away, and at zero the type's `drop` releases what the block holds and frees it.
- The generator emits these calls. Each C expression is either *owned* (the
  result already holds a count, so someone must release it) or *borrowed*; in the
  generator that is `CV {code, owned}`.
- Temporaries in a statement go in variables, released at its end. Locals are
  released when their scope ends, in reverse order, also on `return`, `break` and
  `continue`.

**Lists, sets and maps act like values**, and text never changes: `b = a` shares
the block and adds to its count, and a write first checks that the count is 1
(`ds_list_unique`). If it isn't, the write works on a fresh copy. Copying therefore
costs nothing until something changes. This is also why a list passed to a function
can't be changed by it, unlike a class instance, which is shared.

**Cycles leak.** Two objects that hold each other are never freed. Programs are
short-lived enough that this rarely matters, and there's no weak reference yet.

**Allocation.** Blocks up to 512 bytes come from free lists, one per size in steps
of 16, carved out of 64 KB chunks; the usual case is a few inline instructions.
Larger ones go to `malloc`. `DS_NO_POOL` sends everything to `malloc`, so memory
checkers can see each block (see `tests/sanitize.py`). Counts aren't atomic, which
is part of the speed, and is safe because programs have one thread.

## Errors

`throw` and runtime errors work through `setjmp`/`longjmp`:

```c
typedef struct ds_catch { ds_jump jump; struct ds_catch* prev; } ds_catch;
extern ds_catch* ds_handler;   // the innermost try
```

A `try` pushes a `ds_catch` and calls `setjmp`, which costs little when nothing is
thrown; a throw stores the error and jumps to `ds_handler`.

**The classes of errors** are in Dessa, in `include/prelude/errors.ds`, which every
program gets. When the runtime fails, with text like `Int overflow: ...`, it calls
`ds_error_hook`, set to the prelude's `_error`, which picks the class from the
message and makes the object. So `catch (e[OverflowError])` catches a runtime
error, and `catch (e[Str])` still works too (it gets the message).

**Line numbers.** Each statement sets `ds_trace_file` and `ds_trace_line`. That's
two stores per statement, in exchange for every error saying where it happened.

**What a jump skips.** Temporaries of the statement that was interrupted aren't
released, as `longjmp` doesn't run any code on the way out. Variables held across a
`try` are `volatile` where needed. The leak is small and only on paths with errors.

## Async

The generators know nothing of `async`. `src/analyser/analyser_async.cpp` rewrites each async function, once
checked, into plain Dessa: a class holding its variables, with one method that runs from where it
stopped, a `switch` over states inside a `try`. So reference counting, errors and all three outputs work as
for any class. A stopped function is an object in the heap, with no stack of its own. `include/async/async.dm` has
`Task`, the list of what is ready, and timers; pages use the browser's timers instead.

## Checks

| Check | How | Cost |
|---|---|---|
| `Int` arithmetic | `__builtin_*_overflow`, then a cold path | a branch |
| Text or a decimal to `Int` | `ds_dbl_to_int`, `ds_str_to_int`: in C, casting a `double` out of range is undefined behaviour | a comparison |
| List, text and map access | Compared with the length, and an `IndexError` | a branch |
| `%` by zero, `/` by zero | A `DivisionError` | a branch |
| Nil | The type checker; `?!` and conversions at run time | none, mostly |
| `is` on a class | Walks the parent chain from `type` | a few loads |
| `is` on a list, set or map | Reads `et`; for items that are objects, looks at each one | a pass over the items |
| Stack overflow | The system's guard page; a handler prints the line | nothing until it happens |

**Stack overflow.** A check on each call would slow every call, so programs use
what the system already does: at the end of the stack the guard page faults, and a
handler (`SetThreadStackGuarantee` and a vectored handler on Windows, `sigaltstack`
and `SIGSEGV` elsewhere) prints the error and the line, and ends the program. In
WebAssembly the engine stops the program, and the bridge reports it.
`-Wl,--stack-first` puts WebAssembly's stack at the bottom of memory, so running off
it traps; otherwise it would run into the program's data.

## WebAssembly

The same C goes through clang for WebAssembly (`wasm32-wasip1`), with WASI for
files, the clock and random numbers. In a page, or in Node, `runtime/dessa_wasm.js`
provides WASI and a bridge to the page.

**Page objects.** Dessa can't hold a JavaScript object, so the bridge keeps a table
of them, and Dessa holds a number in a counted `ds_js`; when the count reaches zero,
the bridge forgets the object. Any class in `webutils` whose methods are all `{ ... }`
is a page object.

```
 el.textContent = "Hi"
      │ generated C                        │ runtime/dessa_wasm.js
      ▼                                    ▼
 dsw_push_str(v);  ────────────►  pushed.push(text(ptr, len))
 dsw_set(el, "textContent");  ──►  objects[n]["textContent"] = pushed[0]
```

The bridge is fixed: nothing is generated for each program. A method call pushes
its arguments and calls by name (`target[name](...values)`), which is why a browser
method can be added to `webutils.dm` without touching the bridge.

**Callbacks.** A Dessa function given to the page is wrapped in a JavaScript function
that calls the exported `dsw_invoke(f, kind, arg)`. An error nobody caught inside one
is printed, and the page goes on.

**Waiting.** Dessa has no `await`. `fetchText` and `request` use synchronous requests,
which stop the page while they wait. `fetchAsync` calls you back instead.

## The C++ backend

The fallback for libraries with C++ parts. `Ref<T>` is an intrusive counted pointer,
with a pool for small objects; errors are C++ exceptions; values in an `Obj` are tagged
boxes compared by `typeid`. Tests compare its output with the C backend's line by line.
