/***
 * $("webutils") -- the web page, from Dessa. Only for the scripts of a page
 * (a .dhtml file), which run as WebAssembly in the browser.
 *
 *     $("webutils") { document, alert, log }
 *
 *     var[Element] button = document.createElement("button");
 *     button.textContent = "Click me";
 *     button.addEventListener("click", (e[Event]) := {
 *         alert("Hello from Dessa!");
 *         log($"clicked at {e.clientX}, {e.clientY}");
 *     });
 *     document.body.appendChild(button);
 *
 * Document, Element, Event, ... are the browser's own objects: reading or
 * setting a field (el.textContent = "Hi") changes the page right away,
 * exactly like in JavaScript. The browser's names are kept.
 *
 * The functions marked `{ ... }` are done by the page bridge
 * (runtime/dessa_web.c and runtime/dessa_wasm.js).
 *
 * The utilities are at the end of this file:
 *     find, findAll, must      elements, with less checking for nil
 *     h                        building elements (safe from script injection)
 *     escapeHtml, safeHtml     text going into HTML
 *     on, once, onWindow       events, also for elements added later
 *     debounce, throttle       calling something less often
 *     fetchAsync, postAsync    the network, without stopping the page
 *     Url, encodeUri, decodeUri
 *     formValues, fillForm, validate
 *     store, sessionStore, cookies
 *     Router, State            small apps: views and values that update the page
 *     toast, copyText, downloadText, readChosenFile, mediaMatches, nextFrame
 */

// ============================================================
// Page objects (the browser's own; Dessa doesn't create them)
// ============================================================

// el.style.color = "red"
class Style {
    var[Str] color;
    var[Str] backgroundColor;
    var[Str] display;
    var[Str] fontSize;
    var[Str] fontWeight;
    var[Str] margin;
    var[Str] padding;
    var[Str] width;
    var[Str] height;
    var[Str] border;
    var[Str] textAlign;
    // Any CSS property: el.style.setProperty("grid-template-columns", "1fr 1fr")
    func[Emp] setProperty(name[Str], value[Str]) { ... }
    func[Str] getPropertyValue(name[Str]) { ... }
    func[Emp] removeProperty(name[Str]) { ... }
}

// el.classList.add("active")
class ClassList {
    func[Emp] add(name[Str]) { ... }
    func[Emp] remove(name[Str]) { ... }
    // Adds it if missing, removes it if there; true if it's there afterwards.
    func[Bul] toggle(name[Str]) { ... }
    func[Bul] contains(name[Str]) { ... }
}

// Drawing on a <canvas>: canvas.getContext("2d"). The browser's own names
// (CanvasRenderingContext2D); the canvaslib package wraps it in a simpler API.
//     var[Element] canvas = document.createElement("canvas");
//     var[Context2D] g = canvas.getContext("2d");
//     g.fillStyle = "tomato";
//     g.fillRect(10, 10, 50, 50);
class Context2D {
    var[Str] fillStyle;            // a CSS colour: "red", "#ff8800", "rgb(0 128 255)"
    var[Str] strokeStyle;
    var[Dbl] lineWidth;
    var[Str] font;                 // "16px sans-serif"
    var[Str] textAlign;            // "left", "center", "right"
    var[Str] textBaseline;         // "top", "middle", "alphabetic", "bottom"
    var[Dbl] globalAlpha;          // 0 (see-through) to 1
    var[Str] lineCap;              // "butt", "round", "square"
    func[Emp] fillRect(x[Dbl], y[Dbl], width[Dbl], height[Dbl]) { ... }
    func[Emp] strokeRect(x[Dbl], y[Dbl], width[Dbl], height[Dbl]) { ... }
    func[Emp] clearRect(x[Dbl], y[Dbl], width[Dbl], height[Dbl]) { ... }
    func[Emp] beginPath() { ... }
    func[Emp] closePath() { ... }
    func[Emp] moveTo(x[Dbl], y[Dbl]) { ... }
    func[Emp] lineTo(x[Dbl], y[Dbl]) { ... }
    // A circle's part: angles in radians (a whole circle is 0 to 2 * PI)
    func[Emp] arc(x[Dbl], y[Dbl], radius[Dbl], startAngle[Dbl], endAngle[Dbl]) { ... }
    func[Emp] rect(x[Dbl], y[Dbl], width[Dbl], height[Dbl]) { ... }
    func[Emp] fill() { ... }
    func[Emp] stroke() { ... }
    func[Emp] fillText(text[Str], x[Dbl], y[Dbl]) { ... }
    func[Emp] strokeText(text[Str], x[Dbl], y[Dbl]) { ... }
    func[TextMetrics] measureText(text[Str]) { ... }
    // An <img> (or another canvas) at x, y
    func[Emp] drawImage(image[Element], x[Dbl], y[Dbl]) { ... }
    func[Emp] save() { ... }
    func[Emp] restore() { ... }
    func[Emp] translate(x[Dbl], y[Dbl]) { ... }
    func[Emp] rotate(angle[Dbl]) { ... }
    func[Emp] scale(x[Dbl], y[Dbl]) { ... }
}

