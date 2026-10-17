// Linux implementation of $(<ashwee>), with GTK 3. Included by ashwee.cpp.
//
// GTK is loaded when the first window is made (dlopen), so building a
// program needs no GTK development files: running it needs GTK 3, which
// every desktop Linux has (or: sudo apt install libgtk-3-0).
#include <dlfcn.h>

#include <cstdint>
#include <deque>
#include <vector>

namespace dessa::ashwee {

namespace {

// ---- The GTK functions we use, looked up in libgtk-3 ----
using P = void*;
struct Gtk {
    int (*init_check)(int*, char***);
    P (*window_new)(int);
    void (*window_set_title)(P, const char*);
    const char* (*window_get_title)(P);
    void (*window_set_default_size)(P, int, int);
    void (*window_set_resizable)(P, int);
    void (*window_present)(P);
    P (*fixed_new)();
    void (*fixed_put)(P, P, int, int);
    void (*container_add)(P, P);
    P (*container_get_children)(P);
    void (*widget_set_size_request)(P, int, int);
    void (*widget_show_all)(P);
    void (*widget_destroy)(P);
    void (*widget_set_sensitive)(P, int);
    P (*label_new)(const char*);
    void (*label_set_text)(P, const char*);
    const char* (*label_get_text)(P);
    void (*label_set_xalign)(P, float);
    P (*button_new_with_label)(const char*);
    void (*button_set_label)(P, const char*);
    const char* (*button_get_label)(P);
    P (*check_button_new_with_label)(const char*);
    int (*toggle_button_get_active)(P);
    void (*toggle_button_set_active)(P, int);
    P (*entry_new)();
    void (*entry_set_text)(P, const char*);
    const char* (*entry_get_text)(P);
    void (*entry_set_visibility)(P, int);
    P (*text_view_new)();
    P (*text_view_get_buffer)(P);
    void (*text_buffer_set_text)(P, const char*, int);
    void (*text_buffer_get_start_iter)(P, P);
    void (*text_buffer_get_end_iter)(P, P);
    char* (*text_buffer_get_text)(P, P, P, int);
    P (*scrolled_window_new)(P, P);
    P (*list_box_new)();
    void (*list_box_insert)(P, P, int);
    P (*list_box_get_selected_row)(P);
    int (*list_box_row_get_index)(P);
    P (*message_dialog_new)(P, int, int, int, const char*, ...);
    int (*dialog_run)(P);
    int (*main_iteration_do)(int);
    int (*events_pending)();
    unsigned long (*signal_connect_data)(P, const char*, P, P, P, int);
    void (*g_free)(P);
    void (*g_list_free)(P);
};

Gtk gtk;
bool started = false;

template <class F>
void bind(void* lib, F& slot, const char* name) {
    void* f = dlsym(lib, name);
    if (!f) fail(std::string("ashwee: this GTK has no ") + name + " (GTK 3 is needed)");
    slot = reinterpret_cast<F>(f);
}

void start() {
    if (started) return;
    void* lib = nullptr;
    for (const char* name : {"libgtk-3.so.0", "libgtk-3.so"}) {
        if ((lib = dlopen(name, RTLD_NOW | RTLD_GLOBAL))) break;
    }
    if (!lib) fail("ashwee: GTK 3 isn't installed (e.g. sudo apt install libgtk-3-0)");
#define DESSA_BIND(field, name) bind(lib, gtk.field, name)
    DESSA_BIND(init_check, "gtk_init_check");
    DESSA_BIND(window_new, "gtk_window_new");
    DESSA_BIND(window_set_title, "gtk_window_set_title");
    DESSA_BIND(window_get_title, "gtk_window_get_title");
    DESSA_BIND(window_set_default_size, "gtk_window_set_default_size");
    DESSA_BIND(window_set_resizable, "gtk_window_set_resizable");
    DESSA_BIND(window_present, "gtk_window_present");
    DESSA_BIND(fixed_new, "gtk_fixed_new");
    DESSA_BIND(fixed_put, "gtk_fixed_put");
    DESSA_BIND(container_add, "gtk_container_add");
    DESSA_BIND(container_get_children, "gtk_container_get_children");
    DESSA_BIND(widget_set_size_request, "gtk_widget_set_size_request");
    DESSA_BIND(widget_show_all, "gtk_widget_show_all");
    DESSA_BIND(widget_destroy, "gtk_widget_destroy");
    DESSA_BIND(widget_set_sensitive, "gtk_widget_set_sensitive");
    DESSA_BIND(label_new, "gtk_label_new");
    DESSA_BIND(label_set_text, "gtk_label_set_text");
    DESSA_BIND(label_get_text, "gtk_label_get_text");
    DESSA_BIND(label_set_xalign, "gtk_label_set_xalign");
    DESSA_BIND(button_new_with_label, "gtk_button_new_with_label");
    DESSA_BIND(button_set_label, "gtk_button_set_label");
    DESSA_BIND(button_get_label, "gtk_button_get_label");
    DESSA_BIND(check_button_new_with_label, "gtk_check_button_new_with_label");
    DESSA_BIND(toggle_button_get_active, "gtk_toggle_button_get_active");
    DESSA_BIND(toggle_button_set_active, "gtk_toggle_button_set_active");
    DESSA_BIND(entry_new, "gtk_entry_new");
    DESSA_BIND(entry_set_text, "gtk_entry_set_text");
    DESSA_BIND(entry_get_text, "gtk_entry_get_text");
    DESSA_BIND(entry_set_visibility, "gtk_entry_set_visibility");
    DESSA_BIND(text_view_new, "gtk_text_view_new");
    DESSA_BIND(text_view_get_buffer, "gtk_text_view_get_buffer");
    DESSA_BIND(text_buffer_set_text, "gtk_text_buffer_set_text");
    DESSA_BIND(text_buffer_get_start_iter, "gtk_text_buffer_get_start_iter");
    DESSA_BIND(text_buffer_get_end_iter, "gtk_text_buffer_get_end_iter");
    DESSA_BIND(text_buffer_get_text, "gtk_text_buffer_get_text");
    DESSA_BIND(scrolled_window_new, "gtk_scrolled_window_new");
    DESSA_BIND(list_box_new, "gtk_list_box_new");
    DESSA_BIND(list_box_insert, "gtk_list_box_insert");
    DESSA_BIND(list_box_get_selected_row, "gtk_list_box_get_selected_row");
    DESSA_BIND(list_box_row_get_index, "gtk_list_box_row_get_index");
    DESSA_BIND(message_dialog_new, "gtk_message_dialog_new");
    DESSA_BIND(dialog_run, "gtk_dialog_run");
    DESSA_BIND(main_iteration_do, "gtk_main_iteration_do");
    DESSA_BIND(events_pending, "gtk_events_pending");
    DESSA_BIND(signal_connect_data, "g_signal_connect_data");
    DESSA_BIND(g_free, "g_free");
    DESSA_BIND(g_list_free, "g_list_free");
#undef DESSA_BIND
    if (!gtk.init_check(nullptr, nullptr)) fail("ashwee: can't open a window (no display; is a desktop running?)");
    started = true;
}

struct Item {
    P widget = nullptr;    // the control (for textarea: the text view; list: the list box)
    P outer = nullptr;     // what was put in the window (a scrolled window for textarea / list)
    Int window = -1;
    Str kind;
    bool open = true;
};

std::vector<Item> items;
std::deque<List<Str>> events;
bool quiet = false;   // changes made by the program itself aren't events (like on Windows)

struct GList { P data; GList* next; GList* prev; };

Item& item(Int id, const char* what) {
    if (id < 0 || id >= static_cast<Int>(items.size()) || !items[static_cast<size_t>(id)].open) {
        fail(std::string("ashwee: ") + what + ": no such window or control (" + std::to_string(id) + ")");
    }
    return items[static_cast<size_t>(id)];
}

P idData(Int id) { return reinterpret_cast<P>(static_cast<intptr_t>(id)); }
Int dataId(P data) { return static_cast<Int>(reinterpret_cast<intptr_t>(data)); }

void push(const char* kind, P data) {
    if (quiet) return;
    Int id = dataId(data);
    if (id >= 0 && id < static_cast<Int>(items.size()) && items[static_cast<size_t>(id)].open) {
        events.push_back({kind, std::to_string(id)});
    }
}

// Signal handlers (C calling convention, GTK passes our id as user data)
extern "C" void dessa_ashwee_clicked(P, P data) { push("click", data); }
extern "C" void dessa_ashwee_changed(P, P data) { push("change", data); }
extern "C" void dessa_ashwee_selected(P, P, P data) { push("select", data); }
extern "C" int dessa_ashwee_delete(P, P, P data) {
    push("close", data);
    return 1;   // ashwee.dm decides (it calls _close)
}

void connect(P instance, const char* signal, P handler, Int id) {
    gtk.signal_connect_data(instance, signal, handler, idData(id), nullptr, 0);
}

Str textOf(P buffer) {
    alignas(16) unsigned char start[256], end[256];   // GtkTextIter is about 80 bytes
    gtk.text_buffer_get_start_iter(buffer, start);
    gtk.text_buffer_get_end_iter(buffer, end);
    char* text = gtk.text_buffer_get_text(buffer, start, end, 1);
    Str out = text ? text : "";
    if (text) gtk.g_free(text);
    return out;
}

} // namespace

Int _window(const Str& title, Int width, Int height) {
    start();
    P win = gtk.window_new(0);
    gtk.window_set_title(win, title.c_str());
    gtk.window_set_default_size(win, static_cast<int>(width), static_cast<int>(height));
    gtk.window_set_resizable(win, 0);
    P fixed = gtk.fixed_new();
    gtk.container_add(win, fixed);
    Int id = static_cast<Int>(items.size());
    items.push_back({win, fixed, id, "window", true});
    connect(win, "delete-event", reinterpret_cast<P>(&dessa_ashwee_delete), id);
    return id;
}

Int _control(Int window, const Str& kind, const Str& text, Int x, Int y, Int width, Int height) {
    P fixed = item(window, "add a control").outer;
    Int id = static_cast<Int>(items.size());
    P widget = nullptr, outer = nullptr;
    if (kind == "label") {
        widget = gtk.label_new(text.c_str());
        gtk.label_set_xalign(widget, 0.0f);
    } else if (kind == "button") {
        widget = gtk.button_new_with_label(text.c_str());
        connect(widget, "clicked", reinterpret_cast<P>(&dessa_ashwee_clicked), id);
    } else if (kind == "checkbox") {
        widget = gtk.check_button_new_with_label(text.c_str());
        connect(widget, "toggled", reinterpret_cast<P>(&dessa_ashwee_clicked), id);
    } else if (kind == "input" || kind == "password") {
        widget = gtk.entry_new();
        gtk.entry_set_text(widget, text.c_str());
        if (kind == "password") gtk.entry_set_visibility(widget, 0);
        connect(widget, "changed", reinterpret_cast<P>(&dessa_ashwee_changed), id);
    } else if (kind == "textarea") {
        widget = gtk.text_view_new();
        P buffer = gtk.text_view_get_buffer(widget);
        gtk.text_buffer_set_text(buffer, text.c_str(), -1);
        connect(buffer, "changed", reinterpret_cast<P>(&dessa_ashwee_changed), id);
        outer = gtk.scrolled_window_new(nullptr, nullptr);
        gtk.container_add(outer, widget);
    } else if (kind == "list") {
        widget = gtk.list_box_new();
        connect(widget, "row-selected", reinterpret_cast<P>(&dessa_ashwee_selected), id);
        outer = gtk.scrolled_window_new(nullptr, nullptr);
        gtk.container_add(outer, widget);
    } else {
        fail("ashwee: unknown control kind '" + kind + "'");
    }
    if (!outer) outer = widget;
    gtk.widget_set_size_request(outer, static_cast<int>(width), static_cast<int>(height));
    gtk.fixed_put(fixed, outer, static_cast<int>(x), static_cast<int>(y));
    items.push_back({widget, outer, window, kind, true});
    return id;
}

void _show(Int window) {
    P win = item(window, "show").widget;
    gtk.widget_show_all(win);
    gtk.window_present(win);
}

void _close(Int window) {
    Item& w = item(window, "close");
    gtk.widget_destroy(w.widget);
    for (auto& i : items) {
        if (i.window == window) i.open = false;
    }
    while (gtk.events_pending()) gtk.main_iteration_do(0);
}

void _setText(Int id, const Str& text) {
    Item& i = item(id, "setText");
    if (i.kind == "window") gtk.window_set_title(i.widget, text.c_str());
    else if (i.kind == "label") gtk.label_set_text(i.widget, text.c_str());
    else if (i.kind == "button" || i.kind == "checkbox") gtk.button_set_label(i.widget, text.c_str());
    else if (i.kind == "input" || i.kind == "password") gtk.entry_set_text(i.widget, text.c_str());
    else if (i.kind == "textarea") gtk.text_buffer_set_text(gtk.text_view_get_buffer(i.widget), text.c_str(), -1);
}

Str _getText(Int id) {
    Item& i = item(id, "text");
    const char* text = nullptr;
    if (i.kind == "window") text = gtk.window_get_title(i.widget);
    else if (i.kind == "label") text = gtk.label_get_text(i.widget);
    else if (i.kind == "button" || i.kind == "checkbox") text = gtk.button_get_label(i.widget);
    else if (i.kind == "input" || i.kind == "password") text = gtk.entry_get_text(i.widget);
    else if (i.kind == "textarea") return textOf(gtk.text_view_get_buffer(i.widget));
    return text ? text : "";
}

void _setEnabled(Int id, Bul enabled) {
    gtk.widget_set_sensitive(item(id, "setEnabled").outer, enabled ? 1 : 0);
}

void _setChecked(Int id, Bul checked) {
    quiet = true;
    gtk.toggle_button_set_active(item(id, "setChecked").widget, checked ? 1 : 0);
    quiet = false;
}

Bul _isChecked(Int id) {
    return gtk.toggle_button_get_active(item(id, "checked").widget) != 0;
}

void _listAdd(Int id, const Str& entry) {
    Item& i = item(id, "add");
    P label = gtk.label_new(entry.c_str());
    gtk.label_set_xalign(label, 0.0f);
    gtk.list_box_insert(i.widget, label, -1);
    gtk.widget_show_all(i.widget);
}

void _listClear(Int id) {
    Item& i = item(id, "clear");
    GList* children = static_cast<GList*>(gtk.container_get_children(i.widget));
    for (GList* c = children; c; c = c->next) gtk.widget_destroy(c->data);
    if (children) gtk.g_list_free(children);
}

Int _listSelected(Int id) {
    P row = gtk.list_box_get_selected_row(item(id, "selected").widget);
    return row ? gtk.list_box_row_get_index(row) : -1;
}

void _message(const Str& title, const Str& text) {
    start();
    P owner = nullptr;
    for (const auto& i : items) {
        if (i.open && i.kind == "window") owner = i.widget;
    }
    // GTK_DIALOG_MODAL | DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_OK
    P dialog = gtk.message_dialog_new(owner, 1 | 2, 0, 1, "%s", text.c_str());
    gtk.window_set_title(dialog, title.c_str());
    gtk.dialog_run(dialog);
    gtk.widget_destroy(dialog);
}

List<Str> _nextEvent() {
    while (events.empty()) {
        bool anyOpen = false;
        for (const auto& i : items) {
            if (i.open && i.kind == "window") anyOpen = true;
        }
        if (!anyOpen) return {"idle", ""};
        gtk.main_iteration_do(1);
    }
    List<Str> event = events.front();
    events.pop_front();
    return event;
}

} // namespace dessa::ashwee
