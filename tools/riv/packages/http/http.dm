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
 * fits answers; none fits: 404. A handler that throws: 500 with the message.
 * Each request is printed: GET /hello/Ann 200.
 *
 * server.handle(new Request("GET", "/hello/Ann")) answers without the
 * network, which is handy for tests.
 *
 * One request at a time (no threads). Native programs only: WebAssembly
 * can't open a server. For calling other servers, use $("request").
 */

$("http") { _listen, _accept, _read, _write, _close }   // native part (http.hpp / http.c)

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
}

// What the server answers
class Response {
    var[Int] status = 200;
    var[Map[Str, Str]] headers = {:};
    var[Str] body = "";

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
    var[Bul] quiet = false;            // true: don't print each request
    var[_Route<>] _routes = <>;
    var[Bul] _stopping = false;

    init(port[Int] = 8080) {
        this.port = port;
    }

    func[Emp] get(path[Str], handler[Func[Response, [Request]]]) { this.route("GET", path, handler); }
    func[Emp] post(path[Str], handler[Func[Response, [Request]]]) { this.route("POST", path, handler); }
    func[Emp] put(path[Str], handler[Func[Response, [Request]]]) { this.route("PUT", path, handler); }
    func[Emp] delete(path[Str], handler[Func[Response, [Request]]]) { this.route("DELETE", path, handler); }

    // Any method ("PATCH", ...); "*" answers every method
    func[Emp] route(method[Str], path[Str], handler[Func[Response, [Request]]]) {
        this._routes.push(new _Route(method.upper(), path, handler));
    }

    // The answer to one request, without the network
    func[Response] handle(req[Request]) {
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
                return text($"{typeof(e)}: {e.message}", 500);
            } catch (e[Str]) {
                return text(e, 500);
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
        var[Int] socket = _listen(this.port);
        if (!this.quiet) print($"Listening on http://localhost:{this.port}");
        var[Int] answered = 0;
        while (!this._stopping && (count <= 0 || answered < count)) {
            var[Int] conn = _accept(socket);
            if (conn < 0) continue;
            var[Str] raw = _read(conn);
            if (raw == "") {
                _close(conn);
                continue;
            }
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
            if (!this.quiet) print($"{req.method} {req.path} {res.status}");
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

// Content-Length counts bytes, not characters
func[Int] _byteLength(text[Str]) {
    var[Int] n = 0;
    for (var[Chr] c in text) {
        var[Int] code = c.code();
        n += code < 128 ? 1 : code < 2048 ? 2 : code < 65536 ? 3 : 4;
    }
    return n;
}

func[Str] _serialize(res[Response], headOnly[Bul]) {
    var[Str] reason = _REASONS.has(res.status) ? _REASONS<res.status> : "Status";
    var[Str] out = $"HTTP/1.1 {res.status} {reason}\r\n";
    for (var[Str] name in res.headers) out += $"{name}: {res.headers<name>}\r\n";
    out += $"Content-Length: {_byteLength(res.body)}\r\nConnection: close\r\n\r\n";
    return headOnly ? out : out + res.body;
}
