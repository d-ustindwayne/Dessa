# http

A small web server: routes with `:params`, answers as text, HTML or JSON, static files.

**Install:** Install with `riv add http`.

**Runs in:** Native programs only: WebAssembly can't open a server.

## Example

```dessa
$("http") { Server, Request, text, html, json }

var[Server] server = new Server(8080);
server.get("/", (req[Request]) := html("<h1>Hello from Dessa</h1>"));
server.get("/hello/:name", (req[Request]) := text($"Hello, {req.params<"name">}!"));
server.post("/echo", (req[Request]) := json(req.body));
server.files("/assets", "public");
server.listen();
```

## What it has

| Part | Names |
|---|---|
| `Server` | `get`, `post` and the other methods, `use(handler)`, `files(url, folder)`, `listen()`, `handle(request)` (answers without the network, for tests) |
| Settings | `server.timeoutSeconds` (10), `server.maxBytes` (8 MB), `server.debug` |
| `Request` | `method`, `path`, `query`, `headers`, `body`, `params`, `form()`, `cookies()` |
| Answers | `text`, `html`, `json`, `redirect`, `new Response(status, body, type)`, with `.header(name, value)` and `.cookie(name, value)` |

`*` at the end of a route takes the rest of the path. `use` runs before every route,
and answers in its place when it gives a `Response`.

No route gives a 404. A handler that throws gives a 500, with the reason printed on the
server (`server.debug = true` sends it to the client too, while developing).

## Good to know

Only this computer can reach the server unless you say otherwise:
`new Server(8080, "0.0.0.0")`.

Clients are read side by side, but handlers run one at a time.

There is no HTTPS: put a reverse proxy in front of anything public. See
[docs/security.md](../../../../docs/security.md).
