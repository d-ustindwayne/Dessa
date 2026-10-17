#include "toolchain.hpp"
#include "builtins.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace fs = std::filesystem;

namespace dessa::toolchain {

namespace {

std::string quoteArg(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else out += c;
    }
    return out + "\"";
}

int system(const std::string& command) {
#ifdef _WIN32
    // cmd.exe strips the first and last quote of the whole line.
    int code = std::system(("\"" + command + "\"").c_str());
    return code;
#else
    int status = std::system(command.c_str());
    if (status == -1) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
}

std::string compiler() {
    if (const char* cxx = std::getenv("DESSA_CXX"); cxx && *cxx) return cxx;
    return "g++";
}

fs::path exeDir(const char* argv0) {
    std::error_code ec;
    // Ask the OS: argv[0] is just "dessa" when it's started from PATH
#ifdef _WIN32
    wchar_t buffer[32768];
    DWORD length = GetModuleFileNameW(nullptr, buffer, 32768);
    if (length > 0 && length < 32768) return fs::path(std::wstring(buffer, length)).parent_path();
#elif defined(__linux__)
    fs::path self = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return self.parent_path();
#elif defined(__APPLE__)
    char buffer[4096];
    uint32_t size = sizeof buffer;
    if (_NSGetExecutablePath(buffer, &size) == 0) return fs::weakly_canonical(fs::path(buffer), ec).parent_path();
#endif
    fs::path p = fs::weakly_canonical(fs::absolute(argv0 ? argv0 : ""), ec);
    if (ec || !fs::exists(p)) {
#ifdef _WIN32
        p = fs::weakly_canonical(fs::absolute(std::string(argv0 ? argv0 : "") + ".exe"), ec);
#endif
    }
    return p.parent_path();
}

fs::path workDir() {
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec) / "dessa";
    fs::create_directories(dir, ec);
    return dir;
}

std::uint64_t fnv1a(const std::string& data, std::uint64_t hash = 1469598103934665603ull) {
    for (unsigned char c : data) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string hex(std::uint64_t v) {
    std::ostringstream out;
    out << std::hex << v;
    return out.str();
}

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

} // namespace

std::string runtimeDir(const char* argv0) {
    std::vector<fs::path> candidates;
    if (const char* home = std::getenv("DESSA_HOME"); home && *home) candidates.push_back(fs::path(home) / "runtime");
    fs::path dir = exeDir(argv0);
    candidates.push_back(dir / ".." / "runtime");
    candidates.push_back(dir / "runtime");
#ifdef DESSA_RUNTIME_DIR
    candidates.push_back(DESSA_RUNTIME_DIR);
#endif
    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(c / "dessa_runtime.hpp", ec)) return fs::weakly_canonical(c, ec).string();
    }
    return "";
}

std::string includeDir(const char* argv0) {
    std::string runtime = runtimeDir(argv0);
    if (runtime.empty()) return "";
    std::error_code ec;
    fs::path include = fs::path(runtime).parent_path() / "include";
    return fs::is_directory(include, ec) ? fs::weakly_canonical(include, ec).string() : "";
}

// A library's C or C++ file (SQLite, the SQL driver code), compiled once and
// kept in the cache. C++ files may include dessa_runtime.hpp.
static bool cObject(const fs::path& source, std::string& object, std::string& error, const std::string& runtime,
                    const std::string& include) {
    std::error_code ec;
    const bool isC = source.extension() == ".c";
    std::uint64_t key = fnv1a(compiler(), fnv1a(readFile(source)));
    if (!isC) key = fnv1a(readFile(fs::path(runtime) / "dessa_runtime.hpp"), key);
    // Files it may include from its own folder (ashwee.cpp includes ashwee_gtk.inc)
    if (!isC) for (const auto& entry : fs::directory_iterator(source.parent_path(), ec)) {
        const auto ext = entry.path().extension();
        if (ext == ".inc" || ext == ".hpp" || ext == ".h") key = fnv1a(readFile(entry.path()), key);
    }
    fs::path dir = workDir() / "objects";
    fs::create_directories(dir, ec);
    fs::path target = dir / (source.stem().string() + "_" + hex(key) + ".o");
    object = target.string();
    if (fs::exists(target, ec)) return true;
    std::cerr << "dessa: compiling " << source.filename().string() << " (only needed once)...\n";
    fs::path log = dir / (source.stem().string() + ".log");
    // .mm: Objective-C++ (macOS windows for ashwee)
    const bool isObjC = source.extension() == ".mm";
    std::string command = quoteArg(compiler()) + (isC ? " -x c -O2" : isObjC ? " -x objective-c++ -fobjc-arc -std=c++20 -O2" : " -std=c++20 -O2") +
                          " -I" + quoteArg(runtime) + " -I" + quoteArg(include) +
                          " -c " + quoteArg(source.string()) + " -o " + quoteArg(object) +
                          " > " + quoteArg(log.string()) + " 2>&1";
    if (system(command) != 0) {
        error = "dessa: could not compile " + source.filename().string() + " for a library; details: " + log.string() + "\n";
        return false;
    }
    return true;
}

