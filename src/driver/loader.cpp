#include "loader.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

#include "builtins.hpp"
#include "lexer.hpp"
#include "parser.hpp"

namespace fs = std::filesystem;

namespace dessa {

namespace {

std::string normalise(const fs::path& p) {
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(fs::absolute(p), ec);
    return (ec ? fs::absolute(p) : canonical).lexically_normal().string();
}

std::string displayPath(const std::string& absolute) {
    std::error_code ec;
    fs::path rel = fs::relative(absolute, fs::current_path(), ec);
    if (ec || rel.empty() || rel.string().rfind("..", 0) == 0) return fs::path(absolute).generic_string();
    return rel.generic_string();
}

} // namespace

Diagnostic diagnosticFromMessage(const std::string& file, const std::string& message) {
    static const std::regex pattern(R"(^\[line (\d+)(?:, col (\d+))?\] (?:Lex error|Parse error)?:?\s*(.*)$)");
    Diagnostic d;
    d.file = file;
    std::smatch m;
    if (std::regex_match(message, m, pattern)) {
        d.line = std::stoi(m[1]);
        d.column = m[2].matched ? std::stoi(m[2]) : 0;
        d.message = m[3];
    } else {
        d.message = message;
    }
    return d;
}

ModuleLoader::ModuleLoader(std::vector<Diagnostic>& diagnostics) : diagnostics(diagnostics) {}

void ModuleLoader::addError(const std::string& file, int line, int column, const std::string& message) {
    diagnostics.push_back({DiagnosticSeverity::Error, message, file, line, column});
}

Program ModuleLoader::load(const std::string& entryPath) {
    return load(entryPath, nullptr);
}

Program ModuleLoader::load(const std::string& entryPath, const std::string* sourceOverride) {
    Program program;
    std::string entry = normalise(entryPath);
    projectRoot = fs::path(entry).parent_path().string();
    State state;
    int id = visit(program, entry, sourceOverride, state, "", 0, 0);
    if (id >= 0) program.modules.back()->isEntry = true;
    return program;
}

std::string ModuleLoader::resolve(const std::string& importingFile, const std::string& dotted) const {
    std::string relative = dotted;
    std::replace(relative.begin(), relative.end(), '.', '/');
    // All Dessa file types, in order of preference. (They are compiled the
    // same way for now.)
    static const char* const extensions[] = {".ds", ".dm", ".dx", ".dz", ".dsx", ".dxx", ".dzx", ".dds", ".dss"};
    const fs::path folders[] = {fs::path(importingFile).parent_path(), fs::path(projectRoot)};
    for (const auto& folder : folders) {
        for (const char* extension : extensions) {
            fs::path candidate = folder / (relative + extension);
            std::error_code ec;
            if (fs::is_regular_file(candidate, ec)) return normalise(candidate);
        }
    }

    // Packages installed by riv live in riv_modules/<package>/ (see libraryFolder):
    //   $("colors")         -> riv_modules/colors/lib.ds (or colors.ds)
    //   $("colors.palette") -> riv_modules/colors/palette.ds
    const std::string package = dotted.substr(0, dotted.find('.'));
    const fs::path packageDir = libraryFolder(package, false);
    if (packageDir.empty()) return "";
    std::vector<std::string> inPackage;
    if (dotted.find('.') == std::string::npos) {
        inPackage = {"lib", package};
    } else {
        std::string rest = dotted.substr(dotted.find('.') + 1);
        std::replace(rest.begin(), rest.end(), '.', '/');
        inPackage = {rest};
    }
    for (const auto& name : inPackage) {
        for (const char* extension : extensions) {
            fs::path candidate = packageDir / (name + extension);
            std::error_code ec;
            if (fs::is_regular_file(candidate, ec)) return normalise(candidate);
        }
    }
    return "";
}

// The folder of library `dotted` ($(<sql>), $(<ui.widgets>)), first found of:
//   include/sql/                        built into Dessa
//   <project or a parent>/riv_modules/sql/   installed with `riv add sql`
//   <each folder in DESSA_PACKAGES>/sql/      extra package folders
fs::path ModuleLoader::libraryFolder(const std::string& dotted, bool includeBuiltins) const {
    std::string relative = dotted;
    std::replace(relative.begin(), relative.end(), '.', '/');
    std::error_code ec;
    if (includeBuiltins && !includeDir.empty() && fs::is_directory(fs::path(includeDir) / relative, ec)) {
        return fs::path(normalise(fs::path(includeDir) / relative));
    }
    for (fs::path dir = fs::path(projectRoot); !dir.empty(); dir = dir.parent_path()) {
        fs::path candidate = dir / "riv_modules" / relative;
        if (fs::is_directory(candidate, ec)) return fs::path(normalise(candidate));
        if (dir == dir.parent_path()) break;
    }
    if (const char* extra = std::getenv("DESSA_PACKAGES"); extra && *extra) {
#ifdef _WIN32
        const char separator = ';';
#else
        const char separator = ':';
#endif
        std::string list = extra;
        size_t start = 0;
        while (start <= list.size()) {
            size_t end = list.find(separator, start);
            std::string entry = list.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!entry.empty() && fs::is_directory(fs::path(entry) / relative, ec)) {
                return fs::path(normalise(fs::path(entry) / relative));
            }
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }
    return {};
}

int ModuleLoader::visit(Program& program, const std::string& path, const std::string* sourceOverride, State& state,
                        const std::string& fromFile, int fromLine, int fromColumn) {
    // Already loaded?
    for (const auto& m : program.modules) {
        if (m->path == path) return m->id;
    }
    // Currently loading -> cycle
    auto onStack = std::find(state.loadingStack.begin(), state.loadingStack.end(), path);
    if (onStack != state.loadingStack.end()) {
        std::string cycle;
        for (auto it = onStack; it != state.loadingStack.end(); ++it) cycle += displayPath(*it) + " -> ";
        cycle += displayPath(path);
        addError(fromFile, fromLine, fromColumn, "Import cycle: " + cycle);
        return -1;
    }

    auto module = std::make_unique<Module>();
    module->path = path;
    module->displayPath = displayPath(path);

    if (sourceOverride) {
        module->source = *sourceOverride;
    } else {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            addError(fromFile, fromLine, fromColumn, "Could not open file '" + module->displayPath + "'");
            return -1;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        module->source = buffer.str();
    }

    Lexer lexer(module->source);
    std::vector<Token> tokens = lexer.scanTokens();
    for (const auto& e : lexer.getErrors()) diagnostics.push_back(diagnosticFromMessage(module->displayPath, e));

    Parser parser(std::move(tokens));
    module->ast = parser.parseProgram();
    for (const auto& e : parser.getErrors()) diagnostics.push_back(diagnosticFromMessage(module->displayPath, e));

    state.loadingStack.push_back(path);
    for (const auto& statement : module->ast) {
        if (!statement || statement->kind != StmtKind::Import) continue;
        const auto* import = static_cast<const ImportStmt*>(statement.get());
        for (const auto& spec : import->modules) {
            if (spec.isBuiltin) {
                // $(<lib>): include/<lib>/ or an installed riv package. Its
                // native part is in the compiler (include/) or described by
                // <lib>.native (package); its Dessa part is <lib>.dm.
                const std::string last = spec.path.substr(spec.path.rfind('.') + 1);
                fs::path folder = libraryFolder(spec.path);
                if (folder.empty()) continue; // the analyser reports unknown libraries
                std::error_code ec;
                const bool isPackage = includeDir.empty() || folder.string().rfind(fs::path(includeDir).string(), 0) != 0;
                if (isPackage && fs::is_regular_file(folder / (last + ".native"), ec)) {
                    std::string error;
                    if (!loadNativeLibrary(folder.generic_string(), last, error)) {
                        addError(module->displayPath, spec.line, spec.column, "Library <" + spec.path + ">: " + error);
                    }
                }
                fs::path dm = folder / (last + ".dm");
                if (isPackage && !fs::is_regular_file(dm, ec)) dm = folder / "lib.ds";
                if (!fs::is_regular_file(dm, ec)) continue;
                std::string target = normalise(dm);
                if (target == path) continue; // the library's own .dm uses only the native part
                int childId = visit(program, target, nullptr, state, module->displayPath, spec.line, spec.column);
                if (childId >= 0) {
                    module->resolvedImports[&spec] = childId;
                    // A JavaScript native part (webutils.js): only for --target web
                    if (fs::is_regular_file(folder / (last + ".js"), ec)) {
                        program.modules[static_cast<size_t>(childId)]->jsNative = normalise(folder / (last + ".js"));
                    }
                }
                continue;
            }
            std::string target = resolve(path, spec.path);
            if (target.empty()) {
                std::string rel = spec.path;
                std::replace(rel.begin(), rel.end(), '.', '/');
                addError(module->displayPath, spec.line, spec.column,
                         "Cannot find module '" + spec.path + "' (looked for " + rel + ".ds / .dm / .dx ... next to this file and in the project root)" +
                         (spec.path.find('/') != std::string::npos || spec.path.find(".ds") != std::string::npos
                              ? "; write module paths with dots, e.g. \"utils.strings\"" : ""));
                continue;
            }
            if (target == path) {
                addError(module->displayPath, spec.line, spec.column, "A module cannot import itself");
                continue;
            }
            int childId = visit(program, target, nullptr, state, module->displayPath, spec.line, spec.column);
            if (childId >= 0) module->resolvedImports[&spec] = childId;
        }
    }
    state.loadingStack.pop_back();

    module->id = static_cast<int>(program.modules.size());
    program.modules.push_back(std::move(module));
    return program.modules.back()->id;
}

} // namespace dessa