class TextMetrics {
    var[Dbl] width;
}

// What happened: passed to event listeners.
class Event {
    var[Str] type;                 // "click", "input", "keydown", ...
    var[Element | Nil] target;     // the element it happened on
    var[Str] key;                  // keyboard events: "Enter", "a", ...
    var[Dbl] clientX;              // mouse events: position in the window
    var[Dbl] clientY;
    var[Bul] shiftKey;
    var[Bul] ctrlKey;
    var[Bul] altKey;
    var[Bul] metaKey;
    var[Element | Nil] currentTarget;   // the element the listener is on
    var[Str] code;                 // the key's place on the keyboard: "KeyA", "Space", ...
    var[Dbl] button;               // mouse button: 0 left, 1 middle, 2 right
    var[Dbl] deltaX;               // the wheel
    var[Dbl] deltaY;
    // Stops the browser's default action (following a link, submitting a form, ...)
    func[Emp] preventDefault() { ... }
    func[Emp] stopPropagation() { ... }
}

// An element of the page: <div>, <button>, <input>, ...
class Element {
    var[Str] id;
    var[Str] className;
    var[Str] tagName;              // "DIV", "BUTTON", ...
    var[Str] textContent;          // the text inside
    var[Str] innerHTML;            // the HTML inside
    var[Str] value;                // <input>, <textarea>, <select>
    var[Bul] checked;              // checkboxes
    var[Bul] disabled;
    var[Bul] hidden;
    var[Str] title;
    var[Str] href;
    var[Str] src;
    var[Str] placeholder;
    var[Str] type;
    var[Str] name;
    var[Style] style;
    var[ClassList] classList;
    var[Element | Nil] parentElement;
    var[Element | Nil] firstElementChild;
    var[Element | Nil] lastElementChild;
    var[Element | Nil] nextElementSibling;
    var[Element | Nil] previousElementSibling;
    var[Dbl] scrollTop;
    var[Dbl] scrollLeft;
    var[Dbl] clientWidth;
    var[Dbl] clientHeight;
    var[Element<>] children;
    var[Dbl] offsetWidth;
    var[Dbl] offsetHeight;
    var[Dbl] width;                // <canvas>, <img>: the size in pixels
    var[Dbl] height;
    var[Bul] complete;             // <img>: finished loading
    var[Func[Emp, [Event]] | Nil] onclick;
    var[Func[Emp, [Event]] | Nil] oninput;
    var[Func[Emp, [Event]] | Nil] onchange;

    // Puts `child` inside this element, at the end. Returns `child`.
    func[Element] appendChild(child[Element]) { ... }
    func[Element] removeChild(child[Element]) { ... }
    // Puts `child` before `other`, one of this element's children (nil: at the end).
    func[Element] insertBefore(child[Element], other[Element | Nil]) { ... }
    // A copy, not yet in the page. deep: true copies what is inside too.
    func[Element] cloneNode(deep[Bul] = false) { ... }
    // Puts `other` in this element's place.
    func[Emp] replaceWith(other[Element]) { ... }
    func[Emp] scrollIntoView() { ... }
    // Takes this element out of the page.
    func[Emp] remove() { ... }
    func[Emp] setAttribute(name[Str], value[Str]) { ... }
    func[Str | Nil] getAttribute(name[Str]) { ... }
    func[Emp] removeAttribute(name[Str]) { ... }
    func[Bul] hasAttribute(name[Str]) { ... }
    // The first element inside matching a CSS selector ("#id", ".class", "li"), or nil.
    func[Element | Nil] querySelector(selector[Str]) { ... }
    // Every element inside matching a CSS selector.
    func[Element<>] querySelectorAll(selector[Str]) { ... }
    func[Element | Nil] closest(selector[Str]) { ... }
    func[Bul] matches(selector[Str]) { ... }
    // Runs `listener` every time the event happens: "click", "input", "keydown", "mouseover", ...
    func[Emp] addEventListener(type[Str], listener[Func[Emp, [Event]]]) { ... }
    // "beforebegin", "afterbegin", "beforeend" or "afterend"
    func[Emp] insertAdjacentHTML(position[Str], html[Str]) { ... }
    func[Emp] click() { ... }
    func[Emp] focus() { ... }
    func[Emp] blur() { ... }
    // A <canvas>'s drawing surface: getContext("2d")
    func[Context2D] getContext(kind[Str]) { ... }
    // Where it is on the screen (for mouse positions inside it)
    func[Rect] getBoundingClientRect() { ... }
}

