# Security policy

## Reporting a problem

Please report security problems privately, through GitHub:
**[Report a vulnerability](https://github.com/d-ustindwayne/Dessa/security/advisories/new)**.
Please don't open a public issue for them.

It helps to include what you did, what happened, what you expected, the Dessa
version (`dessa --version`) and your system. A small program that shows the
problem is the most useful thing you can send.

Dessa is maintained by one person. These are aims, not guarantees:

| Step | Aim |
|---|---|
| A first answer | within 14 days |
| A fix, for anything rated medium or higher | within 60 days of it being confirmed |
| Telling people | a GitHub Security Advisory with the fix, crediting you unless you'd rather not |

## What is supported

Only the newest release gets fixes. Update with `riv update`.

## What counts

Reports about any of these are welcome:

- A valid Dessa program that corrupts memory or reads memory it shouldn't
  (that's always a bug, whatever the program does)
- The compiler, `riv` or a library running something it was only meant to read
- A package or release file being changed without `riv`, `dessa setup` or npm
  noticing
- `dessa serve` or the `http` package handing out what they shouldn't
- `dcrypto` not doing what it says

These are not vulnerabilities:

- A program doing what its code says, such as `run(...)` with text a user typed.
  [docs/security.md](docs/security.md) says what Dessa does and doesn't protect
  against.
- A program running out of memory, or a loop that never ends. Compile and run
  code you don't trust inside a sandbox: Dessa is not one.

Security is covered in more depth in [docs/security.md](docs/security.md) (how
it works) and [docs/security-assessment.md](docs/security-assessment.md) (how it
measures up against the published frameworks).
