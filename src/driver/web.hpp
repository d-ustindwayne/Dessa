#pragma once

#include <functional>
#include <string>
#include <vector>

namespace dessa::web {

// Compiles Dessa source (as if it were the file `path`) to JavaScript.
// Returns false and fills `errors` (already formatted) on failure.
using CompileFn = std::function<bool(const std::string& path, const std::string& source, std::string& js, std::string& errors)>;

// Is this a Dessa page? .dhtml (every plain <script> is Dessa)
bool isDessaPage(const std::string& path);
// .dhtml, .html or .htm: a page that may hold Dessa scripts
bool isPage(const std::string& path);

// One <script> of a page
struct Script {
    size_t open = 0, tagEnd = 0, close = 0, closeEnd = 0;   // "<script", its ">", "</script", after its ">"
    bool dessa = false;
    bool hasSrc = false;
    std::string src;
};
std::vector<Script> scripts(const std::string& path, const std::string& page);

// The page with everything except the given inline Dessa scripts blanked
// (newlines kept), so a Dessa error's line and column are the page's own.
// `only` = keep just the script containing this byte offset (npos = all).
std::string dessaOnly(const std::string& path, const std::string& page, size_t only = std::string::npos);
// Byte offset of 1-based line / column
size_t offsetOf(const std::string& text, int line, int column);

// The HTML of a page with its Dessa scripts compiled to JavaScript:
//   .dhtml: <script> without a type, or with type="text/dessa" / "text/ds"
//   .html:  only <script type="text/dessa"> / "text/ds"
// <script src="app.ds"> is compiled from the file. Positions in error
// messages are those of the page itself.
bool buildPage(const std::string& path, const CompileFn& compile, std::string& html, std::string& errors);

// A small web server for trying pages: .dhtml pages (and .html pages with
// Dessa scripts) are compiled on every request. Blocks until stopped.
// `open` = a page to open in the browser once the server runs ("" = none).
int serve(const std::string& folder, int port, const std::string& open, const CompileFn& compile);

} // namespace dessa::web
