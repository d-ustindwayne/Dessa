/***
 * $("http") -- a small web server.
 *
 *     $("http") { Server, Request, Response, text, html, json }
 *
 *     var[Server] server = new Server(8080);
 *     server.get("/", (req[Request]) := html("<h1>Hello from Dessa</h1>"));
 *     server.get("/hello/:name", (req[Request]) := text($"Hello, {req.params<"name">}!"));
 *     server.get("/add", (req[Request]) := text(Str(Int(req.query<"a">) + Int(req.query<"b">))));
 *     server.post("/echo", (req[Request]) := json(req.body));
 *     server.listen();          // http://localhost:8080 -- Ctrl+C stops it
 *
 * A route is a path; `:name` matches one part of it (req.params<"name">) and
 * a `*` at the end matches the rest (req.params<"*">). The first route that
 * fits answers; none fits: 404. A handler that throws: 500 (the client sees
 * only that; the reason is printed here, and sent too with server.debug = true).
 * Each request is printed: GET /hello/Ann 200.
 *
 * Only this computer can reach a server unless you say otherwise:
 * new Server(8080, "0.0.0.0") listens on every network.
 *
 * server.files("/assets", "public") serves a folder. server.use(...) runs
 * before every route, and answers instead of it when it gives a Response.
 * req.cookies() reads cookies, response.cookie(name, value) sets one.
 *
 * Clients are read side by side, so a slow one doesn't hold up the others:
 * it has server.timeoutSeconds to send its request, of at most server.maxBytes.
 * Handlers run one at a time (no threads).
 *
 * server.handle(new Request("GET", "/hello/Ann")) answers without the
 * network, which is handy for tests.
 *
 * Native programs only: WebAssembly can't open a server. For calling other
 * servers, use $("request").
 */

$("http") { _listen, _port, _next, _take, _write, _close, _size }   // native part (http.hpp / http.c)
$("os") { readFile, fileExists, isDir }

class HttpError(Error) {}

// What the browser (or another program) asked for
class Request {
    var[Str] method = "GET";
    var[Str] path = "/";                        // without the ?query
    var[Map[Str, Str]] query = {:};             // ?a=1&b=2 (decoded)
    var[Map[Str, Str]] headers = {:};           // names in lower case: req.headers<"content-type">
    var[Str] body = "";
    var[Map[Str, Str]] params = {:};            // :name parts of the route

    // A request made by hand (server.handle, tests): "GET", "/path?a=1"
    init(method[Str] = "GET", target[Str] = "/", body[Str] = "") {
        this.method = method.upper();
        this.body = body;
        var[Int] q = target.indexOf("?");
        if (q >= 0) {
            this.path = _decode(target.substr(0, q));
            this.query = parseQuery(target.substr(q + 1, target.length() - q - 1));
        } else {
            this.path = _decode(target);
        }
    }

    // A header's value, or nil (the name in any case)
    func[Str | Nil] header(name[Str]) {
        return this.headers.get(name.lower());
    }

    // The body of a form post (a=1&b=2)
    func[Map[Str, Str]] form() {
        return parseQuery(this.body);
    }

    // The cookies the client sent
    func[Map[Str, Str]] cookies() {
        var[Map[Str, Str]] out = {:};
        for (var[Str] pair in this.headers.getOr("cookie", "").split(";")) {
            var[Int] eq = pair.indexOf("=");
            if (eq > 0) out<pair.substr(0, eq).trim()> = pair.substr(eq + 1, pair.length() - eq - 1).trim();
        }
        return out;
    }
}

// What the server answers
class Response {
    var[Int] status = 200;
    var[Map[Str, Str]] headers = {:};
    var[Str] body = "";
    var[Str<>] _cookies = <>;
    var[Str] _problem = "";            // why a handler failed, for the server's own log

    init(status[Int] = 200, body[Str] = "", contentType[Str] = "text/plain; charset=utf-8") {
        this.status = status;
        this.body = body;
        this.headers<"Content-Type"> = contentType;
    }

    // Adds a header and gives the response back: text("hi").header("Cache-Control", "no-store")
    func[Response] header(name[Str], value[Str]) {
        this.headers<name> = value;
        return this;
    }