class Rect {
    var[Dbl] left;
    var[Dbl] top;
    var[Dbl] width;
    var[Dbl] height;
}

// The page itself.
class Document {
    var[Element] body;
    var[Element] head;
    var[Element] documentElement;
    var[Str] title;
    var[Str] cookie;
    var[Element | Nil] activeElement;   // the element that has the keyboard
    func[Element | Nil] getElementById(id[Str]) { ... }
    func[Element | Nil] querySelector(selector[Str]) { ... }
    func[Element<>] querySelectorAll(selector[Str]) { ... }
    // A new element (not in the page until appendChild): createElement("div")
    func[Element] createElement(tag[Str]) { ... }
    func[Emp] addEventListener(type[Str], listener[Func[Emp, [Event]]]) { ... }
}

// Text the browser keeps for this site, even after the page closes.
class Storage {
    func[Str | Nil] getItem(key[Str]) { ... }
    func[Emp] setItem(key[Str], value[Str]) { ... }
    func[Emp] removeItem(key[Str]) { ... }
    func[Emp] clear() { ... }
}

class Location {
    var[Str] href;
    var[Str] pathname;
    var[Str] search;
    var[Str] hash;
    var[Str] origin;               // "https://example.com"
    var[Str] host;
    var[Str] protocol;             // "https:"
    func[Emp] reload() { ... }
    // Goes to another address (replace: without a step in the browser's history).
    func[Emp] assign(url[Str]) { ... }
    func[Emp] replace(url[Str]) { ... }
}

// The browser's back and forward. To add a step: pushState(url)
class History {
    var[Dbl] length;
    func[Emp] back() { ... }
    func[Emp] forward() { ... }
}

class MediaQuery {
    var[Bul] matches;
}

// What fetchAsync and postAsync hand to their function
class FetchResult {
    var[Bul] ok;                   // an answer of 200 to 299
    var[Dbl] status;               // the answer's code; 0 when nothing answered
    var[Str] text;
    var[Str] error;                // why nothing answered ("" when something did)
}

// What readChosenFile hands to its function
class ChosenFile {
    var[Str] name;
    var[Dbl] size;
    var[Str] text;
    var[Str] error;                // "" when it could be read
}

// An event listener from on(), onWindow(), ...: listener.remove() stops it
class Listener {
    func[Emp] remove() { ... }
}

// The browser window.
class Window {
    var[Dbl] innerWidth;
    var[Dbl] innerHeight;
    var[Location] location;
    var[Storage] localStorage;
    var[Storage] sessionStorage;   // the same, only until the tab closes
    var[History] history;
    var[Dbl] scrollX;
    var[Dbl] scrollY;
    func[MediaQuery] matchMedia(query[Str]) { ... }
    func[Emp] addEventListener(type[Str], listener[Func[Emp, [Event]]]) { ... }
    func[Emp] scrollTo(x[Dbl], y[Dbl]) { ... }
}

func[Document] _document() { ... }
func[Window] _window() { ... }

// The page: document.getElementById("app"), document.body, ...
var[Document] document = _document();
// The window: window.innerWidth, window.localStorage, ...
var[Window] window = _window();

// ============================================================
// Talking to the user
// ============================================================

// Shows a message in a box with an OK button; waits until it's closed.
func[Emp] alert(message[Str]) { ... }

// Asks for some text. Returns nil if the user presses Cancel.
//     var[Str | Nil] name = prompt("Your name?", "Ann");
func[Str | Nil] prompt(question[Str], answer[Str]="") { ... }

// Asks a yes / no question (OK / Cancel).
func[Bul] confirm(question[Str]) { ... }

// Writes to the browser's developer console (F12), separated by spaces.
//     log("total:", Str(total));
func[Emp] log(messages[Str*]) { ... }
func[Emp] warn(messages[Str*]) { ... }
func[Emp] error(messages[Str*]) { ... }

// ============================================================
// Time
// ============================================================

// Runs `action` once, after `ms` milliseconds. Returns an id for clearTimer.
func[Int] setTimeout(action[Func[Emp]], ms[Int]) { ... }
// Runs `action` every `ms` milliseconds. Returns an id for clearTimer.
func[Int] setInterval(action[Func[Emp]], ms[Int]) { ... }
// Stops a setTimeout or setInterval.
func[Emp] clearTimer(id[Int]) { ... }

// ============================================================
// The network
// ============================================================

// Downloads a text file (same site, or one that allows it) and returns its
// text. Error if it fails. Waits until it's done.
//     $("json") { parseJson }
//     var[Json] data = parseJson(fetchText("data.json"));
func[Str] fetchText(url[Str]) { ... }

