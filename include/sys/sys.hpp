// $(<sys>)
#pragma once
#include "dessa_runtime.hpp"

namespace dessa::sys {

// Command-line arguments passed to the program (not including its name).
inline List<Str> args() { return program_args(); }

[[noreturn]] inline void exit(Int code) {
    std::cout.flush();
    std::exit(static_cast<int>(code));
}

inline Str platform() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

} // namespace dessa::sys
