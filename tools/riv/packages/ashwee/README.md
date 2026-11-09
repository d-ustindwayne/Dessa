# ashwee

Desktop windows and controls.

**Install:** Install with `riv add ashwee`.

**Runs in:** Native programs only: Windows (Win32), Linux (GTK 3) and macOS (AppKit).

## Example

```dessa
$("ashwee") { Window, Label, TextBox }

var[Window] win = new Window("Greeter", 360, 140);
var[TextBox] name = win.input(20, 20, 200);
var[Label] greeting = win.label("", 20, 64, 320);
win.button("Greet", 236, 18).onClick(() := greeting.setText($"Hello, {name.text()}!"));
win.run();
```

A longer example is in [examples/todo.ds](examples/todo.ds).

## What it has

A `Window` makes its controls: `label`, `button`, `input`, `password`, `textArea`,
`checkBox` and `list`. `message(text)` shows a message box and `run()` opens the window
and waits until it closes.

## Good to know

On Linux, programs need GTK 3 at run time (`libgtk-3-0` on Debian and Ubuntu, `gtk3` on
Fedora and Arch). Nothing extra is needed to build them.
