#include "loader.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "builtins.hpp"
#include "source_text.hpp"
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
    // "[line 3, col 7] Parse error: text" (no regex: std::regex recurses on
    // each character, so a long message would use up the stack)
    Diagnostic d;
    d.file = file;
    d.message = message;
    size_t at = 6;
    const auto number = [&](int& out) {
        const size_t start = at;
        long long value = 0;
        while (at < message.size() && message[at] >= '0' && message[at] <= '9' && value < 100000000) value = value * 10 + (message[at++] - '0');
        out = static_cast<int>(value);
        return at > start;
    };
    int line = 0, column = 0;
    if (message.compare(0, at, "[line ") != 0 || !number(line)) return d;
    if (message.compare(at, 6, ", col ") == 0) {
        at += 6;
        if (!number(column)) return d;
    }
    if (message.compare(at, 2, "] ") != 0) return d;
    at += 2;
    for (const char* kind : {"Lex error", "Parse error"}) {
        const size_t length = std::string(kind).size();
        if (message.compare(at, length, kind) == 0) at += length;
    }
    if (at < message.size() && message[at] == ':') ++at;
    while (at < message.size() && (message[at] == ' ' || message[at] == '\t')) ++at;
    d.line = line;
    d.column = column;
    d.message = message.substr(at);
    return d;
}

bool isComponentFile(const std::string& path) {
    std::string extension = fs::path(path).extension().string();
    for (auto& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return extension == ".xds" || extension == ".xdz" || extension == ".xdx";
}

// What kind of value a <Tag attribute="..."> gives a parameter
static std::string pageParamKind(const TypeNode& type) {
    if (!type.unionMembers.empty() || type.isFunction || !type.group.empty() || type.isMap || type.isList || type.isSet) return "";
    if (type.name == "Str" || type.name == "Int" || type.name == "Dbl" || type.name == "Flt" || type.name == "Bul" || type.name == "Children") return type.name;
    return "";
}

std::vector<ComponentInfo> componentsIn(const std::vector<StmtPtr>& ast) {
    std::vector<ComponentInfo> found;
    for (const auto& statement : ast) {
        if (!statement || statement->kind != StmtKind::FuncDecl) continue;
        const auto* fn = static_cast<const FuncDeclStmt*>(statement.get());
        if (!fn->isComp) continue;
        ComponentInfo info;
        info.name = fn->name;
        for (const auto& p : fn->params) {
            ComponentParam param;
            param.name = p.name;
            param.kind = pageParamKind(p.type);
            param.required = !p.defaultValue;
            info.params.push_back(std::move(param));
        }
        found.push_back(std::move(info));
    }
    return found;
}

std::vector<ComponentInfo> componentsInSource(const std::string& path, const std::string& source) {
    std::string text = source, problem;
    sourceToUtf8(text, problem);
    Lexer lexer(text, isComponentFile(path));
    Parser parser(lexer.scanTokens());
    parser.setComponentFile(isComponentFile(path));
    return componentsIn(parser.parseProgram());
}

// Source text of a default value, when it's that simple
static std::string defaultText(const Expr* e) {
    if (!e) return "";
    if (e->kind == ExprKind::Literal) {
        const Token& t = static_cast<const LiteralExpr*>(e)->token;
        if (t.type != TokenType::STRING) return t.lexeme;
        std::string out = "\"";
        for (char c : t.lexeme) {
            if (c == '"' || c == '\\') { out += '\\'; out += c; }
            else if (c == '\n') out += "\\n";
            else if (c == '\t') out += "\\t";
            else if (c == '\r') out += "\\r";
            else out += c;
        }
        return out + "\"";
    }
    if (e->kind == ExprKind::Identifier) return static_cast<const IdentifierExpr*>(e)->name.lexeme;
    if (e->kind == ExprKind::Unary) {
        const auto* u = static_cast<const UnaryExpr*>(e);
        const std::string inner = defaultText(u->operand.get());
        return inner.empty() ? "" : u->op.lexeme + inner;
    }
    return "";
}

// For each component a page can use: find its tags and put it there.
//   _xds._mount("dessa-card", (el[_xds.Element]) := { _xds._place(el, Card(title = _xds._attrStr(el, "title", "") )); });
std::string componentMounts(const std::vector<StmtPtr>& ast) {
    std::string code;
    for (const auto& statement : ast) {
        if (!statement || statement->kind != StmtKind::FuncDecl) continue;
        const auto* fn = static_cast<const FuncDeclStmt*>(statement.get());
        if (!fn->isComp) continue;
        std::string args;
        bool usable = true;
        for (const auto& p : fn->params) {
            const std::string kind = pageParamKind(p.type);
            if (kind.empty()) {
                if (!p.defaultValue) usable = false;
                continue;   // its default
            }
            std::string value;
            const std::string quoted = "\"" + p.name + "\"";
            if (kind == "Children") {
                value = "_xds._nestedIn(el)";
            } else {
                std::string fallback = p.defaultValue ? defaultText(p.defaultValue.get()) : (kind == "Str" ? "\"\"" : kind == "Bul" ? "false" : "0");
                if (fallback.empty()) {
                    if (p.defaultValue) continue;   // not simple enough to write again: always the default
                    usable = false;
                    continue;
                }
                value = "_xds._attr" + kind + "(el, " + quoted + ", " + fallback + ")";
            }
            if (!args.empty()) args += ", ";
            args += p.name + " = " + value;
        }
        if (!usable) continue;
        std::string tag = fn->name;
        for (auto& c : tag) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        code += "_xds._mount(\"dessa-" + tag + "\", (el[_xds.Element]) := { _xds._place(el, " + fn->name + "(" + args + ")); });\n";
    }
    return code;
}

ModuleLoader::ModuleLoader(std::vector<Diagnostic>& diagnostics) : diagnostics(diagnostics) {}

void ModuleLoader::addError(const std::string& file, int line, int column, const std::string& message) {
    diagnostics.push_back({DiagnosticSeverity::Error, message, file, line, column, "", 0});
}

Program ModuleLoader::load(const std::string& entryPath) {
    return load(entryPath, nullptr);
}

Program ModuleLoader::load(const std::string& entryPath, const std::string* sourceOverride) {
    Program program;
    std::string entry = normalise(entryPath);
    projectRoot = fs::path(entry).parent_path().string();
    entryFile = entry;
    State state;
    int id = visit(program, entry, sourceOverride, state, "", 0, 0);
    if (id >= 0) program.modules.back()->isEntry = true;
    return program;
}

// include/prelude/errors.ds, when the compiler has its include folder
std::string ModuleLoader::preludePath() const {
    if (includeDir.empty()) return "";
    fs::path file = fs::path(includeDir) / "prelude" / "errors.ds";
    std::error_code ec;
    return fs::is_regular_file(file, ec) ? normalise(file) : "";
}

// All Dessa file types, in order of preference. (They are compiled the same
// way for now.)
static const char* const extensions[] = {".ds", ".dm", ".dx", ".dz", ".dsx", ".dxx", ".dzx", ".dds", ".dss", ".xds", ".xdz", ".xdx"};

// Is this file part of a library (include/, riv_modules/, DESSA_PACKAGES)?
bool ModuleLoader::inLibrary(const std::string& file) const {
    const std::string f = fs::path(normalise(file)).generic_string();
    if (!includeDir.empty() && f.rfind(fs::path(includeDir).generic_string(), 0) == 0) return true;
    if (f.find("/riv_modules/") != std::string::npos) return true;
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
            if (!entry.empty() && f.rfind(fs::path(normalise(fs::path(entry))).generic_string(), 0) == 0) return true;
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }
    return false;
}

