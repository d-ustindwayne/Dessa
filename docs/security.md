# Security

What Dessa protects against, how, and what it leaves to you. To report a
problem, see [SECURITY.md](../SECURITY.md). For how this compares with the
published frameworks, see [security-assessment.md](security-assessment.md).

## Who could attack what

| What is at stake | Who it's protected from | How it could be attacked |
|---|---|---|
| The computer that **compiles** | A hostile source file or package | Crashing or hanging the compiler; a package running something when it is installed |
| A Dessa **program** and its data | Input from outside: files, the network, users | Bad JSON, long text, a URL that turns into a command, memory errors |
| A **web page** made with Dessa, and its visitors | Text from users | Script injection (XSS) |
| A **server** made with Dessa | Anyone who can connect | Slow or huge requests, reading files outside a folder, header injection |
| **Dessa itself**, as you download it | Someone between you and GitHub, or who has changed a file | A changed compiler, package or installer |

Dessa does **not** try to:

- **Run hostile code safely.** A Dessa program can do anything its user can:
  read files, start programs. Compiling is safer than running (no code from the
  source file runs while compiling), but to run a program you don't trust, use a
  sandbox such as a container. In a browser, WebAssembly and the page's own
  rules are that sandbox.
- **Be safe from someone who already controls your computer** or your `PATH`.
- **Stop a program using up memory or time.** There are limits on what comes in
  (see below), not on what your own code does.

## The language

These hold for every program, in every backend, unless it calls C or C++ of its
own through a package.

| Weakness (CWE) | What Dessa does |
|---|---|
| Out-of-bounds read or write (CWE-125, CWE-787) | Every list, text and map access is checked. A bad index is an `IndexError`. |
| Use after free, double free (CWE-416, CWE-415) | Values are counted and freed by generated code; a program can't free anything itself. |
| Null dereference (CWE-476) | `nil` is part of the type (`T \| Nil`), and the compiler won't let you use one without checking. |
| Integer overflow (CWE-190) | `+ - * ^ ++ --`, and turning text or a decimal into an `Int`, are checked. Too big is an `OverflowError`, never a wrong number. |
| Wrong kind of value (CWE-843) | Static types; `is` on a collection also looks at what it holds. |
| Uninitialised memory (CWE-908) | Memory for values is zeroed; variables must have a value or a type's default. |
| Buffer overflow in text (CWE-120) | Text carries its length. |
| Invalid text (CWE-176) | Source files must be valid UTF-8; text is counted in characters, not bytes. |
| Running out of stack (CWE-674) | Too much recursion ends with an error message and the line, not a bare crash. Compiling has limits too (see below). |

**Known limits:**

- Stack overflow can't be caught with `catch`: the program ends.
- `x is Func[...]` only tells that `x` is a function, not its signature, in the C
  backend. Calling it with the wrong arguments isn't checked. Don't narrow an
  `Obj` to a function type from data you don't control.
- A list of objects that is *empty* fits any `is Foo<>`, in the C backend (it has
  nothing to tell it by). Nothing can be read from it as the wrong type, so this
  is harmless.
- When an error is caught, the temporary values of the interrupted statement
  aren't freed. A little memory per error; it can matter only for a program that
  catches errors in a loop millions of times.
- Reference counts aren't atomic. Dessa programs are single-threaded, so this
  is safe today, but it would have to change before threads are added.
- `a / b` on Ints goes through a `Dbl`, so past 2^53 it loses digits. Use
  `idiv(a, b)` from `math` where every digit matters.
- Dessa has no constant-time guarantees for your own code. For secrets, use
  `dcrypto`'s functions, which compare in constant time.

## The compiler

A source file can't make the compiler crash or run for very long:

- **No code runs while compiling.** There are no macros, build scripts or
  install hooks.
- **Depth limits.** Nesting beyond 200 levels, chains of more than 1000 operators
  or calls, and types nested more than 32 levels are error messages. The analyser
  and generators walk the tree recursively, so without limits a small file could
  overflow their stack.
- **No exponential work.** Parsing and type-checking take time in proportion to
  the code. Three cases that doubled at each level (chained method calls, nested
  generic calls, unclosed `a<b(c<d(...`) were found and fixed; tests stop them
  coming back.
- **Fuzz tested.** `tests/fuzz.py` feeds the compiler damaged programs.
- **Generated code stays generated.** The C compiler's own errors are never
  shown. If it fails, that's reported as a Dessa bug.

The compile cache holds programs that are then run, so it has to be private. It
is in `~/.cache/dessa` (mode 700; the compiler refuses a folder owned by someone
else) on Linux and macOS, and in your own `%TEMP%\dessa` on Windows.

## Libraries

### Running other programs: `os`

| Function | Use it for |
|---|---|
| `execute(program, args)` | **The safe default.** No shell is involved, so arguments are only ever arguments, whatever they hold. On Windows, batch files are refused, because they go through the command shell. |
| `run(text)` | Text you wrote yourself, when you need the shell. **Never** put in it anything a user typed or that came from a file or the network: that's command injection (CWE-78). |

### Calling servers: `request`

Nothing you pass reaches a shell: the URL, method and headers go to `curl` in a
config file, in a new folder only you can read. Methods and header names are
checked, and a line break in a header value is an error (so a value can't add a
header). Only `http://` and `https://` are followed, also through redirects, so a
hostile server can't redirect you to `file:///etc/passwd`.

Addresses you take from users can still point at your own network (CWE-918,
server-side request forgery): check them if your program runs on a server.

### Web servers: `http`

