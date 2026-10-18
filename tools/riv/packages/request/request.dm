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

class RequestError {
    var[Str] message = "";
    init(message[Str]) {
        this.message = message;
    }
    func[Str] toStr() {
        return $"RequestError: {this.message}";
    }
}

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

func[Response] request(method[Str], url[Str], headers[Map[Str, Str]]={}, body[Str]="", timeout[Int]=30) {
    var[Str] headerLines = "";
    for (var[Str] name in headers) {
        headerLines += $"{name}: {headers<name>}\n";
    }

    var[Str<>] raw = [];
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