// A file of the project: next to the importing file, then in the project root
// (libraries only look next to their own files)
std::string ModuleLoader::resolveLocal(const std::string& importingFile, const std::string& dotted) const {
    std::string relative = dotted;
    std::replace(relative.begin(), relative.end(), '.', '/');
    std::vector<fs::path> folders = {fs::path(importingFile).parent_path()};
    if (!inLibrary(importingFile)) folders.push_back(fs::path(projectRoot));
    for (const auto& folder : folders) {
        for (const char* extension : extensions) {
            fs::path candidate = folder / (relative + extension);
            std::error_code ec;
            if (fs::is_regular_file(candidate, ec)) return normalise(candidate);
        }
    }
    return "";
}

// A library: built in (math, os, ...), or a package with native code or a
// .dm file (sql, json). Packages of plain Dessa files (colors) are modules.
bool ModuleLoader::isLibrary(const std::string& dotted) const {
    if (findBuiltinLibrary(dotted)) return true;
    fs::path folder = libraryFolder(dotted);
    if (folder.empty()) return false;
    std::error_code ec;
    if (!includeDir.empty() && folder.string().rfind(fs::path(includeDir).string(), 0) == 0) return true;
    const std::string last = dotted.substr(dotted.rfind('.') + 1);
    return fs::is_regular_file(folder / (last + ".native"), ec) || fs::is_regular_file(folder / (last + ".dm"), ec);
}

