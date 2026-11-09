# Security self-assessment

Where Dessa stands against the published security frameworks.

**This is a self-assessment**, written by the project, not an audit or a
certification. Nobody independent has reviewed Dessa. "Met" means there is
something in the repository you can check, named in the table.

Status of this page: written for Dessa 0.6.0 (November 2026).

## Summary

Dessa was not built to a framework. It was built by one developer, with AI help,
aiming for a safe language. In November 2026 it was checked against the frameworks
below, the problems that turned up were fixed, and tests were added so they stay
fixed. What is still open is listed in [What remains](#what-remains).

| Framework | What it asks about | Where Dessa stands |
|---|---|---|
| [NIST SSDF](https://csrc.nist.gov/projects/ssdf) (SP 800-218, version 1.1) | How software is built and maintained | Most of what applies to a one-person project is met; see below |
| [CWE Top 25](https://cwe.mitre.org/top25/) | The most dangerous kinds of bug | Covered, or left to the programmer, weakness by weakness; see below |
| [SLSA](https://slsa.dev/spec/v1.0/levels) (Build track) | Can you trust where a release came from? | Build Level 2 once the next release is made with this workflow |
| [OpenSSF Best Practices Badge](https://www.bestpractices.dev/en/criteria/0) | Basic practices of an open source project | Not applied for yet; the known gaps are below |
| [OWASP Cheat Sheets](https://cheatsheetseries.owasp.org/) | Passwords, web servers and pages | Followed for `dcrypto`, `http` and `webutils`; see [security.md](security.md) |

Frameworks left out, and why:

- **ISO/IEC 27001** and the **NIST Cybersecurity Framework** are about running an
  organisation (its people, risks and processes), not about a product.
- **ISO/IEC 24772** (programming language vulnerabilities) is the closest match
  for a language, but it isn't free and hasn't been read for this assessment. The
  CWE table below covers much of the same ground, using a free source.

## What was found

The assessment found real problems. They are listed because a list like this is
what makes an assessment useful. All of them were fixed, and nearly all have a test
that fails without the fix. Two don't yet: the private compile cache, and the download
checks as a whole, whose checksum logic is tested but which need a real release to
run from start to finish. None is known to have affected anyone.

| Problem | Kind | How it was found | Severity (the project's own view) |
|---|---|---|---|
| A URL, header or method given to `request` could run commands | CWE-78, command injection | Code review, then shown by a test | High |
| `riv update` and `dessa setup wasm` ran downloads without checking them | CWE-494, download without integrity check | Code review | High |
| `x is Str<>` didn't check a list's contents, so a list of the wrong type could crash a program or print garbage | CWE-843, type confusion | While testing `json` | High |
| `Int("9223372036854775808")` and `Int(1e30)` gave a wrong number, different in native and WebAssembly | CWE-190, CWE-681 | While testing `json` | High |
| Deeply nested JSON crashed the program, uncatchably | CWE-674, uncontrolled recursion | Test | High for servers |
| The compile cache was a shared folder on Linux and macOS, so another user could plant a program | CWE-377, CWE-379 | Code review | Medium |
| One idle connection could stop the `http` server for everybody | CWE-400, resource exhaustion | Code review | Medium |
| `http`'s response headers could be injected; errors told clients internals; oversized requests were cut short and handled anyway | CWE-113, CWE-209, CWE-130 | Code review | Medium |
| Passwords used 100,000 PBKDF2 rounds | CWE-916 | Comparison with OWASP | Medium |
| `dessa serve` let a request reach a sibling folder (`site-secret` beside `site`), and answered requests for any site name | CWE-22, CWE-350 | Code review | Low (it listens only on this computer) |
| Chained method calls, nested generic calls and unclosed `a<b(...` took exponential time to compile | CWE-407 | Test; fuzzing | Medium for anything that compiles others' code |
| The compiler crashed on deep nesting, on invalid UTF-8 and on a long token in an error | CWE-674, CWE-176 | Tests; fuzzing | Low |

## NIST SSDF, version 1.1

Practices are grouped as in the framework. SSDF is written for organisations, so
some of it doesn't fit one person; those are marked and not counted as met.
NIST has a draft version 1.2, not yet final when this was written.

| Practice | Status | Evidence |
|---|---|---|
| **PO: Prepare the organization** | | |
| PO.1 Define security requirements | Met | [security.md](security.md) says what is and isn't protected |
| PO.2 Roles and responsibilities | Not applicable | One maintainer |
| PO.3 Supporting toolchains | Met | CI runs the checks below; `.github/workflows/` |
| PO.4 Criteria for security checks | Met | CI must pass; [CONTRIBUTING.md](../CONTRIBUTING.md) |
| PO.5 Secure environments for development | Partly | Builds on GitHub's machines; a single developer's computer otherwise |
| **PS: Protect the software** | | |
| PS.1 Protect code from unauthorised access | Partly | GitHub account access; branch protection and required 2FA should be confirmed by the maintainer |
| PS.2 Provide a way to verify releases | Met | `SHA256SUMS`, artifact attestations (`release.yml`) |
| PS.3 Archive and protect each release | Met | GitHub Releases, tags |
| **PW: Produce well-secured software** | | |
| PW.1 Design to meet the requirements; threat model | Met | The table at the start of [security.md](security.md) |
| PW.2 Review the design | Partly | This assessment; no outside reviewer |
| PW.4 Reuse well-secured software | Met | SQLite and wasi-sdk (pinned); dependencies kept few |
| PW.5 Follow secure coding practices | Met | Language guarantees; [runtime.md](runtime.md) |
| PW.6 Configure compilers to improve security | Partly | Sanitizers in CI. Hardening flags for generated programs (`_FORTIFY_SOURCE`, stack protector) aren't set. |
| PW.7 Review or analyse code | Partly | CodeQL; code review with AI help; no second person |
| PW.8 Test executable code | Met | `tests/test.py` on three systems, `tests/sanitize.py`, `tests/fuzz.py`, attack tests |
| PW.9 Secure defaults | Met | `http` local by default, `execute` beside `run`, `HttpOnly` cookies |
| **RV: Respond to vulnerabilities** | | |
| RV.1 Identify vulnerabilities continually | Met | Dependabot, CodeQL, weekly fuzzing, a [reporting process](../SECURITY.md) |
| RV.2 Assess and fix | Met | [SECURITY.md](../SECURITY.md) |
| RV.3 Find root causes | Met | Each finding above was fixed at its cause, nearly all with a test |

## CWE Top 25

How each weakness applies. "Prevented" means a Dessa program can't have it, unless
it calls C of its own. "Helped" means Dessa makes the safe way easy, but a program
can still get it wrong.

| CWE | Weakness | In Dessa |
|---|---|---|
| 79 | Cross-site scripting | Helped: `h`, `safeHtml`, `safeUrl`, `textContent`. `innerHTML` is still there. |
| 787, 125 | Out-of-bounds write, read | Prevented |
| 89 | SQL injection | Helped: `?` places in `sql` |
| 352 | Cross-site request forgery | Helped: `SameSite=Lax` cookies by default. No CSRF tokens in `http`. |
| 22 | Path traversal | Helped: `http.files` is checked. `readFile(path)` with a path from a user is up to you. |
| 416 | Use after free | Prevented |
| 78 | OS command injection | Helped: `execute` has no shell. `run` is still there, with a warning. |
| 862, 863, 306 | Missing authorisation or authentication | Up to the program; `server.use` gives it a place |
| 434 | Unrestricted file upload | Up to the program |
| 94 | Code injection | Prevented: Dessa has no `eval` |
| 20 | Improper input validation | Helped: `JsonSchema`, `validate`, static types |
| 77 | Command injection | As 78 |
| 287 | Improper authentication | Helped: `hashPassword` |
| 269 | Improper privilege management | Not applicable: nothing in Dessa uses privileges |
| 502 | Deserialization of untrusted data | Prevented: JSON only fills the class you name |
| 200 | Exposing sensitive information | Helped: `http` hides handler errors |
| 119, 120 | Buffer errors | Prevented |
| 190 | Integer overflow | Prevented |
| 476 | Null dereference | Prevented by the type checker |
| 798 | Hard-coded credentials | Up to the program |
| 918 | Server-side request forgery | Helped only: `request` allows only http and https |
| 400 | Resource exhaustion | Partly: limits in `http`, `json`; your own code has none |

This table was written from the weakness names; check it against the
[current list](https://cwe.mitre.org/top25/), which MITRE updates each year.

## SLSA, build track

| Level | Asks for | Status |
|---|---|---|
| Build L1 | Provenance exists | Met from the next release |
| Build L2 | Signed provenance, from a hosted build platform | Met from the next release: GitHub [artifact attestations](https://docs.github.com/en/actions/concepts/security/artifact-attestations), `release.yml` |
| Build L3 | A hardened build platform, with isolated builds and signing keys out of the build's reach | Not met. GitHub suggests building in a shared reusable workflow for this. |

## OpenSSF Best Practices Badge, passing level

The project hasn't applied. The known gaps, from reading [the criteria](https://www.bestpractices.dev/en/criteria/0):

- Applying means answering the whole questionnaire honestly; a few items may need
  more work than listed here.
- Tests should be added for each new feature, as a stated policy: add this to
  [CONTRIBUTING.md](../CONTRIBUTING.md).
- Two-factor authentication for the maintainer should be confirmed.

## What remains

In rough order of value:

1. **An outside review.** Everything here is one project's view of itself. Even an
   informal review by someone experienced would be worth more than the rest of this list.
2. **Turn on GitHub's own protections** (maintainer's settings): private vulnerability
   reporting, secret scanning, Dependabot alerts, branch protection for `main`, 2FA.
3. **Hardening flags for generated programs** (SSDF PW.6): `-fstack-protector-strong`,
   `-D_FORTIFY_SOURCE=2`, measuring what they cost.
4. **`dcrypto`:** Argon2id for passwords, and considering a reviewed library in place of
   our own AES and SHA code.
5. **Signed packages** in `riv`, and a way to pin the first install.
6. **ISO/IEC 24772**, if a copy can be had, as a check on the language design.
7. **SLSA Build Level 3**, with a reusable release workflow.
8. **HTTPS and rate limiting in `http`**, or clear guidance for running behind a proxy (given now).
