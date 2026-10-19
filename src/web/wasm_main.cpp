// The WebAssembly build of the compiler (web/dessa.js): compiles Dessa to
// JavaScript inside a browser. Built with Emscripten:
//
//     emcmake cmake -S . -B build-wasm && cmake --build build-wasm
//
// The page's loader (web/engine.js) writes the program's files into the
// in-memory file system under /app, calls dessa_compile("/app/main.ds") and
// runs the JavaScript it returns. The core libraries, the runtime and the
// Dessa-only riv packages are embedded under /dessa.
#include <emscripten/emscripten.h>

#include <cstdlib>
#include <unistd.h>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "analyser.hpp"
#include "builtins.hpp"
#include "jsgen.hpp"
#include "json.hpp"
#include "loader.hpp"

using namespace dessa;

namespace {

const char* severityName(DiagnosticSeverity s) {
    switch (s) {
        case DiagnosticSeverity::Error: return "error";
        case DiagnosticSeverity::Warning: return "warning";
        case DiagnosticSeverity::Note: return "note";
        case DiagnosticSeverity::Hint: return "hint";
    }
    return "error";
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

} // namespace

extern "C" {

// {"ok": true|false, "js": "...", "diagnostics": [{file, line, column, severity, message}, ...]}
EMSCRIPTEN_KEEPALIVE const char* dessa_compile(const char* entryPath) {
    static std::string result;
    setenv("DESSA_PACKAGES", "/dessa/packages", 1);
    chdir("/app");   // file names in messages: "app.ds:12", not "app/app.ds:12"

    std::vector<Diagnostic> diagnostics;
    ModuleLoader loader(diagnostics);
    loader.setIncludeDir("/dessa/include");
    Program program = loader.load(entryPath);
    Analyser analyser;
    bool ok = !program.modules.empty();
    for (const auto& d : diagnostics) if (d.severity == DiagnosticSeverity::Error) ok = false;
    std::string js;
    if (!program.modules.empty()) {
        analyser.analyse(program);
        for (const auto& d : analyser.getDiagnostics()) {
            if (d.severity == DiagnosticSeverity::Hint) continue;   // editor-only
            diagnostics.push_back(d);
            if (d.severity == DiagnosticSeverity::Error) ok = false;
        }
    }
    if (ok) {
        static const std::set<std::string> withJs = {"math", "os", "sys", "time", "random", "dcrypto"};
        for (const auto& lib : analyser.builtinLibrariesUsed()) {
            if (!withJs.count(lib)) {
                diagnostics.push_back({DiagnosticSeverity::Error,
                                       "The library '" + lib + "' has a C++ part, so it can't run in the browser", "", 0, 0, "", 0});
                ok = false;
            }
        }
    }
    if (ok) {
        JsCodegen generator(program, analyser);
        js = generator.generate(readFile("/dessa/runtime/dessa_runtime.js"));
    }

    std::string out = "{\"ok\":" + std::string(ok ? "true" : "false") + ",\"js\":" + json::quote(js) + ",\"diagnostics\":[";
    for (size_t i = 0; i < diagnostics.size(); ++i) {
        const auto& d = diagnostics[i];
        out += (i ? "," : "") + std::string("{\"file\":") + json::quote(d.file) + ",\"line\":" + std::to_string(d.line) +
               ",\"column\":" + std::to_string(d.column) + ",\"severity\":\"" + severityName(d.severity) +
               "\",\"message\":" + json::quote(d.message) + "}";
    }
    result = out + "]}";
    return result.c_str();
}

EMSCRIPTEN_KEEPALIVE const char* dessa_version() {
    return "0.1.5";
}

} // extern "C"
