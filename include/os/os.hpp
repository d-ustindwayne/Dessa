// $("os")
#pragma once
#include "dessa_runtime.hpp"
#include <filesystem>
#include <fstream>

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
    return std::system(command.c_str());
}

} // namespace dessa::os
