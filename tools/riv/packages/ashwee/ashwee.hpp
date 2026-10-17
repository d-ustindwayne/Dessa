// $("ashwee") -- native part: windows and controls (Win32 for now).
// The friendly API (Window, Button, run, ...) is written in Dessa in ashwee.dm.
//
// The implementation lives in ashwee.cpp, compiled once and linked in, so
// <windows.h> never touches your program.
#pragma once
#include "dessa_runtime.hpp"

namespace dessa::ashwee {

// Every window and control is a number (an index into a table).
Int _window(const Str& title, Int width, Int height);
// kind: "label", "button", "input", "password", "textarea", "checkbox", "list"
Int _control(Int window, const Str& kind, const Str& text, Int x, Int y, Int width, Int height);
void _show(Int window);
void _close(Int window);
void _setText(Int id, const Str& text);
Str _getText(Int id);
void _setEnabled(Int id, Bul enabled);
void _setChecked(Int id, Bul checked);
Bul _isChecked(Int id);
void _listAdd(Int id, const Str& item);
void _listClear(Int id);
Int _listSelected(Int id);
void _message(const Str& title, const Str& text);
// Waits for the next event: {"click", id} {"change", id} {"select", id}
// {"close", window}, or {"idle", ""} when no window is open any more.
List<Str> _nextEvent();

} // namespace dessa::ashwee