    // Sets a cookie. By default scripts in the page can't read it, and it isn't
    // sent along with requests started from other sites. maxAge in seconds
    // (-1: until the browser closes; 0: delete it).
    // secure: true sends it over https only: use that on a real site.
    func[Response] cookie(name[Str], value[Str], maxAge[Int] = -1, path[Str] = "/", httpOnly[Bul] = true, secure[Bul] = false, sameSite[Str] = "Lax") {
        for (var[Str] part in <name, value, path, sameSite>) {
            if (part.includes(";") || part.includes("\r") || part.includes("\n")) throw new HttpError($"a cookie can't hold '{part}': ; and line breaks aren't allowed");
        }
        var[Str] line = $"{name}={value}; Path={path}; SameSite={sameSite}";
        if (maxAge >= 0) line += $"; Max-Age={maxAge}";
        if (httpOnly) line += "; HttpOnly";
        if (secure) line += "; Secure";
        this._cookies.push(line);
        return this;
    }

    func[Str] toStr() {
        return $"Response({this.status}, {this.body.length()} characters)";
    }
}

func[Response] text(body[Str], status[Int] = 200) {
    return new Response(status, body, "text/plain; charset=utf-8");
}

func[Response] html(body[Str], status[Int] = 200) {
    return new Response(status, body, "text/html; charset=utf-8");
}

// `body` is JSON text already (make it with $("json")'s stringify)
func[Response] json(body[Str], status[Int] = 200) {
    return new Response(status, body, "application/json");
}

// A redirect to another address
func[Response] redirect(to[Str], status[Int] = 302) {
    return new Response(status, "", "text/plain; charset=utf-8").header("Location", to);
}

class _Route {
    var[Str] method = "";
    var[Str<>] parts = <>;
    var[Func[Response, [Request]]] handler;

    init(method[Str], pattern[Str], handler[Func[Response, [Request]]]) {
        this.method = method;
        this.parts = _segments(pattern);
        this.handler = handler;
    }

    // The :params when `path` fits, else nil
    func[Map[Str, Str] | Nil] fits(path[Str]) {
        var[Str<>] got = _segments(path);
        var[Map[Str, Str]] params = {:};
        for (var[Int] i = 0; i < this.parts.length(); i++) {
            var[Str] part = this.parts<i>;
            if (part == "*" && i == this.parts.length() - 1) {
                params<"*"> = got.slice(i, got.length()).join("/");
                return params;
            }
            if (i >= got.length()) return nil;
            if (part.startsWith(":")) params<part.substr(1, part.length() - 1)> = got<i>;
            else if (part != got<i>) return nil;
        }
        return got.length() == this.parts.length() ? params : nil;
    }
}

class Server {
    var[Int] port = 8080;
    var[Str] host = "127.0.0.1";       // "0.0.0.0": every network, not only this computer
    var[Bul] quiet = false;            // true: don't print each request
    var[Bul] debug = false;            // true: a handler's error goes to the client too (never on a real site)
    var[Int] timeoutSeconds = 10;      // to send a whole request
    var[Int] maxBytes = 8388608;       // the largest body taken (8 MB)
    var[_Route<>] _routes = <>;
    var[Func[Response | Nil, [Request]]<>] _before = <>;
    var[Bul] _stopping = false;

    // Port 0: the system chooses a free one (server.port tells which, once listening)
    init(port[Int] = 8080, host[Str] = "127.0.0.1") {
        this.port = port;
        this.host = host;
    }

    // Runs before every route, in the order added. A Response answers the
    // request there and then (a login check, say); nil goes on.
    func[Emp] use(step[Func[Response | Nil, [Request]]]) {
        this._before.push(step);
    }

    // Serves the files of `folder` under `path`. Only what is inside the folder,
    // and not hidden files like .env. A folder's page is its index.html.
    func[Emp] files(path[Str], folder[Str]) {
        var[Str] pattern = path.endsWith("/") ? path + "*" : path + "/*";
        this.get(pattern, (req[Request]) := _fileResponse(folder, req.params.getOr("*", "")));
    }

    func[Emp] get(path[Str], handler[Func[Response, [Request]]]) { this.route("GET", path, handler); }
    func[Emp] post(path[Str], handler[Func[Response, [Request]]]) { this.route("POST", path, handler); }
    func[Emp] put(path[Str], handler[Func[Response, [Request]]]) { this.route("PUT", path, handler); }
    func[Emp] delete(path[Str], handler[Func[Response, [Request]]]) { this.route("DELETE", path, handler); }

    // Any method ("PATCH", ...); "*" answers every method
    func[Emp] route(method[Str], path[Str], handler[Func[Response, [Request]]]) {
        this._routes.push(new _Route(method.upper(), path, handler));
    }

    func[Response] _failed(reason[Str]) {
        var[Response] res = text(this.debug ? reason : "Internal Server Error", 500);
        res._problem = reason;
        return res;
    }