// ============================================================
// The utilities: elements
// ============================================================

// The first element matching a CSS selector, or nil. Short for document.querySelector.
func[Element | Nil] find(selector[Str]) { return document.querySelector(selector); }

// Every element matching a CSS selector.
func[Element<>] findAll(selector[Str]) { return document.querySelectorAll(selector); }

// The element, when the page can't work without it: no nil to check for, and a
// clear error naming the selector if it's missing.
//     must("#save").textContent = "Save";
func[Element] must(selector[Str]) {
    var[Element | Nil] found = document.querySelector(selector);
    if (found == nil) throw $"The page has no element matching {selector}";
    return found;
}

// ============================================================
// Text in HTML
// ============================================================

// Text made safe to put in HTML: what a user typed can't become a script.
func[Str] escapeHtml(text[Str]) {
    return text.replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;").replaceAll("\"", "&quot;").replaceAll("'", "&#39;");
}

// HTML with each {} filled by a value, escaped. The template itself is trusted;
// the values aren't.
//     el.innerHTML = safeHtml("<b>{}</b> wrote {}", name, comment);
func[Str] safeHtml(template[Str], values[Str*]) {
    var[Str] out = "";
    var[Str] rest = template;
    for (var[Str] value in values) {
        var[Int] at = rest.indexOf("{}");
        if (at < 0) throw "safeHtml: more values than {} in the template";
        out += rest.substr(0, at) + escapeHtml(value);
        rest = rest.substr(at + 2, rest.length() - at - 2);
    }
    return out + rest;
}

// A web address that is safe in a link: http, https, mailto, or a path on this
// site. Anything else ("javascript:...", which would run as a script) gives "#".
func[Str] safeUrl(url[Str]) {
    // Browsers ignore spaces and control characters inside a scheme
    var[Str] plain = "";
    for (var[Chr] c in url) if (c.code() > 32) plain += Str(c);
    var[Str] low = plain.lower();
    var[Int] colon = low.indexOf(":");
    if (colon < 0) return url;
    var[Int] end = low.length();
    for (var[Str] mark in <"/", "?", "#">) {
        var[Int] at = low.indexOf(mark);
        if (at >= 0 && at < end) end = at;
    }
    if (colon > end) return url;
    var[Str] scheme = low.substr(0, colon);
    return scheme == "http" || scheme == "https" || scheme == "mailto" ? url : "#";
}

// A new element, its attributes and what goes inside it. Text stays text, so this
// is the safe way to build a page from what users typed.
//     var[Element] card = h("div", {"class": "card"},
//         h("h2", {:}, title),
//         h("a", {"href": link}, "Read more"));
// Listeners go through on(), not attributes: onclick="..." is refused here, and
// a link's address goes through safeUrl.
func[Element] h(tag[Str], attributes[Map[Str, Str]], children[(Element | Str)*]) {
    var[Element] el = document.createElement(tag);
    for (var[Str] name in attributes) {
        var[Str] low = name.lower();
        if (low.startsWith("on")) throw $"h: use on(...), not the attribute {name}";
        var[Str] value = attributes<name>;
        if (low == "href" || low == "src" || low == "action" || low == "formaction" || low == "xlink:href") value = safeUrl(value);
        el.setAttribute(name, value);
    }
    for (var[Element | Str] child in children) {
        if (child is Element) {
            el.appendChild(child);
        } else if (child is Str) {
            var[Element] holder = document.createElement("span");
            holder.textContent = child;
            el.appendChild(holder);
        }
    }
    return el;
}

// ============================================================
// Events
// ============================================================

func[Listener] _on(target[Element], type[Str], listener[Func[Emp, [Event]]], once[Bul]) { ... }

// Runs `listener` for an event on `parent`, or, with a selector, on anything
// inside it that matches, also what is added later. listener.remove() stops it.
//     on(must("#list"), "click", "li", (e[Event]) := log("clicked a row"));
func[Listener] on(parent[Element], type[Str], selector[Str], listener[Func[Emp, [Event]]]) {
    if (selector == "") return _on(parent, type, listener, false);
    return _on(parent, type, (e[Event]) := {
        var[Element | Nil] target = e.target;
        if (target != nil && target.closest(selector) != nil) listener(e);
    }, false);
}

// Only the first time.
func[Listener] once(parent[Element], type[Str], listener[Func[Emp, [Event]]]) {
    return _on(parent, type, listener, true);
}

// Events of the whole window: "resize", "scroll", "hashchange", "keydown", ...
func[Listener] onWindow(type[Str], listener[Func[Emp, [Event]]]) {
    return _onWindow(type, listener);
}

func[Listener] _onWindow(type[Str], listener[Func[Emp, [Event]]]) { ... }

// ============================================================
// Calling something less often
// ============================================================

