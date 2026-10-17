/***
 * $(<ashwee>) -- desktop windows and controls. Windows only for now.
 *
 *     { Window, Label, TextBox } from $(<ashwee>);
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
 * The native part (Win32) is ashwee.hpp / ashwee.cpp, described by ashwee.native.
 */

{ _window, _control, _show, _close, _setText, _getText, _setEnabled, _setChecked, _isChecked,
  _listAdd, _listClear, _listSelected, _message, _nextEvent } from $(<ashwee>);

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
        self.id = id;
        self.handlers = handlers;
    }

    func[Str] text() { return _getText(self.id); }
    func[Emp] setText(text[Str]) { _setText(self.id, text); }

    // Greyed out and not clickable when false.
    func[Emp] setEnabled(enabled[Bul]) { _setEnabled(self.id, enabled); }

    func[Emp] _on(action[Func[Emp]]) {
        self.handlers.actions<self.id> = action;
    }
}

// Text the user can't edit.
class Label(Widget) {}

class Button(Widget) {
    // Runs `action` when the button is clicked. Returns the button.
    func[Button] onClick(action[Func[Emp]]) {
        self._on(action);
        return self;
    }
}

// A one-line text field (input, password) or a multi-line one (textArea).
class TextBox(Widget) {
    // Runs `action` every time the text changes.
    func[TextBox] onChange(action[Func[Emp]]) {
        self._on(action);
        return self;
    }
}

class CheckBox(Widget) {
    func[Bul] checked() { return _isChecked(self.id); }
    func[Emp] setChecked(checked[Bul]) { _setChecked(self.id, checked); }

    // Runs `action` when the user ticks or unticks it.
    func[CheckBox] onToggle(action[Func[Emp]]) {
        self._on(action);
        return self;
    }
}

// A list of lines the user can pick from.
class ListBox(Widget) {
    var[Str<>] items = [];

    func[Emp] add(item[Str]) {
        _listAdd(self.id, item);
        self.items.push(item);
    }

    func[Emp] clear() {
        _listClear(self.id);
        self.items.clear();
    }

    // Index of the selected line, -1 if none.
    func[Int] selected() { return _listSelected(self.id); }

    // The selected line, or nil.
    func[Str | Nil] selectedText() {
        var[Int] index = self.selected();
        if (index < 0 || index >= self.items.length()) return nil;
        return self.items<index>;
    }

    // Runs `action` when the user selects a line.
    func[ListBox] onSelect(action[Func[Emp]]) {
        self._on(action);
        return self;
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
        self.id = _window(title, width, height);
        self.handlers = new _Handlers();
    }

    func[Int] _add(kind[Str], text[Str], x[Int], y[Int], width[Int], height[Int]) {
        return _control(self.id, kind, text, x, y, width, height);
    }

    func[Label] label(text[Str], x[Int], y[Int], width[Int]=200, height[Int]=24) {
        return new Label(self._add("label", text, x, y, width, height), self.handlers);
    }

    func[Button] button(text[Str], x[Int], y[Int], width[Int]=100, height[Int]=30) {
        return new Button(self._add("button", text, x, y, width, height), self.handlers);
    }

    func[TextBox] input(x[Int], y[Int], width[Int]=200, text[Str]="") {
        return new TextBox(self._add("input", text, x, y, width, 26), self.handlers);
    }

    // Like input, but shows dots instead of the letters.
    func[TextBox] password(x[Int], y[Int], width[Int]=200) {
        return new TextBox(self._add("password", "", x, y, width, 26), self.handlers);
    }

    func[TextBox] textArea(x[Int], y[Int], width[Int]=300, height[Int]=120, text[Str]="") {
        return new TextBox(self._add("textarea", text, x, y, width, height), self.handlers);
    }

    func[CheckBox] checkBox(text[Str], x[Int], y[Int], width[Int]=200, checked[Bul]=false) {
        var[CheckBox] box = new CheckBox(self._add("checkbox", text, x, y, width, 24), self.handlers);
        if (checked) box.setChecked(true);
        return box;
    }

    func[ListBox] list(x[Int], y[Int], width[Int]=200, height[Int]=120) {
        return new ListBox(self._add("list", "", x, y, width, height), self.handlers);
    }

    func[Emp] setTitle(title[Str]) { _setText(self.id, title); }

    // Runs `action` when the user closes the window (before it closes).
    func[Emp] onClose(action[Func[Emp]]) {
        self.closing = action;
    }

    func[Emp] close() {
        if (self.closed) return;
        self.closed = true;
        _close(self.id);
    }

    // Shows the window and handles clicks, typing, ... until it is closed.
    func[Emp] run() {
        _show(self.id);
        while (!self.closed) {
            var[Str<>] event = _nextEvent();
            var[Str] kind = event<0>;
            if (kind == "idle") break;
            var[Int] source = Int(event<1>);
            if (kind == "close") {
                if (source != self.id) continue;
                var[Func[Emp] | Nil] closing = self.closing;
                if (closing != nil) closing();
                self.close();
            } else if (self.handlers.actions.has(source)) {
                var[Func[Emp]] action = self.handlers.actions<source>;
                action();
            }
        }
    }
}

// A message box with an OK button; waits until it's closed.
func[Emp] message(text[Str], title[Str]="Message") {
    _message(title, text);
}