std::string ModuleLoader::resolve(const std::string& importingFile, const std::string& dotted) const {
    if (std::string local = resolveLocal(importingFile, dotted); !local.empty()) return local;

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

// The folder of library `dotted` ($("sql"), $("ui.widgets")), first found of:
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

// Where a file stops being valid UTF-8, as its line number; 0 when it's all valid
static int invalidUtf8Line(const std::string& text) {
    int line = 1;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '\n') ++line;
        const int extra = c < 0x80 ? 0 : (c >= 0xC2 && c <= 0xDF) ? 1 : (c >= 0xE0 && c <= 0xEF) ? 2 : (c >= 0xF0 && c <= 0xF4) ? 3 : -1;
        if (extra < 0 || i + static_cast<size_t>(extra) >= text.size() + (extra == 0 ? 1 : 0)) return line;
        for (int k = 1; k <= extra; ++k) {
            const unsigned char more = static_cast<unsigned char>(text[i + static_cast<size_t>(k)]);
            unsigned char low = 0x80, high = 0xBF;
            if (k == 1 && c == 0xE0) low = 0xA0;      // too long a form
            if (k == 1 && c == 0xED) high = 0x9F;     // surrogates aren't characters
            if (k == 1 && c == 0xF0) low = 0x90;      // too long a form
            if (k == 1 && c == 0xF4) high = 0x8F;     // past the last character
            if (more < low || more > high) return line;
        }
        i += static_cast<size_t>(extra) + 1;
    }
    return 0;
}