class _Debounce {
    var[Func[Emp]] action;
    var[Int] ms = 0;
    var[Int] timer = -1;
    init(action[Func[Emp]], ms[Int]) {
        this.action = action;
        this.ms = ms;
    }
    func[Emp] call() {
        if (this.timer >= 0) clearTimer(this.timer);
        var[_Debounce] me = this;
        this.timer = setTimeout(() := {
            me.timer = -1;
            me.action();
        }, this.ms);
    }
}

class _Throttle {
    var[Func[Emp]] action;
    var[Int] ms = 0;
    var[Int] last = -1;
    init(action[Func[Emp]], ms[Int]) {
        this.action = action;
        this.ms = ms;
    }
    func[Emp] call() {
        var[Int] now = nowMs();
        if (this.last >= 0 && now - this.last < this.ms) return;
        this.last = now;
        this.action();
    }
}

// A function that runs `action` only once calls have stopped for `ms`
// milliseconds: a search box that waits until the typing pauses.
//     var[Func[Emp]] search = debounce(() := runSearch(), 300);
//     on(box, "input", "", (e[Event]) := search());
func[Func[Emp]] debounce(action[Func[Emp]], ms[Int]) {
    var[_Debounce] state = new _Debounce(action, ms);
    return () := state.call();
}

// A function that runs `action` at most once every `ms` milliseconds: for
// events that fire constantly, like scrolling.
func[Func[Emp]] throttle(action[Func[Emp]], ms[Int]) {
    var[_Throttle] state = new _Throttle(action, ms);
    return () := state.call();
}

// Milliseconds since 1970, from the browser's clock.
func[Int] nowMs() { ... }

// Runs `action` just before the browser next draws the page: smooth animation.
func[Int] nextFrame(action[Func[Emp]]) { ... }
func[Emp] clearFrame(id[Int]) { ... }

// ============================================================
// The network, without stopping the page
// ============================================================

func[Emp] _fetch(method[Str], url[Str], body[Str], contentType[Str], done[Func[Emp, [FetchResult]]]) { ... }

// Downloads in the background; the page keeps working, and `done` runs with the
// answer. (fetchText is simpler, but the page waits for it.)
//     fetchAsync("data.json", (r[FetchResult]) := {
//         if (r.ok) show(r.text); else log("failed:", r.error);
//     });
func[Emp] fetchAsync(url[Str], done[Func[Emp, [FetchResult]]]) {
    _fetch("GET", url, "", "", done);
}

// Sends `body` the same way. contentType says what it is.
func[Emp] postAsync(url[Str], body[Str], done[Func[Emp, [FetchResult]]], contentType[Str] = "application/json", method[Str] = "POST") {
    _fetch(method.upper(), url, body, contentType, done);
}

// ============================================================
// Web addresses
// ============================================================

// Text for a part of a web address: spaces and symbols as %20 and so on
func[Str] encodeUri(text[Str]) {
    const[Str] digits = "0123456789ABCDEF";
    var[Str] out = "";
    for (var[Chr] c in text) {
        var[Int] code = c.code();
        if ((code >= 48 && code <= 57) || (code >= 65 && code <= 90) || (code >= 97 && code <= 122) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += Str(c);
            continue;
        }
        var[Int<>] bytes = <>;
        if (code < 128) {
            bytes = <code>;
        } else if (code < 2048) {
            bytes = <192 + Int(code / 64), 128 + code % 64>;
        } else if (code < 65536) {
            bytes = <224 + Int(code / 4096), 128 + Int(code / 64) % 64, 128 + code % 64>;
        } else {
            bytes = <240 + Int(code / 262144), 128 + Int(code / 4096) % 64, 128 + Int(code / 64) % 64, 128 + code % 64>;
        }
        for (var[Int] b in bytes) out += "%" + Str(digits<Int(b / 16)>) + Str(digits<b % 16>);
    }
    return out;
}

func[Int] _hexDigit(c[Chr]) {
    var[Int] code = c.code();
    if (code >= 48 && code <= 57) return code - 48;
    if (code >= 65 && code <= 70) return code - 55;
    if (code >= 97 && code <= 102) return code - 87;
    return -1;
}

