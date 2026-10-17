#pragma once

#include <string>
#include <vector>

#include "semtype.hpp"

namespace dessa {

// Signatures are written as Dessa type strings ("Dbl", "Str<>", "Str | Nil").
// Two pseudo-types exist for builtins only:
//   "Num" -- any numeric type (Int, Flt, Dbl)
//   "T"   -- same type as the first argument (return type only)
//   "Any" -- anything
struct BuiltinParam {
    std::string name;
    std::string type;
};

struct BuiltinFunction {
    std::string name;
    std::vector<BuiltinParam> params;
    std::string returnType;
    std::string cpp;          // C++ callee, e.g. "dessa::math::sqrt"
    bool variadic = false;    // print(a, b, c)
};

struct BuiltinConstant {
    std::string name;
    std::string type;
    std::string cpp;
};

struct BuiltinLibrary {
    std::string name;                 // "math" in $(<math>)
    std::string header;               // "math/math.hpp", relative to include/ (absolute for a package)
    std::vector<BuiltinFunction> functions;
    std::vector<BuiltinConstant> constants;
    std::vector<std::string> linkFlags = {};   // e.g. "-lodbc32"
    std::vector<std::string> cSources = {};    // C files compiled once and linked in, relative to include/ (or baseDir)
    std::string baseDir = {};                  // a riv package's folder; "" for the libraries in include/
};

// Functions available everywhere without an import: print, input, ...
const std::vector<BuiltinFunction>& globalBuiltins();

// $(<name>) libraries; nullptr if there's no such library.
const BuiltinLibrary* findBuiltinLibrary(const std::string& name);
// The libraries compiled into dessa (math, os, sys, time).
const std::vector<BuiltinLibrary>& builtinLibraries();
// Native libraries of riv packages found while loading this program.
const std::vector<const BuiltinLibrary*>& packageLibraries();

// Reads the native part of a riv package, <folder>/<name>.native:
//
//     // comments
//     header sql.hpp                    C++ header with the declarations
//     source sqlite/sqlite3.c           compiled once, linked in (repeatable)
//     link -lfoo                        link flag (link.windows / link.unix for one OS)
//     namespace dessa::sql              C++ namespace of the functions (default dessa::<name>)
//     func[Int] _open(path[Str])        a function: Dessa signature, C++ name <namespace>::_open
//     const[Dbl] PI                     a constant
//
// On success the library is registered (findBuiltinLibrary finds it).
bool loadNativeLibrary(const std::string& folder, const std::string& name, std::string& error);

// Parses a builtin signature type string. Pseudo-types become
// SemanticType::unknown() with `name` set to "Num" / "T" / "Any".
SemanticType parseBuiltinType(const std::string& text);

} // namespace dessa