// The `booleans` setting of the nearest dessa.toml above a file: "both" without one
std::string ModuleLoader::booleanStyle(const std::string& file) {
    std::error_code ec;
    for (fs::path folder = fs::path(file).parent_path(); !folder.empty(); folder = folder.parent_path()) {
        const fs::path settings = folder / "dessa.toml";
        if (fs::is_regular_file(settings, ec)) {
            std::ifstream in(settings);
            std::string line;
            std::string style = "both";
            int number = 0;
            while (std::getline(in, line)) {
                ++number;
                const size_t hash = line.find('#');
                if (hash != std::string::npos) line.resize(hash);
                const size_t equal = line.find('=');
                if (equal == std::string::npos) continue;
                auto trim = [](std::string text) {
                    const char* blank = " \t\r\"";
                    const size_t first = text.find_first_not_of(blank);
                    if (first == std::string::npos) return std::string();
                    return text.substr(first, text.find_last_not_of(blank) - first + 1);
                };
                if (trim(line.substr(0, equal)) != "booleans") continue;
                const std::string value = trim(line.substr(equal + 1));
                if (value == "both" || value == "true-false" || value == "yes-no") {
                    style = value;
                } else {
                    addError(displayPath(settings.generic_string()), number, 1, "booleans is \"both\", \"true-false\" or \"yes-no\", not \"" + value + "\"");
                }
            }
            return style;
        }
        if (folder == folder.parent_path()) break;
    }
    return "both";
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
    module->isPrelude = path == preludePath();
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

    // Saved as UTF-16 or UTF-32, or with a BOM? From here on it's UTF-8.
    if (std::string problem; !sourceToUtf8(module->source, problem)) {
        addError(module->displayPath, 1, 1, "This file can't be read: " + problem + ".");
        return -1;
    }
    // Everything after this may rely on valid text (Str, file names from imports, ...)
    if (const int line = invalidUtf8Line(module->source)) {
        addError(module->displayPath, line, 1, "This file isn't valid UTF-8 text here. Save it as UTF-8.");
        return -1;
    }

    const bool componentFile = isComponentFile(path);
    Lexer lexer(module->source, componentFile);
    std::vector<Token> tokens = lexer.scanTokens();
    for (const auto& e : lexer.getErrors()) diagnostics.push_back(diagnosticFromMessage(module->displayPath, e));

    // dessa.toml's `booleans` is about the project's own files: a library keeps its own style
    if (!module->isPrelude && !inLibrary(path)) {
        const std::string style = booleanStyle(path);
        if (style != "both") {
            const std::string yes = style == "yes-no" ? "yes" : "true", no = style == "yes-no" ? "no" : "false";
            for (const auto& token : tokens) {
                if (token.type != TokenType::TRUE && token.type != TokenType::FALSE) continue;
                const std::string& wanted = token.type == TokenType::TRUE ? yes : no;
                if (token.lexeme != wanted) {
                    addError(module->displayPath, token.line, token.column,
                             "This project uses " + yes + " and " + no + " (booleans in dessa.toml): write " + wanted + " instead of " + token.lexeme);
                }
            }
        }
    }

    // async, await, Task:T: the library is there without asking
    bool usesAsync = false;
    const bool isAsyncLibrary = !includeDir.empty() && fs::path(path).parent_path() == fs::path(normalise(fs::path(includeDir) / "async"));
    const bool isPageLibrary = !includeDir.empty() && fs::path(path).parent_path() == fs::path(normalise(fs::path(includeDir) / "webutils"));
    if (!isAsyncLibrary && !isPageLibrary && path != preludePath()) {
        for (size_t t = 0; t + 1 < tokens.size(); ++t) {
            if (tokens[t].type != TokenType::IDENTIFIER) continue;
            const std::string& word = tokens[t].lexeme;
            const TokenType next = tokens[t + 1].type;
            if ((word == "async" && (next == TokenType::FUNC || next == TokenType::IDENTIFIER)) || (word == "Task" && next == TokenType::COLON) ||
                (word == "await" && next != TokenType::EQUAL)) {
                usesAsync = true;
                break;
            }
        }
    }
    if (usesAsync) state.usesAsync = true;

    // In a component file, what its markup turns into needs these libraries.
    // They come under hidden names, so the file's own names are never in the way.
    bool usesPage = false, usesXml = false;
    if (componentFile) {
        bool hasMarkup = false, namesXml = false, hasComp = false;
        for (const auto& token : tokens) {
            if (token.type == TokenType::MARKUP) hasMarkup = true;
            if (token.type == TokenType::IDENTIFIER && token.lexeme == "Xml") namesXml = true;
            if (token.type == TokenType::IDENTIFIER && (token.lexeme == "comp" || token.lexeme == "Children" || token.lexeme == "Element")) hasComp = true;
        }
        usesXml = namesXml;
        usesPage = hasComp || (hasMarkup && !namesXml);
    }

    if (lexer.stopped()) tokens.erase(tokens.begin(), tokens.end() - 1);
    Parser parser(std::move(tokens));
    parser.setComponentFile(componentFile);
    module->ast = parser.parseProgram();
    module->templates = parser.takeTemplates();
    for (const auto& e : parser.getErrors()) diagnostics.push_back(diagnosticFromMessage(module->displayPath, e));

    // Every file sees Error, IndexError, ... as if it had imported them
    const std::string prelude = preludePath();
    if (!prelude.empty() && prelude != path) {
        ModuleSpec spec;
        spec.path = "errors";
        spec.isPrelude = true;
        std::vector<ModuleSpec> specs;
        specs.push_back(std::move(spec));
        module->ast.insert(module->ast.begin(), std::make_unique<ImportStmt>(std::vector<ImportName>{}, std::move(specs), true));
    }
    // A page's inline scripts see webutils as if they had imported it. An
    // external <script src="app.ds"> is a file of its own and imports it itself.
    const std::string extension = fs::path(path).extension().string();
    if (extension == ".dhtml" || extension == ".html" || extension == ".htm" || usesPage) {
        ModuleSpec spec;
        spec.path = "webutils";
        spec.isBuiltin = true;
        spec.isPageLibrary = true;
        std::vector<ModuleSpec> specs;
        specs.push_back(std::move(spec));
        module->ast.insert(module->ast.begin(), std::make_unique<ImportStmt>(std::vector<ImportName>{}, std::move(specs), true));
    }
    const auto hiddenImport = [&](const char* library, const char* alias) {
        ModuleSpec spec;
        spec.path = library;
        spec.alias = alias;
        spec.isBuiltin = true;
        spec.isHidden = true;
        std::vector<ModuleSpec> specs;
        specs.push_back(std::move(spec));
        module->ast.insert(module->ast.begin(), std::make_unique<ImportStmt>(std::vector<ImportName>{}, std::move(specs), true));
    };
    if (usesPage) hiddenImport("webutils", "_xds");
    const auto autoImport = [&](const char* library) {
        ModuleSpec spec;
        spec.path = library;
        spec.isBuiltin = true;
        spec.isPageLibrary = true;
        std::vector<ModuleSpec> specs;
        specs.push_back(std::move(spec));
        module->ast.insert(module->ast.begin(), std::make_unique<ImportStmt>(std::vector<ImportName>{}, std::move(specs), true));
    };
    if (usesAsync) {
        hiddenImport("async", "_xdsasync");
        autoImport("async");
    }
    if (usesXml) hiddenImport("xml", "_xdsxml");
    // The entry file of a page: components are put in the page's tags (see web.cpp)
    if (usesPage && path == entryFile) {
        std::string mounts = componentMounts(module->ast);
        if (!mounts.empty()) {
            Lexer mountLexer(mounts, false);
            std::vector<Token> mountTokens = mountLexer.scanTokens();
            for (auto& t : mountTokens) t.line = t.column = 0;   // at no place in the file
            Parser mountParser(std::move(mountTokens));
            for (auto& statement : mountParser.parseProgram()) module->ast.push_back(std::move(statement));
        }
    }

    state.loadingStack.push_back(path);
    for (const auto& statement : module->ast) {
        if (!statement || statement->kind != StmtKind::Import) continue;
        const auto* import = static_cast<const ImportStmt*>(statement.get());
        for (const auto& spec : import->modules) {
            if (spec.isPrelude) {
                int childId = visit(program, preludePath(), nullptr, state, module->displayPath, 0, 0);
                if (childId >= 0) module->resolvedImports[&spec] = childId;
                continue;
            }
            // A file of the project wins over a library of the same name. (A
            // library's own files import its native part: $("sql") in sql.dm.)
            if (!spec.isBuiltin && isLibrary(spec.path)) {
                // json.ds importing $("json") means the library, too
                const std::string local = resolveLocal(path, spec.path);
                const fs::path folder = libraryFolder(spec.path);
                if (local.empty() || local == path || (!folder.empty() && fs::path(local).parent_path() == folder)) {
                    spec.isBuiltin = true;
                } else {
                    diagnostics.push_back({DiagnosticSeverity::Warning,
                                           "'" + spec.path + "' is your file " + displayPath(local) + ", which hides the " + spec.path +
                                               " library (rename the file to use the library)",
                                           module->displayPath, spec.line, spec.column, "", 0});
                }
            }
            if (spec.isBuiltin) {
                // $("lib"): include/<lib>/ or an installed riv package. Its
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
                        addError(module->displayPath, spec.line, spec.column, "Library '" + spec.path + "': " + error);
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
                    // webutils: the page bridge does its `{ ... }` functions
                    if (!isPackage && last == "webutils") program.modules[static_cast<size_t>(childId)]->browserLibrary = true;
                }
                continue;
            }
            std::string target = resolve(path, spec.path);
            if (target.empty()) {
                std::string rel = spec.path;
                std::replace(rel.begin(), rel.end(), '.', '/');
                addError(module->displayPath, spec.line, spec.column,
                         "Cannot find module or library '" + spec.path + "' (looked for " + rel + ".ds / .dm / .dx ... next to this file and in the project root, then for a library; for a riv package: riv add " + spec.path + ")" +
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

    // The file being run, if anything uses async: what is still waiting goes on
    // after its last line. In a page, the browser's timers do that.
    if (path == entryFile && state.usesAsync && fs::path(path).extension() != ".dm") {
        bool hasPage = false;
        for (const auto& m : program.modules) if (m->browserLibrary) hasPage = true;
        const auto hiddenLibrary = [&](const char* library, const char* alias) {
            auto import = std::make_unique<ImportStmt>(std::vector<ImportName>{}, std::vector<ModuleSpec>{}, true);
            ModuleSpec spec;
            spec.path = library;
            spec.alias = alias;
            spec.isBuiltin = true;
            spec.isHidden = true;
            import->modules.push_back(std::move(spec));
            const fs::path dm = libraryFolder(library) / (std::string(library) + ".dm");
            const int childId = visit(program, normalise(dm), nullptr, state, module->displayPath, 0, 0);
            if (childId >= 0) module->resolvedImports[&import->modules[0]] = childId;
            module->ast.insert(module->ast.begin(), std::move(import));
        };
        if (!usesAsync) hiddenLibrary("async", "_xdsasync");
        if (hasPage && !usesPage) hiddenLibrary("webutils", "_xds");
        const std::string code = hasPage ? "_xdsasync._useHost((ms[Int], f[Func[Emp]]) := { _xds.setTimeout(f, ms); });\n" : "_xdsasync._runLoop();\n";
        Lexer hookLexer(code, false);
        std::vector<Token> hookTokens = hookLexer.scanTokens();
        for (auto& t : hookTokens) t.line = t.column = 0;
        Parser hookParser(std::move(hookTokens));
        auto hook = hookParser.parseProgram();
        if (hasPage) {
            size_t after = 0;
            while (after < module->ast.size() && module->ast[after] && module->ast[after]->kind == StmtKind::Import) ++after;
            for (auto& s : hook) module->ast.insert(module->ast.begin() + static_cast<std::ptrdiff_t>(after++), std::move(s));
        } else {
            for (auto& s : hook) module->ast.push_back(std::move(s));
        }
    }

    module->id = static_cast<int>(program.modules.size());
    program.modules.push_back(std::move(module));
    return program.modules.back()->id;
}

} // namespace dessa