    // The answer to one request, without the network
    func[Response] handle(req[Request]) {
        try {
            for (var[Func[Response | Nil, [Request]]] step in this._before) {
                var[Response | Nil] early = step(req);
                if (early != nil) return early;
            }
        } catch (e[Error]) {
            return this._failed($"{typeof(e)}: {e.message}");
        } catch (e[Str]) {
            return this._failed(e);
        } catch {
            return this._failed("a value was thrown that isn't an error");
        }
        var[Bul] pathFits = false;
        for (var[_Route] r in this._routes) {
            var[Map[Str, Str] | Nil] params = r.fits(req.path);
            if (params == nil) continue;
            pathFits = true;
            if (r.method != "*" && r.method != req.method && !(r.method == "GET" && req.method == "HEAD")) continue;
            req.params = params;
            try {
                return r.handler(req);
            } catch (e[Error]) {
                return this._failed($"{typeof(e)}: {e.message}");
            } catch (e[Str]) {
                return this._failed(e);
            } catch {
                return this._failed("a value was thrown that isn't an error");
            }
        }
        return pathFits ? text("Method Not Allowed", 405) : text($"Not Found: {req.path}", 404);
    }

    // Stops listen() after the request it is answering
    func[Emp] stop() {
        this._stopping = true;
    }

    // Answers requests until stop() (or Ctrl+C). `count` > 0: only that many.
    func[Emp] listen(count[Int] = 0) {
        var[Int] socket = _listen(this.port, this.host);
        this.port = _port(socket);
        if (!this.quiet) print($"Listening on http://{this.host == "127.0.0.1" ? "localhost" : this.host}:{this.port}");
        var[Int] answered = 0;
        while (!this._stopping && (count <= 0 || answered < count)) {
            var[Int] conn = _next(socket, this.timeoutSeconds, this.maxBytes);
            if (conn < 0) break;
            var[Str] raw = _take(conn);
            var[Response] res = text("Bad Request", 400);
            var[Request] req = new Request();
            try {
                req = _parse(raw);
                res = this.handle(req);
            } catch (e[Error]) {
                res = text("Bad Request", 400);
            }
            _write(conn, _serialize(res, req.method == "HEAD"));
            _close(conn);
            answered++;
            if (!this.quiet) print(res._problem == "" ? $"{req.method} {req.path} {res.status}" : $"{req.method} {req.path} {res.status} ({res._problem})");
        }
        _close(socket);
    }
}

// "a=1&b=two%20words" -> {"a": "1", "b": "two words"}
func[Map[Str, Str]] parseQuery(text[Str]) {
    var[Map[Str, Str]] out = {:};
    if (text == "") return out;
    for (var[Str] pair in text.split("&")) {
        if (pair == "") continue;
        var[Int] eq = pair.indexOf("=");
        if (eq < 0) out<_decode(pair)> = "";
        else out<_decode(pair.substr(0, eq))> = _decode(pair.substr(eq + 1, pair.length() - eq - 1));
    }
    return out;
}

func[Str<>] _segments(path[Str]) {
    var[Str<>] out = <>;
    for (var[Str] s in path.split("/")) if (s != "") out.push(s);
    return out;
}

func[Int] _hexValue(c[Chr]) {
    if (c.isDigit()) return c.toInt();
    var[Chr] l = c.lower();
    if (l >= 'a' && l <= 'f') return l.code() - 'a'.code() + 10;
    return -1;
}

// %20 and + into spaces, %C3%A9 into é
func[Str] _decode(text[Str]) {
    if (!text.includes("%") && !text.includes("+")) return text;
    var[Int<>] bytes = <>;
    var[Chr<>] cs = text.chars();
    var[Str] out = "";
    var[Int] i = 0;
    while (i < cs.length()) {
        var[Chr] c = cs<i>;
        if (c == '%' && i + 2 < cs.length() && _hexValue(cs<i + 1>) >= 0 && _hexValue(cs<i + 2>) >= 0) {
            bytes.push(_hexValue(cs<i + 1>) * 16 + _hexValue(cs<i + 2>));
            i += 3;
            continue;
        }
        out += _utf8(bytes);
        bytes = <>;
        out += c == '+' ? " " : Str(c);
        i++;
    }
    return out + _utf8(bytes);
}

