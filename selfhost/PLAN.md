# The Dessa compiler, in Dessa

This folder holds a rewrite of the compiler (`src/`, C++) in Dessa itself. The
C++ compiler stays the real one until the Dessa one can do everything it does.
After that, the Dessa compiler compiles itself.

The rewrite lives in a folder on `main`, not in a branch, so CI keeps
checking it against the C++ compiler while both change.

## How it works

Each stage is written to give **exactly** the same output as its C++ twin, and
a check compares the two on every Dessa file in the repository:

```
python selfhost/check.py          # the lexer: `dessa tokens` vs selfhost/main.ds tokens
```

That way the rewrite can't drift away from the language, and each stage can be
finished and trusted before the next one starts.

## Stages

| # | Stage | C++ today | Dessa | Check |
|---|---|---|---|---|
| 1 | Lexer | `src/lexer` (540 lines) | `lexer.dm`, done | same tokens for all files (`check.py`) |
| 2 | Parser | `src/parser` (1,300) | | same `dessa ast` output |
| 3 | Module loader | `src/driver/loader.cpp` | | same files and import errors |
| 4 | Analyser | `src/analyser` (4,100) | | same errors and warnings on `tests/cases/errors` |
| 5 | JavaScript generator | `src/codegen/jsgen.cpp` (1,600) | | the `--js` tests pass |
| 6 | C++ generator | `src/codegen/codegen.cpp` (1,600) | | the native tests pass |
| 7 | Driver | `src/main.cpp`, `src/driver` | | `run`, `build`, `check`, editor commands |
| 8 | Bootstrap | | | see below |

The JavaScript generator comes before the C++ one: it is simpler, and with it
the Dessa compiler can already run in a browser (compiled to JavaScript by the
C++ compiler), with no WebAssembly build.

## Stage 2: the parser

- The AST as classes: `class Expr`, `class Stmt` and a subclass per kind
  (`BinaryExpr`, `IfStmt`, ...), matched with `is`. No generics are needed:
  child lists are `Expr<>` and `Stmt<>`.
- Errors are collected like the C++ parser does (synchronise at statement
  boundaries) and printed in the same format.
- `dessa ast` gets a full tree dump on both sides so the check can compare
  every node, not only the top-level statements.

## Stage 4: the analyser

The biggest part. It needs:

- `SemanticType` as a class with a kind enum (Obj, Union, Function, ...).
- Scopes: a class holding a `Map[Str, Symbol]` and a parent.
- Side tables (the type of each expression): a `Map[Int, SemanticType]` keyed
  by a node id that the parser assigns, since Dessa maps can't be keyed by
  objects.

## Stage 8: bootstrapping

1. **Stage 0**: the C++ compiler builds `selfhost/main.ds` into `dessa1`.
2. **Stage 1**: `dessa1` builds `selfhost/main.ds` into `dessa2`.
3. **Stage 2**: `dessa2` builds it again into `dessa3`.
4. `dessa2` and `dessa3` must generate the same C++ for every test. Then the
   Dessa compiler is the reference one, and `src/` only has to build stage 0.

## What the language needs first

Things the rewrite will want, to add to Dessa as they come up:

- Reading stdin and writing to stderr (`$("sys")`) for the driver.
- Nested indexing written directly (`edges<path<i>>` doesn't parse yet; a
  temporary variable is needed).
- Faster `Str` building (`+=` in a loop is fine for now).

## Running it

```
dessa run selfhost/main.ds tokens tests/cases/run/hello.ds
dessa build selfhost/main.ds --target js -o lexer.js && node lexer.js tokens file.ds
```
