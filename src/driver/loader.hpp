#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "program.hpp"

namespace dessa {

// Reads the entry file and every .ds file it (transitively) imports.
//
// $("my.other.directory") resolves to my/other/directory.ds, searched for
// first next to the importing file, then in the project root (the folder
// holding the entry file). Import cycles are reported as errors.
class ModuleLoader {
public:
    explicit ModuleLoader(std::vector<Diagnostic>& diagnostics);

    // Folder of the builtin libraries. `$(<json>)` also loads
    // <includeDir>/json/json.dm when a library has a Dessa part.
    void setIncludeDir(std::string dir) { includeDir = std::move(dir); }

    // Always returns a Program (possibly partial); check the diagnostics.
    Program load(const std::string& entryPath);

    // Also used for single-source tooling (e.g. the VS Code extension
    // checking an unsaved buffer): `sourceOverride` replaces the entry
    // file's contents on disk.
    Program load(const std::string& entryPath, const std::string* sourceOverride);

private:
    std::vector<Diagnostic>& diagnostics;
    std::string projectRoot;
    std::string includeDir;

    struct State {
        std::vector<std::string> loadingStack; // for cycle messages
    };

    int visit(Program& program, const std::string& path, const std::string* sourceOverride, State& state,
              const std::string& fromFile, int fromLine, int fromColumn);
    std::string resolve(const std::string& importingFile, const std::string& dotted) const;
    std::filesystem::path libraryFolder(const std::string& dotted, bool includeBuiltins = true) const;
    void addError(const std::string& file, int line, int column, const std::string& message);
};

// Splits "[line 3, col 7] Parse error at 'x': message" into a Diagnostic.
Diagnostic diagnosticFromMessage(const std::string& file, const std::string& message);

} // namespace dessa
