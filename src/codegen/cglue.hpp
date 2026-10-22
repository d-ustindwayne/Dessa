#pragma once

#include <string>

#include "builtins.hpp"
#include "semtype.hpp"

namespace dessa {

// The bridge between C programs and libraries written in C++ (dcrypto, the
// time zone helpers, riv packages such as sql). For each such library a small
// C++ file is generated: one `extern "C"` function per library function,
// converting C runtime values (ds_str*, ds_list*) to C++ ones (dessa::Str,
// dessa::List) and back, and C++ errors into Dessa errors. It's compiled once
// and kept in the cache; C programs that use the library link it in.

// The C type of a value that can cross the bridge, or "" if it can't
// (functions, maps, Obj, ...)
std::string glueCType(const SemanticType& type);

// The library function the C++ name belongs to (nullptr if none)
const BuiltinLibrary* glueLibraryOf(const std::string& cppName, const BuiltinFunction** function,
                                    const BuiltinConstant** constant = nullptr);

// Every parameter and the result can cross
bool glueable(const BuiltinFunction& function);
bool glueable(const BuiltinConstant& constant);

// The C function for a library function or constant: dsx_<lib>_<name>
std::string glueName(const BuiltinLibrary& library, const std::string& name);

// "extern ds_str* dsx_os_cwd(void);"
std::string gluePrototype(const BuiltinLibrary& library, const BuiltinFunction& function);
std::string gluePrototype(const BuiltinLibrary& library, const BuiltinConstant& constant);

// The C++ bridge file of a library
std::string glueSource(const BuiltinLibrary& library);

} // namespace dessa
