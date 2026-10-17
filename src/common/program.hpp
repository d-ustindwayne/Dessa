#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ast.hpp"

namespace dessa {

enum class DiagnosticSeverity {
    Error,
    Warning,
    Note,
    Hint        // only for editors (an unused import), never printed
};

struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string message;
    std::string file;   // path of the .ds file ("" if not tied to one)
    int line = 0;
    int column = 0;
    std::string tag;    // "unused": editors show it faded
    int length = 0;     // characters it covers (0: just the word there)
};

// One parsed .ds file.
struct Module {
    int id = 0;               // index into Program::modules
    std::string path;         // absolute, normalised path of the file
    std::string displayPath;  // path as shown in diagnostics
    std::string source;
    std::vector<StmtPtr> ast;
    bool isEntry = false;
    // The Dessa part (.dm) of a library whose native part is JavaScript
    // (<lib>/<lib>.js, e.g. webutils): the path of that .js file.
    std::string jsNative;

    // For every user-module spec in this file's imports, the id of the
    // module it resolved to. Builtin specs ($("math")) are not listed.
    std::unordered_map<const ModuleSpec*, int> resolvedImports;
};

// Every module reachable from the entry file, dependencies first: a module
// always appears after all the modules it imports. The entry file is last.
struct Program {
    std::vector<std::unique_ptr<Module>> modules;

    Module& entry() { return *modules.back(); }
    const Module& entry() const { return *modules.back(); }
};

} // namespace dessa
