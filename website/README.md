# Website

The source of https://dessa.dustindwayn.ee/, made with Angular and Angular Material.
Every page is rendered to plain HTML at build time and served by GitHub Pages.

The pages are built from the repository itself, so they stay current:

| Page | Comes from |
|---|---|
| Docs | `docs/*.md` |
| Benchmarks | `tests/bench/results.csv` and `tests/bench/RESULTS.md` |
| Features | `website/data/coverage.json` (see below) |
| Examples | `tests/cases/run/*.ds` with their `.out` files, `web/example/counter.dhtml` |
| Version, home page sample | `npm/package.json`, `README.md` |
| Playground starter files | `tests/cases/run/*.ds` (and the modules they import), `website/playground/input.ds` |

```
bun install
bun run start          the site at http://localhost:4200
bun run build          dist/website/browser, ready for GitHub Pages
bun run coverage       run the test suite in every backend and update data/coverage.json
```

## The playground

`/playground` runs the real toolchain in the browser. `bun run build` (and
`bun run playground` alone) prepares `public/playground/`, which isn't in git:

- `compiler/`: the dessa command built for WebAssembly. Build it first, with
  the [Emscripten SDK](https://emscripten.org), from the repository root:
  `emcmake cmake -S . -B build-playground && cmake --build build-playground`
- `objects/`: the runtime and the libraries' C, compiled once with the same
  clang the browser uses (`@yowasp/clang`); SQLite takes a few minutes the
  first time, then they are reused while the C doesn't change
- `clang/`: that clang, gzipped (the page's worker unpacks it)
- the workers (`playground/*.worker.*`), the program loader and the service
  worker that allows `input()` (`coi-serviceworker`)

`bun run coverage` needs the compiler built (`cmake --build build`) and takes a few
minutes. Run it when backend support changes and commit `data/coverage.json`.

Pushing to `main` publishes the site through `.github/workflows/pages.yml`.
