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

```
bun install
bun run start          the site at http://localhost:4200
bun run build          dist/website/browser, ready for GitHub Pages
bun run coverage       run the test suite in every backend and update data/coverage.json
```

`bun run coverage` needs the compiler built (`cmake --build build`) and takes a few
minutes. Run it when backend support changes and commit `data/coverage.json`.

Pushing to `main` publishes the site through `.github/workflows/pages.yml`.
