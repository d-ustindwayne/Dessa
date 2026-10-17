// Win32 implementation of $(<ashwee>). Compiled once per machine and linked
// into programs that import ashwee.
#include "ashwee.hpp"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <commctrl.h>

#include <deque>
#include <filesystem>
#include <fstream>
#include <vector>

namespace dessa::ashwee {

namespace {

struct Item {
    HWND hwnd = nullptr;
    Int window = -1;      // the window a control belongs to (itself for a window)
    Str kind;
    bool open = true;
};

std::vector<Item> items;
std::deque<List<Str>> events;
HFONT font = nullptr;
bool started = false;
const wchar_t* const kClass = L"AshweeWindow";

std::wstring wide(const Str& text) {
    if (text.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    // Windows controls want \r\n line breaks
    std::wstring fixed;
    for (wchar_t c : out) {
        if (c == L'\n' && (fixed.empty() || fixed.back() != L'\r')) fixed += L'\r';
        fixed += c;
    }
    return fixed;
}

Str narrow(const std::wstring& text) {
    if (text.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    Str out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    Str fixed;
    for (char c : out) {
        if (c != '\r') fixed += c;
    }
    return fixed;
}

// Sizes are given in "96 dpi" pixels and scaled for the screen.
int scaled(Int value) {
    HDC screen = GetDC(nullptr);
    int dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);
    return MulDiv(static_cast<int>(value), dpi, 96);
}

Item& item(Int id, const char* what) {
    if (id < 0 || id >= static_cast<Int>(items.size()) || !items[static_cast<size_t>(id)].open) {
        fail(std::string("ashwee: ") + what + ": no such window or control (" + std::to_string(id) + ")");
    }
    return items[static_cast<size_t>(id)];
}

Int idOf(HWND hwnd) {
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].hwnd == hwnd && items[i].open) return static_cast<Int>(i);
    }
    return -1;
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_COMMAND: {
            Int id = idOf(reinterpret_cast<HWND>(lParam));
            if (id < 0) break;
            const Str& kind = items[static_cast<size_t>(id)].kind;
            WORD code = HIWORD(wParam);
            if ((kind == "button" || kind == "checkbox") && code == BN_CLICKED) events.push_back({"click", std::to_string(id)});
            else if ((kind == "input" || kind == "password" || kind == "textarea") && code == EN_CHANGE) events.push_back({"change", std::to_string(id)});
            else if (kind == "list" && code == LBN_SELCHANGE) events.push_back({"select", std::to_string(id)});
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            // Labels and checkboxes take the window's background
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkMode(dc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_CLOSE: {
            Int id = idOf(hwnd);
            if (id >= 0) events.push_back({"close", std::to_string(id)});
            return 0; // ashwee.dm decides (it calls _close)
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

// Modern-looking controls (Common Controls 6) without a resource file:
// activate a manifest written to a temporary file.
void enableVisualStyles() {
    const char* manifest =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
        "<dependency><dependentAssembly><assemblyIdentity type=\"win32\" name=\"Microsoft.Windows.Common-Controls\" "
        "version=\"6.0.0.0\" processorArchitecture=\"*\" publicKeyToken=\"6595b64144ccf1df\" language=\"*\"/>"
        "</dependentAssembly></dependency></assembly>";
    std::error_code ec;
    std::filesystem::path path = std::filesystem::temp_directory_path(ec) / "dessa_ashwee.manifest";
    {
        std::ofstream out(path, std::ios::binary);
        out << manifest;
    }
    std::wstring file = path.wstring();
    ACTCTXW context = {};
    context.cbSize = sizeof(context);
    context.lpSource = file.c_str();
    HANDLE handle = CreateActCtxW(&context);
    if (handle != INVALID_HANDLE_VALUE) {
        ULONG_PTR cookie = 0;
        ActivateActCtx(handle, &cookie);
    }
}

void start() {
    if (started) return;
    started = true;
    SetProcessDPIAware();
    enableVisualStyles();
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    NONCLIENTMETRICSW metrics = {};
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
    font = CreateFontIndirectW(&metrics.lfMessageFont);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = windowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
}

} // namespace

Int _window(const Str& title, Int width, Int height) {
    start();
    // Client area of width x height
    RECT rect = {0, 0, scaled(width), scaled(height)};
    DWORD style = WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME;
    AdjustWindowRect(&rect, style, FALSE);
    HWND hwnd = CreateWindowExW(0, kClass, wide(title).c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT,
                                rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) fail("ashwee: could not create a window");
    Int id = static_cast<Int>(items.size());
    items.push_back({hwnd, id, "window", true});
    return id;
}

Int _control(Int window, const Str& kind, const Str& text, Int x, Int y, Int width, Int height) {
    HWND parent = item(window, "add a control").hwnd;
    const wchar_t* cls = L"STATIC";
    DWORD style = WS_CHILD | WS_VISIBLE;
    DWORD exStyle = 0;
    if (kind == "label") {
        style |= SS_LEFT;
    } else if (kind == "button") {
        cls = L"BUTTON";
        style |= BS_PUSHBUTTON | WS_TABSTOP;
    } else if (kind == "checkbox") {
        cls = L"BUTTON";
        style |= BS_AUTOCHECKBOX | WS_TABSTOP;
    } else if (kind == "input" || kind == "password") {
        cls = L"EDIT";
        style |= ES_AUTOHSCROLL | WS_TABSTOP | (kind == "password" ? ES_PASSWORD : 0);
        exStyle = WS_EX_CLIENTEDGE;
    } else if (kind == "textarea") {
        cls = L"EDIT";
        style |= ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL | WS_TABSTOP;
        exStyle = WS_EX_CLIENTEDGE;
    } else if (kind == "list") {
        cls = L"LISTBOX";
        style |= LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP | LBS_NOINTEGRALHEIGHT;
        exStyle = WS_EX_CLIENTEDGE;
    } else {
        fail("ashwee: unknown control kind '" + kind + "'");
    }
    HWND hwnd = CreateWindowExW(exStyle, cls, wide(text).c_str(), style, scaled(x), scaled(y), scaled(width),
                                scaled(height), parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) fail("ashwee: could not create a " + kind);
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    Int id = static_cast<Int>(items.size());
    items.push_back({hwnd, window, kind, true});
    return id;
}

void _show(Int window) {
    HWND hwnd = item(window, "show").hwnd;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);
}

void _close(Int window) {
    Item& w = item(window, "close");
    DestroyWindow(w.hwnd);
    for (auto& i : items) {
        if (i.window == window) i.open = false;
    }
}

void _setText(Int id, const Str& text) {
    SetWindowTextW(item(id, "setText").hwnd, wide(text).c_str());
}

Str _getText(Int id) {
    HWND hwnd = item(id, "text").hwnd;
    int length = GetWindowTextLengthW(hwnd);
    std::wstring buffer(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(hwnd, buffer.data(), length + 1);
    buffer.resize(static_cast<size_t>(length));
    return narrow(buffer);
}

void _setEnabled(Int id, Bul enabled) {
    EnableWindow(item(id, "setEnabled").hwnd, enabled ? TRUE : FALSE);
}

void _setChecked(Int id, Bul checked) {
    SendMessageW(item(id, "setChecked").hwnd, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}

Bul _isChecked(Int id) {
    return SendMessageW(item(id, "checked").hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void _listAdd(Int id, const Str& entry) {
    SendMessageW(item(id, "add").hwnd, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(entry).c_str()));
}

void _listClear(Int id) {
    SendMessageW(item(id, "clear").hwnd, LB_RESETCONTENT, 0, 0);
}

Int _listSelected(Int id) {
    LRESULT index = SendMessageW(item(id, "selected").hwnd, LB_GETCURSEL, 0, 0);
    return index == LB_ERR ? -1 : static_cast<Int>(index);
}

void _message(const Str& title, const Str& text) {
    start();
    HWND owner = nullptr;
    for (const auto& i : items) {
        if (i.open && i.kind == "window") owner = i.hwnd;
    }
    MessageBoxW(owner, wide(text).c_str(), wide(title).c_str(), MB_OK | MB_ICONINFORMATION);
}

List<Str> _nextEvent() {
    while (events.empty()) {
        bool anyOpen = false;
        for (const auto& i : items) {
            if (i.open && i.kind == "window") anyOpen = true;
        }
        if (!anyOpen) return {"idle", ""};
        MSG message;
        if (GetMessageW(&message, nullptr, 0, 0) <= 0) return {"idle", ""};
        // Tab moves between controls
        HWND root = GetAncestor(message.hwnd, GA_ROOT);
        if (root && IsDialogMessageW(root, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    List<Str> event = events.front();
    events.pop_front();
    return event;
}

} // namespace dessa::ashwee

#elif defined(__linux__)
#include "ashwee_gtk.inc"
#endif   // macOS: ashwee_mac.mm
