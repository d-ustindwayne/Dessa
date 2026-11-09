/***
 * $("request") -- HTTP requests, like Python's requests library.
 *
 *     $("request") { get, post }
 *
 *     var[Response] r = get("https://api.github.com/repos/python/cpython");
 *     print(r.status, r.ok);                      // 200 true
 *     print(r.json().get("full_name").asStr());   // python/cpython
 *
 *     var[Response] sent = post("https://httpbin.org/post", body: "hello",
 *                               headers: {"Content-Type": "text/plain"});
 *
 * A failed connection (no internet, bad host, timeout) throws a RequestError.
 * An HTTP error status (404, 500, ...) does not throw: check r.ok or r.status,
 * or call r.raiseForStatus().
 *
 * Uses the `curl` program, which comes with Windows 10+, macOS and Linux.
 */

$("request") { _send }            // native part (request.hpp)
$("json") { Json, parseJson, stringify }

class RequestError(Error) {}   // an Error: e.message, prints as RequestError: ...

class Response {
    var[Str] url = "";
    var[Int] status = 0;
    var[Bul] ok = false;                   // true for status 200-299
    var[Str] text = "";                    // the body
    var[Map[Str, Str]] headers = {:};      // header names in lower case

    // The body parsed as JSON (see $("json")).
    func[Json] json() {
        return parseJson(this.text);
    }

    // A header by name (any case), or nil.
    func[Str | Nil] header(name[Str]) {
        return this.headers.get(name.lower());
    }

    // Throws a RequestError if the status isn't 2xx.
    func[Emp] raiseForStatus() {
        if (!this.ok) {
            throw new RequestError($"HTTP {this.status} for {this.url}");
        }
    }

    func[Str] toStr() {
        return $"<Response [{this.status}]>";
    }
}

// The characters HTTP allows in a method or a header name
func[Bul] _isToken(text[Str]) {
    if (text.isEmpty()) return false;
    const[Str] marks = "!#$%&'*+-.^_`|~";
    for (var[Chr] c in text) {
        var[Int] code = c.code();
        var[Bul] plain = (code >= 48 && code <= 57) || (code >= 65 && code <= 90) || (code >= 97 && code <= 122);
        if (!plain && !marks.includes(Str(c))) return false;
    }
    return true;
}

func[Response] request(method[Str], url[Str], headers[Map[Str, Str]]={}, body[Str]="", timeout[Int]=30) {
    // Checked here, for every target: a line break in a value would otherwise
    // start a header of its own
    if (!_isToken(method)) throw new RequestError($"'{method}' is not an HTTP method");
    var[Str] headerLines = "";
    for (var[Str] name in headers) {
        if (!_isToken(name)) throw new RequestError($"'{name}' is not a header name");
        var[Str] value = headers<name>;
        if (value.includes("\n") || value.includes("\r")) throw new RequestError($"the value of header {name} has a line break");
        headerLines += $"{name}: {value}\n";
    }
    var[Str] address = url.trim().lower();
    if (!address.startsWith("http://") && !address.startsWith("https://")) {
        throw new RequestError($"'{url}' is not an http:// or https:// address");
    }

    var[Str<>] raw = <>;
    try {
        raw = _send(method.upper(), url, headerLines, body, timeout);
    } catch (e[Str]) {
        throw new RequestError(e);
    }

    var[Response] response = new Response();
    response.url = url;
    response.status = Int(raw<0>.trim());
    response.ok = response.status >= 200 && response.status < 300;
    response.text = raw<2>;
    for (var[Str] line in raw<1>.split("\n")) {
        var[Int] colon = line.indexOf(":");
        if (colon <= 0) continue;
        var[Str] name = line.substr(0, colon).trim().lower();
        response.headers<name> = line.substr(colon + 1, line.length() - colon - 1).trim();
    }
    return response;
}

func[Response] get(url[Str], headers[Map[Str, Str]]={}, timeout[Int]=30) {
    return request("GET", url, headers, "", timeout);
}

// To wait for in an async function. It still waits for the network itself: Dessa has
// one thread, so nothing else runs until the answer comes.
async func[Response] getTask(url[Str], headers[Map[Str, Str]]={}, timeout[Int]=30) {
    return get(url, headers, timeout);
}

func[Response] post(url[Str], body[Str]="", headers[Map[Str, Str]]={}, timeout[Int]=30) {
    return request("POST", url, headers, body, timeout);
}

func[Response] put(url[Str], body[Str]="", headers[Map[Str, Str]]={}, timeout[Int]=30) {
    return request("PUT", url, headers, body, timeout);
}

func[Response] patch(url[Str], body[Str]="", headers[Map[Str, Str]]={}, timeout[Int]=30) {
    return request("PATCH", url, headers, body, timeout);
}

func[Response] delete(url[Str], headers[Map[Str, Str]]={}, timeout[Int]=30) {
    return request("DELETE", url, headers, "", timeout);
}

// Sends `data` as a JSON body (Content-Type: application/json).
func[Response] postJson(url[Str], data[Json], headers[Map[Str, Str]]={}, timeout[Int]=30) {
    var[Map[Str, Str]] all = {...headers, "Content-Type": "application/json"};
    return request("POST", url, all, stringify(data), timeout);
}