| By default | Why |
|---|---|
| Only this computer can connect | `new Server(8080, "0.0.0.0")` opens it up, on purpose |
| A client has 10 seconds to send its request (`timeoutSeconds`) | Slow clients can't hold connections for ever |
| Bodies up to 8 MB (`maxBytes`), headers up to 64 KB | 413 or 431 otherwise |
| Clients are read side by side | One slow or idle client doesn't hold up the others |
| Odd requests are refused: chunked bodies, a length given twice or written strangely | Stops request smuggling through a server in front |
| A handler's error is a plain 500 | The reason is printed on the server, not sent to clients (`debug = true` to change it while developing) |
| Line breaks are taken out of header values | `redirect(whatTheyTyped)` can't add a header |
| `X-Content-Type-Options: nosniff` | Browsers go by the type you sent |
| `cookie(...)` sets `HttpOnly` and `SameSite=Lax` | Add `secure: true` on a real site |
| `files(path, folder)` serves only inside the folder, and no hidden files | `..`, `%2e%2e` and `.env` are a 404 |

**Not included**, so put it in front of your server for anything public: HTTPS
(use a reverse proxy such as Caddy or nginx), rate limiting and CSRF protection.
Handlers run one at a time, so a slow handler delays the others.

### Web pages: `webutils`

| Use | Instead of | Because |
|---|---|---|
| `h(tag, attrs, children...)` or `el.textContent` | `el.innerHTML = ...` with text from users | Text stays text (CWE-79) |
| `safeHtml("<b>{}</b>", name)` | `"<b>" + name + "</b>"` | Values are escaped |
| `safeUrl(url)` | a link from a user as it is | `javascript:` links run scripts |

`h` refuses `on...` attributes and passes links through `safeUrl`. `innerHTML`
is still there, because it's the browser's own: it's up to you what goes in.

`store` and `cookies` can be read by any script on the page, so keep login tokens
in `HttpOnly` cookies set by the server. Checks with `validate` help your users;
a server has to check again.

Pages are compiled to WebAssembly and contain no generated JavaScript, only the
fixed bridge in `runtime/dessa_wasm.js`. A Content-Security-Policy has to allow
that: the loader and the program are inside the page, so expect to need
`script-src 'unsafe-inline' 'wasm-unsafe-eval'` (not tested yet).

### Databases: `sql`

Values after the SQL fill `?` places, so they are data (CWE-89). Never build SQL
by joining text you were given.

### Reading data: `json`

Nesting deeper than 512 levels is a `JsonError` rather than a stack overflow
(`parseJson(text, maxDepth)` changes it). Numbers keep every digit. `JsonSchema`
checks the shape of what arrived. `parseJsonAs:T` only fills the fields of the
class you name; it never creates other types or runs code.

### Secrets: `dcrypto`

| For | Use | Details |
|---|---|---|
| Passwords | `hashPassword`, `verifyPassword` | PBKDF2-HMAC-SHA256, 600,000 rounds ([OWASP's figure](https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html)), a random salt each time, constant-time comparison. Older hashes still verify. |
| Encrypting | `encrypt`, `decrypt` | AES-256-GCM, a key from PBKDF2 as above, a random salt and nonce each time. A wrong password or changed data fails. |
| Random secrets | `token(bytes)` | The system's secure source |
| Checksums | `sha256`, `sha512`, `hmac` | `md5` and `sha1` are for old formats only |

`$("random")` and `math.random` are **not** for secrets: they are predictable.

`dcrypto` was written for Dessa and has had no outside review. Its tests compare
it with known answers, but for high-stakes use, prefer a reviewed
library such as libsodium through a package. OWASP's first choice for passwords
is Argon2id; Dessa has PBKDF2, which OWASP accepts, and which needs no extra code.

## Where Dessa comes from

| Step | Protection |
|---|---|
| Installing with `riv update`, `dessa setup wasm`, the npm package | HTTPS, and each download is checked against the release's `SHA256SUMS` before anything in it is unpacked or run. |
| Checking a release yourself | `gh attestation verify <file> --repo d-ustindwayne/Dessa` proves it was built by this repository's workflow (a signed [SLSA](https://slsa.dev) provenance). |
| The build | Runs on GitHub's machines from a tag. GitHub Actions are pinned to exact commits, and wasi-sdk (the WebAssembly compiler's source) to a SHA-256. Dependabot proposes updates. |
| Packages | `riv.lock` records a SHA-256 of each installed package, and a change is refused. The index in this repository names each version's BitTorrent info hash, which covers its pieces. |

**Known limits:**

- `SHA256SUMS` comes from the same place as the file it checks. It catches damage
  and changed mirrors; somebody with control of the GitHub repository could change
  both, which is what the attestation is for.
- BitTorrent info hashes use SHA-1, which is what the protocol has. `riv.lock`'s
  SHA-256 protects you only *after* the first install.
- Packages aren't signed, and a package with a `.native` file runs its C or C++
  in your program, like any other dependency. Read what you install.

## How this is kept true

| Check | What it does | When |
|---|---|---|
| `tests/test.py` | Includes attacks on `request`, `http`, `dessa serve`, `os.execute`, and depth limits | Every push, on Windows, Linux and macOS |
| `tests/sanitize.py` | Runs the test programs under AddressSanitizer and UndefinedBehaviorSanitizer | Every push |
| `tests/fuzz.py` | Damaged programs into the compiler | Every push, briefly; each week for longer |
| `tests/web/chrome_utils.py` | `webutils` in real Chrome, with script injection attempts | Every push |
| CodeQL | Static analysis of the C and C++ | Every push, and weekly |
| Dependabot | Updates for GitHub Actions and the website's packages | Weekly |

See [CONTRIBUTING.md](../CONTRIBUTING.md) for how to run them.