bool compile(const std::string& cpp, const std::string& outputExe, const char* argv0, std::string& error, bool fast,
             const std::vector<std::string>& libraries) {
    std::string runtime = runtimeDir(argv0);
    if (runtime.empty()) {
        error = "dessa: cannot find the runtime folder (set DESSA_HOME to the Dessa install folder)\n";
        return false;
    }

    // Extra objects and link flags of the libraries in use
    std::string extra;
    for (const auto& name : libraries) {
        const BuiltinLibrary* lib = findBuiltinLibrary(name);
        if (!lib) continue;
        // include/ for the built-in libraries, the package folder for riv packages
        const fs::path base = lib->baseDir.empty() ? fs::path(includeDir(argv0)) : fs::path(lib->baseDir);
        for (const auto& c : lib->cSources) {
            std::string object;
            if (!cObject(base / c, object, error, runtime, includeDir(argv0))) return false;
            extra += " " + quoteArg(object);
        }
        for (const auto& flag : lib->linkFlags) extra += " " + flag;
    }

    fs::path dir = workDir();
    std::string id = hex(fnv1a(cpp));
    fs::path source = dir / ("dessa_" + id + ".cpp");
    fs::path log = dir / ("dessa_" + id + ".log");
    {
        std::ofstream out(source, std::ios::binary);
        out << cpp;
    }

    std::string command = quoteArg(compiler()) + " -std=c++20 " + (fast ? "-O1" : "-O2") +
                          " -I" + quoteArg(runtime) + " -I" + quoteArg(includeDir(argv0)) + " " + quoteArg(source.string()) +
                          " -o " + quoteArg(outputExe) + extra;
#ifdef _WIN32
    command += " -static";
#endif
    command += " > " + quoteArg(log.string()) + " 2>&1";

    int code = system(command);
    if (code != 0) {
        std::string output = readFile(log);
        // The C++ compiler isn't there at all
        if (code == 127 || output.find("is not recognized") != std::string::npos ||
            output.find("command not found") != std::string::npos) {
            error = "dessa: could not run the C++ compiler '" + compiler() + "'.\n"
                    "       Dessa needs a C++20 compiler (e.g. MinGW g++ 13+). Install one, or set DESSA_CXX to its path,\n"
                    "       e.g. C:/mingw64/bin/g++.exe\n";
            return false;
        }
        // Anything else is a bug in Dessa: never show C++ errors to the user,
        // but keep everything needed to report it.
        {
            std::ofstream details(log, std::ios::app);
            details << "\n---- generated source: " << source.string() << "\n";
        }
        error = "dessa: internal compiler error -- this program uses a combination of features that Dessa\n"
                "       can't translate correctly yet. This is a bug in Dessa, not in your code.\n"
                "       Please report it with this file: " + log.string() + "\n";
        if (const char* verbose = std::getenv("DESSA_VERBOSE"); verbose && *verbose) {
            if (output.size() > 6000) output = output.substr(0, 6000) + "\n... (truncated)\n";
            error += "\n" + output;
        }
        return false;
    }
    std::error_code ec;
    fs::remove(log, ec);
    return true;
}

bool compileCached(const std::string& cpp, const char* argv0, std::string& exe, std::string& error,
                   const std::vector<std::string>& libraries) {
    std::string runtime = runtimeDir(argv0);
    // Include the runtime headers in the key so editing them rebuilds.
    std::uint64_t key = fnv1a(cpp);
    key = fnv1a(compiler(), key);
    std::error_code ec;
    if (!runtime.empty()) {
        std::vector<std::string> folders = {runtime, includeDir(argv0)};
        for (const auto& name : libraries) {
            const BuiltinLibrary* lib = findBuiltinLibrary(name);
            if (lib && !lib->baseDir.empty()) folders.push_back(lib->baseDir);   // riv package
        }
        for (const auto& folder : folders) {
            if (folder.empty()) continue;
            for (const auto& entry : fs::recursive_directory_iterator(folder, ec)) {
                if (!entry.is_regular_file(ec)) continue;
                // headers by content; big C sources (SQLite) by size and date
                const auto ext = entry.path().extension();
                if (ext == ".hpp" || ext == ".h") key = fnv1a(readFile(entry.path()), key);
                else key = fnv1a(entry.path().string() + std::to_string(entry.file_size(ec)) +
                                 // (a 128-bit count on macOS: no to_string for it)
                                 std::to_string(static_cast<long long>(entry.last_write_time(ec).time_since_epoch().count())), key);
            }
        }
    }
    fs::path cache = workDir() / "cache";
    fs::create_directories(cache, ec);
    fs::path target = cache / ("prog_" + hex(key));
#ifdef _WIN32
    target += ".exe";
#endif
    exe = target.string();
    if (fs::exists(target, ec)) return true;
    return compile(cpp, exe, argv0, error, true, libraries);
}

int run(const std::string& exe, const std::vector<std::string>& args) {
    std::string command = quoteArg(exe);
    for (const auto& a : args) command += " " + quoteArg(a);
    std::cout.flush();
    return system(command);
}

} // namespace dessa::toolchain