// %20 back into a space, and + too, as forms send it. Stray % stay as they are.
func[Str] decodeUri(text[Str]) {
    var[Chr<>] c = text.chars();
    var[Str] out = "";
    var[Int] i = 0;
    while (i < c.length()) {
        if (c<i> == '+') {
            out += " ";
            i++;
        } else if (c<i> == '%' && i + 2 < c.length() && _hexDigit(c<i + 1>) >= 0 && _hexDigit(c<i + 2>) >= 0) {
            var[Int] first = _hexDigit(c<i + 1>) * 16 + _hexDigit(c<i + 2>);
            var[Int] extra = first >= 240 ? 3 : first >= 224 ? 2 : first >= 192 ? 1 : 0;
            var[Int] code = extra == 3 ? first - 240 : extra == 2 ? first - 224 : extra == 1 ? first - 192 : first;
            i += 3;
            for (var[Int] k = 0; k < extra && i + 2 < c.length() && c<i> == '%'; k++) {
                code = code * 64 + (_hexDigit(c<i + 1>) * 16 + _hexDigit(c<i + 2>) - 128);
                i += 3;
            }
            out += Str(Chr(code));
        } else {
            out += Str(c<i>);
            i++;
        }
    }
    return out;
}

// A web address taken apart. Parts can be changed; toStr puts it together.
//     var[Url] u = new Url("https://example.com/search?q=cats#top");
//     u.host, u.path, u.query<"q">, u.hash
//     u.query<"page"> = "2";
//     print(u);       // https://example.com/search?q=cats&page=2#top
class Url {
    var[Str] protocol = "";            // "https"
    var[Str] host = "";                // "example.com:8080"
    var[Str] path = "";                // "/search"
    var[Map[Str, Str]] query = {:};    // decoded
    var[Str] hash = "";                // "top", without the #

    init(address[Str] = "") {
        var[Str] rest = address;
        var[Int] hashAt = rest.indexOf("#");
        if (hashAt >= 0) {
            this.hash = decodeUri(rest.substr(hashAt + 1, rest.length() - hashAt - 1));
            rest = rest.substr(0, hashAt);
        }
        var[Int] queryAt = rest.indexOf("?");
        if (queryAt >= 0) {
            for (var[Str] pair in rest.substr(queryAt + 1, rest.length() - queryAt - 1).split("&")) {
                if (pair == "") continue;
                var[Int] eq = pair.indexOf("=");
                if (eq < 0) this.query<decodeUri(pair)> = "";
                else this.query<decodeUri(pair.substr(0, eq))> = decodeUri(pair.substr(eq + 1, pair.length() - eq - 1));
            }
            rest = rest.substr(0, queryAt);
        }
        var[Int] scheme = rest.indexOf("://");
        if (scheme >= 0) {
            this.protocol = rest.substr(0, scheme);
            rest = rest.substr(scheme + 3, rest.length() - scheme - 3);
            var[Int] slash = rest.indexOf("/");
            this.host = slash >= 0 ? rest.substr(0, slash) : rest;
            rest = slash >= 0 ? rest.substr(slash, rest.length() - slash) : "";
        }
        this.path = rest;
    }

    func[Str] toStr() {
        var[Str] out = this.protocol == "" ? "" : $"{this.protocol}://{this.host}";
        out += this.path;
        var[Str<>] pairs = <>;
        for (var[Str] key in this.query) pairs.push(encodeUri(key) + "=" + encodeUri(this.query<key>));
        if (!pairs.isEmpty()) out += "?" + pairs.join("&");
        if (this.hash != "") out += "#" + encodeUri(this.hash);
        return out;
    }
}

func[Emp] _pushState(url[Str]) { ... }

// Changes the address in the browser's bar, with a step in its history, without
// loading a page.
func[Emp] pushState(url[Str]) { _pushState(url); }

// ============================================================
// Forms
// ============================================================

// A form's values by name. A checkbox gives "true" or "false"; of radio
// buttons, the chosen one's value.
func[Map[Str, Str]] formValues(form[Element]) {
    var[Map[Str, Str]] out = {:};
    for (var[Element] field in form.querySelectorAll("input, select, textarea")) {
        if (field.name == "") continue;
        if (field.type == "checkbox") out<field.name> = field.checked ? "true" : "false";
        else if (field.type == "radio") { if (field.checked) out<field.name> = field.value; }
        else out<field.name> = field.value;
    }
    return out;
}

// Puts values into a form's fields, by name. Names without a field are skipped.
func[Emp] fillForm(form[Element], values[Map[Str, Str]]) {
    for (var[Element] field in form.querySelectorAll("input, select, textarea")) {
        if (!values.has(field.name)) continue;
        var[Str] value = values<field.name>;
        if (field.type == "checkbox") field.checked = value == "true";
        else if (field.type == "radio") field.checked = field.value == value;
        else field.value = value;
    }
}

