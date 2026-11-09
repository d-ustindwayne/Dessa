# request

HTTP requests: `get`, `post` and the rest, with the answer as a `Response`.

**Install:** Install with `riv add request`.

**Runs in:** Native programs (through the `curl` program, which Windows 10+, macOS and most Linux systems have) and WebAssembly.

## Example

```dessa
$("request") { get, postJson, Response, RequestError }
$("json") { parseJson }

try {
    var[Response] r = get("https://example.com/api/users");
    r.raiseForStatus();
    print(r.status, r.header("content-type"));
    print(r.json().at(0).get("name").asStr());

    postJson("https://example.com/api/users", parseJson("{\"name\": \"Ann\"}"));
} catch (e[RequestError]) {
    print("failed:", e.message);
}
```

## What it has

| Function | |
|---|---|
| `get(url, headers, timeout)` | |
| `post(url, body, headers, timeout)` | |
| `put(url, body, headers, timeout)` | |
| `patch(url, body, headers, timeout)` | |
| `delete(url, headers, timeout)` | |
| `postJson(url, data, headers, timeout)` | sends a `Json` value |
| `request(method, url, headers, body, timeout)` | any method |

The timeout is in seconds (30 unless you say otherwise).

A `Response` has `status`, `ok` (true for 2xx), `text` (the body), `headers`,
`header(name)`, `json()` and `raiseForStatus()`. Failures are a `RequestError`.