// UTF-8 bytes as text
func[Str] _utf8(bytes[Int<>]) {
    var[Str] out = "";
    var[Int] i = 0;
    while (i < bytes.length()) {
        var[Int] b = bytes<i>;
        var[Int] extra = b >= 240 ? 3 : b >= 224 ? 2 : b >= 192 ? 1 : 0;
        var[Int] code = extra == 3 ? b - 240 : extra == 2 ? b - 224 : extra == 1 ? b - 192 : b;
        for (var[Int] k = 1; k <= extra && i + k < bytes.length(); k++) code = code * 64 + (bytes<i + k> - 128);
        out += Str(Chr(code));
        i += extra + 1;
    }
    return out;
}

func[Request] _parse(raw[Str]) {
    var[Int] split = raw.indexOf("\r\n\r\n");
    var[Str] head = split >= 0 ? raw.substr(0, split) : raw;
    var[Str] body = split >= 0 ? raw.substr(split + 4, raw.length() - split - 4) : "";
    var[Str<>] lines = head.split("\r\n");
    var[Str<>] first = lines<0>.split(" ");
    if (first.length() < 2) throw new HttpError("bad request line");
    var[Request] req = new Request(first<0>, first<1>, body);
    for (var[Int] i = 1; i < lines.length(); i++) {
        var[Int] colon = lines<i>.indexOf(":");
        if (colon > 0) req.headers<lines<i>.substr(0, colon).trim().lower()> = lines<i>.substr(colon + 1, lines<i>.length() - colon - 1).trim();
    }
    return req;
}

const[Map[Int, Str]] _REASONS = {200: "OK", 201: "Created", 204: "No Content", 301: "Moved Permanently", 302: "Found",
                                 304: "Not Modified", 400: "Bad Request", 401: "Unauthorized", 403: "Forbidden",
                                 404: "Not Found", 405: "Method Not Allowed", 500: "Internal Server Error"};

// A value can't start a header or a response of its own: redirect(whatTheUserTyped)
func[Str] _oneLine(text[Str]) {
    return text.replaceAll("\r", "").replaceAll("\n", "");
}

func[Str] _serialize(res[Response], headOnly[Bul]) {
    var[Str] reason = _REASONS.has(res.status) ? _REASONS<res.status> : "Status";
    var[Str] out = $"HTTP/1.1 {res.status} {reason}\r\n";
    for (var[Str] name in res.headers) out += $"{_oneLine(name)}: {_oneLine(res.headers<name>)}\r\n";
    for (var[Str] line in res._cookies) out += $"Set-Cookie: {_oneLine(line)}\r\n";
    // Browsers go by Content-Type, and don't guess that a text upload is a script
    if (!res.headers.has("X-Content-Type-Options")) out += "X-Content-Type-Options: nosniff\r\n";
    out += $"Content-Length: {_size(res.body)}\r\nConnection: close\r\n\r\n";
    return headOnly ? out : out + res.body;
}

const[Map[Str, Str]] _TYPES = {"html": "text/html; charset=utf-8", "htm": "text/html; charset=utf-8", "css": "text/css; charset=utf-8",
                               "js": "text/javascript; charset=utf-8", "mjs": "text/javascript; charset=utf-8", "json": "application/json",
                               "txt": "text/plain; charset=utf-8", "md": "text/plain; charset=utf-8", "csv": "text/csv; charset=utf-8",
                               "svg": "image/svg+xml", "png": "image/png", "jpg": "image/jpeg", "jpeg": "image/jpeg", "gif": "image/gif",
                               "webp": "image/webp", "ico": "image/x-icon", "pdf": "application/pdf", "wasm": "application/wasm",
                               "woff": "font/woff", "woff2": "font/woff2", "mp3": "audio/mpeg", "mp4": "video/mp4", "webm": "video/webm"};

// One file of server.files(). `wanted` comes from the client, already decoded
func[Response] _fileResponse(folder[Str], wanted[Str]) {
    var[Response] missing = text("Not Found", 404);
    if (wanted.includes("\\") || wanted.includes(":")) return missing;
    var[Str<>] parts = wanted.split("/");
    for (var[Str] part in parts) {
        if (part.startsWith(".")) return missing;     // .. leaves the folder; .env and .git are private
    }
    var[Str] path = folder;
    for (var[Str] part in parts) if (part != "") path += "/" + part;
    if (isDir(path)) path += "/index.html";
    if (!fileExists(path) || isDir(path)) return missing;
    var[Str<>] pieces = path.split(".");
    var[Str] extension = pieces<pieces.length() - 1>.lower();
    return new Response(200, readFile(path), _TYPES.getOr(extension, "application/octet-stream"));
}