// Checks for what users type. On the page only, so it's for helping them: a
// server must check again, as anyone can send it anything.
class Validator {
    func[Bul] isEmail(text[Str]) {
        var[Int] at = text.indexOf("@");
        if (at < 1 || at != text.length() - text.split("@")<text.split("@").length() - 1>.length() - 1) return false;
        var[Str] domain = text.substr(at + 1, text.length() - at - 1);
        if (domain.startsWith(".") || domain.endsWith(".") || !domain.includes(".") || domain.includes("..")) return false;
        for (var[Chr] c in text) if (c.isSpace()) return false;
        return true;
    }
    func[Bul] isUrl(text[Str]) {
        var[Url] u = new Url(text);
        return (u.protocol == "http" || u.protocol == "https") && u.host != "";
    }
    func[Bul] isNumber(text[Str]) {
        var[Str] t = text.trim();
        if (t == "") return false;
        try {
            Dbl(t);
            return true;
        } catch {
            return false;
        }
    }
    func[Bul] isWhole(text[Str]) {
        var[Str] t = text.trim();
        var[Str] plain = t.startsWith("-") ? t.substr(1, t.length() - 1) : t;
        if (plain == "") return false;
        for (var[Chr] c in plain) if (!c.isDigit()) return false;
        return true;
    }
    func[Bul] isFilled(text[Str]) { return text.trim() != ""; }
    func[Bul] lengthBetween(text[Str], low[Int], high[Int]) { return text.length() >= low && text.length() <= high; }
}

const[Validator] validate = new Validator();

// ============================================================
// Keeping things in the browser
// ============================================================

func[Storage] _storage(session[Bul]) { return session ? window.sessionStorage : window.localStorage; }

// Text kept for this site, with what plain localStorage lacks: a default, and an
// expiry. Keep structured values as JSON text ($("json")).
//     store.set("theme", "dark");
//     store.setFor("token", value, 3600);      // gone after an hour
//     store.get("theme", "light");
// Any script on the page can read this: not the place for passwords.
class Store {
    var[Bul] _session = false;
    init(session[Bul] = false) { this._session = session; }

    func[Str] get(key[Str], fallback[Str] = "") {
        var[Str | Nil] raw = _storage(this._session).getItem(key);
        if (raw == nil) return fallback;
        // "expires|<ms>|<value>", from setFor
        if (raw.startsWith("expires|")) {
            var[Str] rest = raw.substr(8, raw.length() - 8);
            var[Int] bar = rest.indexOf("|");
            if (bar > 0 && validate.isWhole(rest.substr(0, bar))) {
                if (Int(rest.substr(0, bar)) <= nowMs()) {
                    this.remove(key);
                    return fallback;
                }
                return rest.substr(bar + 1, rest.length() - bar - 1);
            }
        }
        return raw;
    }
    func[Bul] has(key[Str]) {
        const[Str] missing = "\x01missing";
        return this.get(key, missing) != missing;
    }
    func[Emp] set(key[Str], value[Str]) { _storage(this._session).setItem(key, value); }
    // Gone after `seconds`
    func[Emp] setFor(key[Str], value[Str], seconds[Int]) {
        _storage(this._session).setItem(key, $"expires|{nowMs() + seconds * 1000}|{value}");
    }
    func[Emp] remove(key[Str]) { _storage(this._session).removeItem(key); }
    func[Emp] clear() { _storage(this._session).clear(); }
}

// Kept after the page closes
const[Store] store = new Store();
// Kept until the tab closes
const[Store] sessionStore = new Store(true);

// Cookies, which travel to the server with each request. For a login, the server
// should set them itself, with HttpOnly: scripts can't read those at all.
class CookieJar {
    func[Str | Nil] get(name[Str]) {
        for (var[Str] pair in document.cookie.split(";")) {
            var[Int] eq = pair.indexOf("=");
            if (eq > 0 && decodeUri(pair.substr(0, eq).trim()) == name) return decodeUri(pair.substr(eq + 1, pair.length() - eq - 1).trim());
        }
        return nil;
    }
    // By default for this site's whole path, until the browser closes, and not
    // sent along with requests started from other sites.
    func[Emp] set(name[Str], value[Str], maxAgeSeconds[Int] = -1, path[Str] = "/", secure[Bul] = false, sameSite[Str] = "Lax") {
        var[Str] text = $"{encodeUri(name)}={encodeUri(value)}; Path={encodeUri(path).replaceAll("%2F", "/")}; SameSite={encodeUri(sameSite)}";
        if (maxAgeSeconds >= 0) text += $"; Max-Age={maxAgeSeconds}";
        if (secure) text += "; Secure";
        document.cookie = text;
    }
    func[Emp] remove(name[Str], path[Str] = "/") { this.set(name, "", 0, path); }
}

const[CookieJar] cookies = new CookieJar();

// ============================================================
// Small apps
// ============================================================

