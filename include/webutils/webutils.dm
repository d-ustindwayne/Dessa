/***
 * $("webutils") -- the web page, from Dessa. Only for programs that run in a
 * browser: <script type="text/dessa"> with web/dessa.js, or
 * `dessa build app.ds --target web`.
 *
 *     { document, alert, log } from $("webutils");
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
 * The functions marked `{ ... }` are implemented in webutils.js.
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
    var[Element<>] children;
    var[Dbl] offsetWidth;
    var[Dbl] offsetHeight;
    var[Func[Emp, [Event]] | Nil] onclick;
    var[Func[Emp, [Event]] | Nil] oninput;
    var[Func[Emp, [Event]] | Nil] onchange;

    // Puts `child` inside this element, at the end. Returns `child`.
    func[Element] appendChild(child[Element]) { ... }
    func[Element] removeChild(child[Element]) { ... }
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
}

// The page itself.
class Document {
    var[Element] body;
    var[Element] head;
    var[Element] documentElement;
    var[Str] title;
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
    func[Emp] reload() { ... }
}

// The browser window.
class Window {
    var[Dbl] innerWidth;
    var[Dbl] innerHeight;
    var[Location] location;
    var[Storage] localStorage;
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
//     { parseJson } from $("json");
//     var[Json] data = parseJson(fetchText("data.json"));
func[Str] fetchText(url[Str]) { ... }
