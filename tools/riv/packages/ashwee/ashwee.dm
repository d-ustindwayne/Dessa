/***
 * $("ashwee") -- desktop windows and controls on Windows, Linux and macOS.
 *
 *     $("ashwee") { Window, Label, TextBox }
 *
 *     var[Window] win = new Window("Greeter", 360, 140);
 *     var[TextBox] name = win.input(20, 20, 200);
 *     var[Label] greeting = win.label("", 20, 64, 320);
 *     win.button("Greet", 236, 18).onClick(() := greeting.setText($"Hello, {name.text()}!"));
 *     win.run();      // shows the window and handles events until it's closed
 *
 * Positions and sizes are in pixels at 100% screen scaling (x, y from the
 * top-left corner of the window); ashwee scales them for the screen.
 *
 * The native part is ashwee.hpp plus ashwee.cpp (Win32, and GTK 3 through
 * ashwee_gtk.inc) and ashwee_mac.mm (AppKit), described by ashwee.native.
 */

$("ashwee") { _window, _control, _show, _close, _setText, _getText, _setEnabled, _setChecked, _isChecked,
  _listAdd, _listClear, _listSelected, _message, _nextEvent }

// What to do when something happens, by control number. Shared by a window
// and its controls (a class, so it isn't copied).
class _Handlers {
    var[Map[Int, Func[Emp]]] actions = {:};
}

// ============================================================
// Controls
// ============================================================

class Widget {
    var[Int] id = -1;
    var[_Handlers] handlers;

    init(id[Int], handlers[_Handlers]) {
        this.id = id;
        this.handlers = handlers;
    }

    func[Str] text() { return _getText(this.id); }
    func[Emp] setText(text[Str]) { _setText(this.id, text); }

    // Greyed out and not clickable when false.
    func[Emp] setEnabled(enabled[Bul]) { _setEnabled(this.id, enabled); }

    func[Emp] _on(action[Func[Emp]]) {
        this.handlers.actions<this.id> = action;
    }
}

// Text the user can't edit.
class Label(Widget) {}

class Button(Widget) {
    // Runs `action` when the button is clicked. Returns the button.
    func[Button] onClick(action[Func[Emp]]) {
        this._on(action);
        return this;
    }
}

// A one-line text field (input, password) or a multi-line one (textArea).
class TextBox(Widget) {
    // Runs `action` every time the text changes.
    func[TextBox] onChange(action[Func[Emp]]) {
        this._on(action);
        return this;
    }
}

class CheckBox(Widget) {
    func[Bul] checked() { return _isChecked(this.id); }
    func[Emp] setChecked(checked[Bul]) { _setChecked(this.id, checked); }

    // Runs `action` when the user ticks or unticks it.
    func[CheckBox] onToggle(action[Func[Emp]]) {
        this._on(action);
        return this;
    }
}

// A list of lines the user can pick from.
class ListBox(Widget) {
    var[Str<>] items = <>;

    func[Emp] add(item[Str]) {
        _listAdd(this.id, item);
        this.items.push(item);
    }

    func[Emp] clear() {
        _listClear(this.id);
        this.items.clear();
    }

    // Index of the selected line, -1 if none.
    func[Int] selected() { return _listSelected(this.id); }

    // The selected line, or nil.
    func[Str | Nil] selectedText() {
        var[Int] index = this.selected();
        if (index < 0 || index >= this.items.length()) return nil;
        return this.items<index>;
    }

    // Runs `action` when the user selects a line.
    func[ListBox] onSelect(action[Func[Emp]]) {
        this._on(action);
        return this;
    }
}

// ============================================================
// Window
// ============================================================

class Window {
    var[Int] id = -1;
    var[_Handlers] handlers;
    var[Func[Emp] | Nil] closing = nil;
    var[Bul] closed = false;

    // A window whose inside is width x height pixels.
    init(title[Str]="Dessa", width[Int]=480, height[Int]=320) {
        this.id = _window(title, width, height);
        this.handlers = new _Handlers();
    }

    func[Int] _add(kind[Str], text[Str], x[Int], y[Int], width[Int], height[Int]) {
        return _control(this.id, kind, text, x, y, width, height);
    }

    func[Label] label(text[Str], x[Int], y[Int], width[Int]=200, height[Int]=24) {
        return new Label(this._add("label", text, x, y, width, height), this.handlers);
    }

    func[Button] button(text[Str], x[Int], y[Int], width[Int]=100, height[Int]=30) {
        return new Button(this._add("button", text, x, y, width, height), this.handlers);
    }

    func[TextBox] input(x[Int], y[Int], width[Int]=200, text[Str]="") {
        return new TextBox(this._add("input", text, x, y, width, 26), this.handlers);
    }

    // Like input, but shows dots instead of the letters.
    func[TextBox] password(x[Int], y[Int], width[Int]=200) {
        return new TextBox(this._add("password", "", x, y, width, 26), this.handlers);
    }

    func[TextBox] textArea(x[Int], y[Int], width[Int]=300, height[Int]=120, text[Str]="") {
        return new TextBox(this._add("textarea", text, x, y, width, height), this.handlers);
    }

    func[CheckBox] checkBox(text[Str], x[Int], y[Int], width[Int]=200, checked[Bul]=false) {
        var[CheckBox] box = new CheckBox(this._add("checkbox", text, x, y, width, 24), this.handlers);
        if (checked) box.setChecked(true);
        return box;
    }

    func[ListBox] list(x[Int], y[Int], width[Int]=200, height[Int]=120) {
        return new ListBox(this._add("list", "", x, y, width, height), this.handlers);
    }

    func[Emp] setTitle(title[Str]) { _setText(this.id, title); }

    // Runs `action` when the user closes the window (before it closes).
    func[Emp] onClose(action[Func[Emp]]) {
        this.closing = action;
    }

    func[Emp] close() {
        if (this.closed) return;
        this.closed = true;
        _close(this.id);
    }

    // Shows the window and handles clicks, typing, ... until it is closed.
    func[Emp] run() {
        _show(this.id);
        while (!this.closed) {
            var[Str<>] event = _nextEvent();
            var[Str] kind = event<0>;
            if (kind == "idle") break;
            var[Int] source = Int(event<1>);
            if (kind == "close") {
                if (source != this.id) continue;
                var[Func[Emp] | Nil] closing = this.closing;
                if (closing != nil) closing();
                this.close();
            } else if (this.handlers.actions.has(source)) {
                var[Func[Emp]] action = this.handlers.actions<source>;
                action();
            }
        }
    }
}

// A message box with an OK button; waits until it's closed.
func[Emp] message(text[Str], title[Str]="Message") {
    _message(title, text);
}
