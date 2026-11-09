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
// holding the entry file). When there is no such file, the name is a library
// ($("math"), $("sql")). Import cycles are reported as errors.
class ModuleLoader {
public:
    explicit ModuleLoader(std::vector<Diagnostic>& diagnostics);

    // Folder of the builtin libraries. `$("json")` also loads
    // <includeDir>/json/json.dm when a library has a Dessa part.
    void setIncludeDir(std::string dir) { includeDir = std::move(dir); }
    std::string preludePath() const;

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
    std::string entryFile;

    struct State {
        std::vector<std::string> loadingStack; // for cycle messages
        bool usesAsync = false;                // something loaded so far uses async
    };

    int visit(Program& program, const std::string& path, const std::string* sourceOverride, State& state,
              const std::string& fromFile, int fromLine, int fromColumn);
    std::string resolve(const std::string& importingFile, const std::string& dotted) const;
    std::string resolveLocal(const std::string& importingFile, const std::string& dotted) const;
    bool isLibrary(const std::string& dotted) const;
    bool inLibrary(const std::string& file) const;
    std::string booleanStyle(const std::string& file);
    std::filesystem::path libraryFolder(const std::string& dotted, bool includeBuiltins = true) const;
    void addError(const std::string& file, int line, int column, const std::string& message);
};

// .xds, .xdz, .xdx: files with components and @(...) markup
bool isComponentFile(const std::string& path);

// A component, as a page sees it. kind: how a tag's attribute gives this
// parameter ("Str", "Int", "Dbl", "Flt", "Bul", "Children"; "" none can)
struct ComponentParam {
    std::string name;
    std::string kind;
    bool required = false;
};
struct ComponentInfo {
    std::string name;
    std::vector<ComponentParam> params;
};
std::vector<ComponentInfo> componentsIn(const std::vector<StmtPtr>& ast);
std::vector<ComponentInfo> componentsInSource(const std::string& path, const std::string& source);
// Dessa code that puts each component in its tags in the page
std::string componentMounts(const std::vector<StmtPtr>& ast);

// Splits "[line 3, col 7] Parse error at 'x': message" into a Diagnostic.
Diagnostic diagnosticFromMessage(const std::string& file, const std::string& message);

} // namespace dessa
