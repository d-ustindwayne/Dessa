#pragma once

#include <string>
#include <vector>

namespace dessa::toolchain {

// The folder containing dessa_runtime.hpp. Searched in order:
//   $DESSA_HOME/runtime, <exe dir>/../runtime, <exe dir>/runtime, the
//   source tree the compiler was built from.
std::string runtimeDir(const char* argv0);

// The folder with the builtin libraries (include/<lib>/), next to runtime/.
std::string includeDir(const char* argv0);

// Compiles generated C++ into `outputExe`. On failure `error` holds the
// compiler's output.
// `libraries`: the builtin libraries the program imports ($("sql") needs
// extra link flags and a C file compiled in).
bool compile(const std::string& cpp, const std::string& outputExe, const char* argv0, std::string& error, bool fast,
             const std::vector<std::string>& libraries = {});

// Like compile(), but reuses a previous build of identical code from the
// cache folder. `exe` receives the executable's path.
bool compileCached(const std::string& cpp, const char* argv0, std::string& exe, std::string& error,
                   const std::vector<std::string>& libraries = {});

// Generated C (from CCodegen), not C++
bool isCSource(const std::string& source);

// ---- WebAssembly (--target wasm, and pages) ----
// The compiler used for it ("" if there is none): DESSA_WASM_CC, a wasi-sdk
// (<Dessa>/wasm from `dessa setup wasm`, WASI_SDK_PATH, the usual places),
// or Emscripten's emcc
std::string wasmCompiler(const char* argv0);
// True when that compiler is Emscripten (its programs can't use files)
bool wasmIsEmscripten(const char* argv0);
// How to get one (for messages)
std::string wasmHelp();
// Generated C (CCodegen, setWasm) to a .wasm file. `page`: a page's script
bool compileWasm(const std::string& c, const std::string& outputWasm, const char* argv0, std::string& error, bool page);
// The same, reusing an earlier build of identical code
bool compileWasmCached(const std::string& c, const char* argv0, std::string& wasm, std::string& error, bool page);

// Runs an executable, forwarding stdio, and returns its exit code.
int run(const std::string& exe, const std::vector<std::string>& args);

} // namespace dessa::toolchain
