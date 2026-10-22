// $("os")
#pragma once
#include "dessa_runtime.hpp"
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

namespace dessa::os {

inline Str readFile(const Str& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) fail("Cannot read file '" + path + "'");
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

inline void writeFile(const Str& path, const Str& content) {
    std::ofstream out(path, std::ios::binary);
    if (!out) fail("Cannot write file '" + path + "'");
    out << content;
}

inline void appendFile(const Str& path, const Str& content) {
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out) fail("Cannot write file '" + path + "'");
    out << content;
}

inline Bul fileExists(const Str& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

inline Bul isDir(const Str& path) {
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

// Deletes a file or a whole folder with everything in it.
// Returns how many files and folders were removed.
inline Int deleteAll(const Str& path) {
    std::error_code ec;
    auto removed = std::filesystem::remove_all(path, ec);
    if (ec) fail("Cannot delete '" + path + "': " + ec.message());
    return static_cast<Int>(removed);
}

inline Bul deleteFile(const Str& path) {
    std::error_code ec;
    return std::filesystem::remove(path, ec);
}

inline List<Str> listDir(const Str& path) {
    List<Str> names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
        names.push_back(entry.path().filename().string());
    }
    if (ec) fail("Cannot list folder '" + path + "': " + ec.message());
    std::sort(names.begin(), names.end());
    return names;
}

inline Bul makeDir(const Str& path) {
    std::error_code ec;
    return std::filesystem::create_directories(path, ec);
}

inline Str cwd() {
    std::error_code ec;
    return std::filesystem::current_path(ec).string();
}

inline Opt<Str> getEnv(const Str& name) {
    const char* value = std::getenv(name.c_str());
    if (!value) return std::nullopt;
    return Str(value);
}

// Runs a shell command and returns its exit code.
inline Int run(const Str& command) {
    std::cout.flush();
    std::fflush(stdout);
#ifdef _WIN32
    // cmd.exe drops the first and last quote of the line: "a b.exe" "x" would break
    return std::system(("\"" + command + "\"").c_str());
#else
    return std::system(command.c_str());
#endif
}

// ---- helpers for class File in os.dm: open files by number ----

struct OpenFile {
    std::fstream stream;
    Str path;
    Str mode;   // "r", "w" or "a"
};

inline std::vector<std::unique_ptr<OpenFile>>& openFiles() {
    static std::vector<std::unique_ptr<OpenFile>> files;
    return files;
}

inline OpenFile& openFile(Int handle, bool reading) {
    auto& files = openFiles();
    if (handle < 0 || handle >= static_cast<Int>(files.size()) || !files[static_cast<size_t>(handle)]) fail("The file is closed");
    OpenFile& f = *files[static_cast<size_t>(handle)];
    if (reading && f.mode != "r") fail("File '" + f.path + "' is open for writing, not reading");
    if (!reading && f.mode == "r") fail("File '" + f.path + "' is open for reading, not writing (open it with \"w\" or \"a\")");
    return f;
}

inline Int _fileOpen(const Str& path, const Str& mode) {
    std::ios::openmode how;
    if (mode == "r") how = std::ios::in | std::ios::binary;
    else if (mode == "w") how = std::ios::out | std::ios::trunc | std::ios::binary;
    else if (mode == "a") how = std::ios::out | std::ios::app | std::ios::binary;
    else fail("open: mode must be \"r\" (read), \"w\" (write) or \"a\" (add to the end), got \"" + mode + "\"");
    auto f = std::make_unique<OpenFile>();
    f->stream.open(path, how);
    if (!f->stream.is_open()) fail(mode == "r" ? "Cannot read file '" + path + "'" : "Cannot write file '" + path + "'");
    f->path = path;
    f->mode = mode;
    openFiles().push_back(std::move(f));
    return static_cast<Int>(openFiles().size()) - 1;
}

inline Opt<Str> _fileReadLine(Int handle) {
    OpenFile& f = openFile(handle, true);
    Str line;
    if (!std::getline(f.stream, line)) return std::nullopt;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

inline Str _fileRead(Int handle) {
    OpenFile& f = openFile(handle, true);
    std::ostringstream rest;
    rest << f.stream.rdbuf();
    f.stream.clear();
    return rest.str();
}

inline Bul _fileAtEnd(Int handle) {
    OpenFile& f = openFile(handle, true);
    return f.stream.peek() == std::char_traits<char>::eof();
}

inline void _fileWrite(Int handle, const Str& text) {
    OpenFile& f = openFile(handle, false);
    f.stream << text;
    if (!f.stream) fail("Cannot write file '" + f.path + "'");
}

inline void _fileFlush(Int handle) {
    openFile(handle, false).stream.flush();
}

inline void _fileClose(Int handle) {
    auto& files = openFiles();
    if (handle >= 0 && handle < static_cast<Int>(files.size())) files[static_cast<size_t>(handle)].reset();
}

} // namespace dessa::os
