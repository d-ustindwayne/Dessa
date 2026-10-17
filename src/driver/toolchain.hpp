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

// Runs an executable, forwarding stdio, and returns its exit code.
int run(const std::string& exe, const std::vector<std::string>& args);

} // namespace dessa::toolchain