class _RouterEntry {
    var[Str<>] parts = <>;
    var[Func[Emp, [Map[Str, Str]]]] view;
    init(parts[Str<>], view[Func[Emp, [Map[Str, Str]]]]) {
        this.parts = parts;
        this.view = view;
    }
    func[Map[Str, Str] | Nil] fits(path[Str<>]) {
        if (path.length() != this.parts.length()) return nil;
        var[Map[Str, Str]] params = {:};
        for (var[Int] i = 0; i < path.length(); i++) {
            if (this.parts<i>.startsWith(":")) params<this.parts<i>.substr(1, this.parts<i>.length() - 1)> = decodeUri(path<i>);
            else if (this.parts<i> != path<i>) return nil;
        }
        return params;
    }
}

func[Str<>] _routeParts(path[Str]) {
    var[Str<>] out = <>;
    for (var[Str] part in path.split("/")) if (part != "") out.push(part);
    return out;
}

// Views of a one-page app, by the part of the address after the #: example.com/#/users/7
//     var[Router] router = new Router();
//     router.add("/", (p[Map[Str, Str]]) := showHome());
//     router.add("/users/:id", (p[Map[Str, Str]]) := showUser(p<"id">));
//     router.otherwise((p[Map[Str, Str]]) := showMissing());
//     router.start();
class Router {
    var[_RouterEntry<>] _routes = <>;
    var[Func[Emp, [Map[Str, Str]]] | Nil] _missing = nil;

    func[Emp] add(path[Str], view[Func[Emp, [Map[Str, Str]]]]) {
        this._routes.push(new _RouterEntry(_routeParts(path), view));
    }
    func[Emp] otherwise(view[Func[Emp, [Map[Str, Str]]]]) {
        this._missing = view;
    }
    // Shows the view for a path, as if the address held it
    func[Emp] show(path[Str]) {
        var[Str<>] parts = _routeParts(path);
        for (var[_RouterEntry] route in this._routes) {
            var[Map[Str, Str] | Nil] params = route.fits(parts);
            if (params != nil) {
                route.view(params);
                return;
            }
        }
        var[Func[Emp, [Map[Str, Str]]] | Nil] missing = this._missing;
        if (missing != nil) missing({:});
    }
    func[Str] current() {
        var[Str] hash = window.location.hash;
        return hash.startsWith("#") ? hash.substr(1, hash.length() - 1) : hash;
    }
    // Goes to a path: the address changes, and the view with it
    func[Emp] go(path[Str]) { window.location.hash = path; }
    // Shows the current address's view, and follows it from now on
    func[Emp] start() {
        var[Router] me = this;
        onWindow("hashchange", (e[Event]) := me.show(me.current()));
        this.show(this.current());
    }
}

// A value the page follows: everything subscribed runs when it changes.
//     var[State:Int] count = new State(0);
//     count.bindText(must("#count"));
//     on(must("#add"), "click", "", (e[Event]) := count.update((n[Int]) := n + 1));
class State:T {
    var[T] _value;
    var[Func[Emp, [T]]<>] _subscribers = <>;

    init(value[T]) { this._value = value; }

    func[T] get() { return this._value; }
    func[Emp] set(value[T]) {
        this._value = value;
        for (var[Func[Emp, [T]]] f in this._subscribers) f(value);
    }
    func[Emp] update(change[Func[T, [T]]]) { this.set(change(this._value)); }
    // Runs now, and each time it changes
    func[Emp] subscribe(f[Func[Emp, [T]]]) {
        this._subscribers.push(f);
        f(this._value);
    }
    // Keeps an element's text the same as this value
    func[Emp] bindText(el[Element]) {
        this.subscribe((value[T]) := { el.textContent = Str(value); });
    }
}

// ============================================================
// Small things
// ============================================================

// A short message in the corner of the page, gone after `ms` milliseconds.
func[Element] toast(message[Str], ms[Int] = 3000) {
    var[Element] box = document.createElement("div");
    box.textContent = message;
    box.setAttribute("role", "status");
    box.setAttribute("style", "position:fixed;right:16px;bottom:16px;padding:10px 14px;border-radius:6px;background:#222;color:#fff;font:14px sans-serif;z-index:2147483647;box-shadow:0 2px 8px rgba(0,0,0,.3)");
    document.body.appendChild(box);
    setTimeout(() := box.remove(), ms);
    return box;
}

// Puts text on the clipboard. Browsers allow this only on https (or localhost),
// during something the user did, like a click.
func[Emp] copyText(text[Str]) { ... }

// Makes the browser save `content` as a file.
func[Emp] downloadText(name[Str], content[Str], type[Str] = "text/plain") { ... }

func[Emp] _readFile(input[Element], done[Func[Emp, [ChosenFile]]]) { ... }

// The file chosen in an <input type="file">, as text. `done` runs when it's read.
func[Emp] readChosenFile(input[Element], done[Func[Emp, [ChosenFile]]]) {
    _readFile(input, done);
}

// Does a CSS media query hold? mediaMatches("(max-width: 600px)")
func[Bul] mediaMatches(query[Str]) { return window.matchMedia(query).matches; }
