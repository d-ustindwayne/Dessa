// dessa -- the Dessa compiler driver
//
//   dessa check   <file.ds> [--json]        type-check, print diagnostics
//   dessa emit    <file.ds> [-o out.cpp]    print / write the generated C++
//   dessa build   <file.ds> [-o out.exe]    compile to a native executable
//   dessa run     <file.ds> [-- args...]    build (cached) and run
//   dessa symbols <file.ds>                 JSON symbol table for editors
//   dessa complete|hover <file.ds> --at L:C completion / hover info for editors
//   dessa tokens  <file.ds>                 debug: token stream
//   dessa ast     <file.ds>                 debug: top-level statements
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#ifndef _WIN32
#include <sys/wait.h>
#endif
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "analyser.hpp"
#include "builtins.hpp"
#include "codegen.hpp"
#include "jsgen.hpp"
#include "web.hpp"
#include "json.hpp"
#include "lexer.hpp"
#include "loader.hpp"
#include "parser.hpp"
#include "toolchain.hpp"

using namespace dessa;
namespace fs = std::filesystem;

namespace {

constexpr const char* kVersion = "0.1.0";
const char* g_argv0 = nullptr; // to find runtime/ and include/ next to the compiler

void printUsage() {
    std::cerr <<
        "Dessa " << kVersion << "\n"
        "Usage:\n"
        "  dessa check   <file.ds> [--json] [--stdin]   type-check and report problems\n"
        "  dessa emit    <file.ds> [-o out.cpp]         show the generated C++\n"
        "  dessa build   <file.ds> [-o program]         compile to an executable\n"
        "  dessa run     <file.ds> [-- args...]         compile and run\n"
        "  dessa symbols <file.ds> [--stdin]            symbol table as JSON (for editors)\n"
        "  dessa serve [folder] [--port N]              a web server for pages (.dhtml compiled on each request)\n"
        "  dessa complete <file.ds> --at L:C [--stdin]  completions at a position as JSON (for editors)\n"
        "  dessa hover <file.ds> --at L:C [--stdin]     the symbol at a position as JSON (for editors)\n"
        "  dessa tokens  <file.ds>                      debug: print tokens\n"
        "  dessa ast     <file.ds>                      debug: print top-level statements\n"
        "\n"
        "Environment: DESSA_CXX (C++ compiler, default g++), DESSA_HOME (folder containing runtime/)\n";
}

std::string severityName(DiagnosticSeverity s) {
    switch (s) {
        case DiagnosticSeverity::Error: return "error";
        case DiagnosticSeverity::Warning: return "warning";
        case DiagnosticSeverity::Note: return "note";
    }
    return "error";
}

void printDiagnostics(const std::vector<Diagnostic>& diagnostics, bool asJson, std::ostream& out) {
    if (asJson) {
        out << "[";
        for (size_t i = 0; i < diagnostics.size(); ++i) {
            const auto& d = diagnostics[i];
            out << (i ? "," : "") << "\n  {\"file\":" << json::quote(d.file)
                << ",\"line\":" << d.line << ",\"column\":" << d.column
                << ",\"severity\":" << json::quote(severityName(d.severity))
                << ",\"message\":" << json::quote(d.message) << "}";
        }
        out << (diagnostics.empty() ? "]\n" : "\n]\n");
        return;
    }
    for (const auto& d : diagnostics) {
        out << d.file;
        if (d.line > 0) out << ":" << d.line << ":" << d.column;
        out << ": " << severityName(d.severity) << ": " << d.message << "\n";
    }
}

bool hasError(const std::vector<Diagnostic>& diagnostics) {
    for (const auto& d : diagnostics) if (d.severity == DiagnosticSeverity::Error) return true;
    return false;
}

std::string readStdin() {
    std::stringstream buffer;
    buffer << std::cin.rdbuf();
    return buffer.str();
}

struct Compilation {
    std::vector<Diagnostic> diagnostics;
    Program program;
    Analyser analyser;
    bool ok = false;
};

// Load + parse + analyse. Analysis runs even after parse errors so editors
// get as many diagnostics as possible.
void compileFrontEnd(Compilation& c, const std::string& path, const std::string* sourceOverride,
                     const std::function<void(Compilation&)>& beforeAnalyse = nullptr) {
    ModuleLoader loader(c.diagnostics);
    loader.setIncludeDir(toolchain::includeDir(g_argv0));
    c.program = loader.load(path, sourceOverride);
    if (c.program.modules.empty()) return;
    bool loadErrors = hasError(c.diagnostics);
    if (beforeAnalyse) beforeAnalyse(c);
    c.analyser.analyse(c.program);
    for (const auto& d : c.analyser.getDiagnostics()) c.diagnostics.push_back(d);
    c.ok = !loadErrors && !hasError(c.diagnostics);
}

int cmdTokens(const std::string& path) {
    std::ifstream file(path);
    if (!file) { std::cerr << "Could not open file: " << path << "\n"; return 1; }
    std::stringstream buffer;
    buffer << file.rdbuf();
    Lexer lexer(buffer.str());
    for (const auto& t : lexer.scanTokens()) {
        if (t.type == TokenType::END_OF_FILE) continue;
        std::cout << t.line << ":" << t.column << "\t" << static_cast<int>(t.type) << "\t" << t.lexeme << "\n";
    }
    for (const auto& e : lexer.getErrors()) std::cout << e << "\n";
    return lexer.hasErrors() ? 1 : 0;
}

const char* stmtName(StmtKind kind) {
    switch (kind) {
        case StmtKind::Expression: return "Expression";
        case StmtKind::VarDecl: return "VarDecl";
        case StmtKind::FuncDecl: return "FuncDecl";
        case StmtKind::ClassDecl: return "ClassDecl";
        case StmtKind::EnumDecl: return "EnumDecl";
        case StmtKind::InterfaceDecl: return "InterfaceDecl";
        case StmtKind::Import: return "Import";
        case StmtKind::If: return "If";
        case StmtKind::Switch: return "Switch";
        case StmtKind::For: return "For";
        case StmtKind::ForIn: return "ForIn";
        case StmtKind::While: return "While";
        case StmtKind::Loop: return "Loop";
        case StmtKind::Break: return "Break";
        case StmtKind::Continue: return "Continue";
        case StmtKind::Return: return "Return";
        case StmtKind::Block: return "Block";
        case StmtKind::Try: return "Try";
        case StmtKind::Throw: return "Throw";
    }
    return "Unknown";
}

int cmdAst(const std::string& path) {
    std::vector<Diagnostic> diagnostics;
    ModuleLoader loader(diagnostics);
    loader.setIncludeDir(toolchain::includeDir(g_argv0));
    Program program = loader.load(path);
    for (const auto& module : program.modules) {
        std::cout << "== " << module->displayPath << " (" << module->ast.size() << " statements)\n";
        for (const auto& s : module->ast) {
            if (s) std::cout << "  " << s->line << ":" << s->column << "  " << stmtName(s->kind) << "\n";
        }
    }
    printDiagnostics(diagnostics, false, std::cerr);
    return hasError(diagnostics) ? 1 : 0;
}

int cmdCheck(const std::string& path, bool asJson, bool fromStdin) {
    std::string source;
    if (fromStdin) source = readStdin();
    std::vector<Diagnostic> diagnostics;
    if (web::isPage(path)) {
        // A page: each of its Dessa scripts is a program of its own
        if (!fromStdin) {
            std::ifstream in(path, std::ios::binary);
            std::stringstream buffer;
            buffer << in.rdbuf();
            source = buffer.str();
        }
        for (const auto& script : web::scripts(path, source)) {
            if (!script.dessa) continue;
            Compilation c;
            if (script.hasSrc) {
                compileFrontEnd(c, (fs::path(path).parent_path() / script.src).string(), nullptr);
            } else {
                std::string only = web::dessaOnly(path, source, script.tagEnd + 1);
                compileFrontEnd(c, path, &only);
            }
            diagnostics.insert(diagnostics.end(), c.diagnostics.begin(), c.diagnostics.end());
        }
    } else {
        Compilation c;
        compileFrontEnd(c, path, fromStdin ? &source : nullptr);
        diagnostics = c.diagnostics;
    }
    printDiagnostics(diagnostics, asJson, std::cout);
    if (!asJson && !hasError(diagnostics)) {
        size_t warnings = diagnostics.size();
        std::cout << "No errors" << (warnings ? " (" + std::to_string(warnings) + " warning(s))" : std::string()) << ".\n";
    }
    return hasError(diagnostics) ? 1 : 0;
}

// Generated C++ for the program, or "" (with diagnostics printed) on failure.
// --target native (C++, the default) or js / web (JavaScript for Node or a browser)
std::string g_target = "native";
int g_port = 8080;   // dessa serve / dessa run page.dhtml
bool targetIsJs() { return g_target == "js" || g_target == "web"; }

std::string readTextFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// Generated C++ (or JavaScript with --target js) for the program, or "" (with
// diagnostics printed) on failure.
// JavaScript for an analysed program, or "" with the reason in `errors`.
std::string javascriptFor(Compilation& c, std::string& errors) {
    // Libraries with a C++ part can't run in JavaScript (except the core ones, which have JS twins)
    static const std::set<std::string> withJs = {"math", "os", "sys", "time"};
    for (const auto& lib : c.analyser.builtinLibrariesUsed()) {
        if (!withJs.count(lib)) {
            errors += "dessa: the library <" + lib + "> has a C++ part, so it can't run in JavaScript or a browser "
                      "(only libraries written in Dessa, or with a .js part, can)\n";
            return "";
        }
    }
    std::string runtime = toolchain::runtimeDir(g_argv0);
    std::string runtimeJs = runtime.empty() ? "" : readTextFile(fs::path(runtime) / "dessa_runtime.js");
    if (runtimeJs.empty()) {
        errors += "dessa: cannot find runtime/dessa_runtime.js (set DESSA_HOME to the Dessa install folder)\n";
        return "";
    }
    JsCodegen js(c.program, c.analyser);
    return js.generate(runtimeJs);
}

// For pages (.dhtml): compiles one script's source, as if it were `path`
bool compileScriptToJs(const std::string& path, const std::string& source, std::string& js, std::string& errors) {
    Compilation c;
    compileFrontEnd(c, path, &source);
    std::ostringstream text;
    printDiagnostics(c.diagnostics, false, text);
    if (!c.ok) {
        errors += text.str();
        return false;
    }
    js = javascriptFor(c, errors);
    return !js.empty();
}

std::string generate(Compilation& c, const std::string& path) {
    compileFrontEnd(c, path, nullptr);
    printDiagnostics(c.diagnostics, false, std::cerr);
    if (!c.ok) return "";
    if (targetIsJs()) {
        std::string errors;
        std::string js = javascriptFor(c, errors);
        std::cerr << errors;
        return js;
    }
    for (const auto& module : c.program.modules) {
        if (!module->jsNative.empty()) {
            std::cerr << "dessa: " << module->displayPath << " belongs to a library that only runs in a browser / JavaScript;\n"
                      << "       build with --target web, e.g. dessa build " << path << " --target web\n";
            return "";
        }
    }
    Codegen codegen(c.program, c.analyser);
    return codegen.generate();
}

int cmdEmit(const std::string& path, const std::string& output) {
    Compilation c;
    std::string cpp = generate(c, path);
    if (cpp.empty()) return 1;
    if (output.empty()) {
        std::cout << cpp;
    } else {
        std::ofstream out(output, std::ios::binary);
        out << cpp;
    }
    return 0;
}

// page.dhtml -> page.html with its Dessa scripts compiled (runs without dessa.js)
int buildPageCommand(const std::string& path, std::string output) {
    std::string html, errors;
    if (!web::buildPage(path, compileScriptToJs, html, errors)) {
        std::cerr << errors;
        return 1;
    }
    if (output.empty()) {
        fs::path p(path);
        output = (p.parent_path() / (p.stem().string() + (web::isDessaPage(path) ? ".html" : ".built.html"))).string();
    }
    std::ofstream out(output, std::ios::binary);
    out << html;
    if (!out) {
        std::cerr << "dessa: could not write " << output << "\n";
        return 1;
    }
    std::cout << "Wrote " << output << "\n";
    return 0;
}

int cmdBuild(const std::string& path, std::string output, const char* argv0) {
    if (web::isPage(path)) return buildPageCommand(path, output);
    Compilation c;
    std::string cpp = generate(c, path);
    if (cpp.empty()) return 1;
    if (targetIsJs()) {
        // One self-contained .js file (runtime included)
        if (output.empty()) output = fs::path(path).stem().string() + ".js";
        std::ofstream out(output, std::ios::binary);
        out << cpp;
        return out ? 0 : 1;
    }
    if (output.empty()) {
        output = fs::path(path).stem().string();
#ifdef _WIN32
        output += ".exe";
#endif
    }
    std::string error;
    std::vector<std::string> libraries(c.analyser.builtinLibrariesUsed().begin(), c.analyser.builtinLibrariesUsed().end());
    if (!toolchain::compile(cpp, output, argv0, error, false, libraries)) {
        std::cerr << error;
        return 1;
    }
    return 0;
}

int cmdRun(const std::string& path, const std::vector<std::string>& programArgs, const char* argv0) {
    if (web::isPage(path)) {
        // A page: serve its folder and open it in the browser
        fs::path p = fs::absolute(path);
        return web::serve(p.parent_path().string(), g_port, p.filename().string(), compileScriptToJs);
    }
    Compilation c;
    std::string cpp = generate(c, path);
    if (cpp.empty()) return 1;
    if (targetIsJs()) {
        // Run the JavaScript with Node
        std::error_code ec;
        fs::path dir = fs::temp_directory_path(ec) / "dessa" / "js";
        fs::create_directories(dir, ec);
        fs::path file = dir / (fs::path(path).stem().string() + ".js");
        {
            std::ofstream out(file, std::ios::binary);
            out << cpp;
        }
        std::string command = "node \"" + file.string() + "\"";
        for (const auto& a : programArgs) command += " \"" + a + "\"";
        int code = std::system(command.c_str());
#ifndef _WIN32
        if (code != -1 && WIFEXITED(code)) code = WEXITSTATUS(code);
#endif
        return code;
    }
    std::string exe;
    std::string error;
    std::vector<std::string> libraries(c.analyser.builtinLibrariesUsed().begin(), c.analyser.builtinLibrariesUsed().end());
    if (!toolchain::compileCached(cpp, argv0, exe, error, libraries)) {
        std::cerr << error;
        return 1;
    }
    return toolchain::run(exe, programArgs);
}

std::string typeString(const SemanticType& t) { return t.toString(); }

std::string symbolKindName(SymbolKind k) {
    switch (k) {
        case SymbolKind::Variable: return "variable";
        case SymbolKind::Constant: return "constant";
        case SymbolKind::Function: return "function";
        case SymbolKind::Class: return "class";
        case SymbolKind::Interface: return "interface";
        case SymbolKind::Enum: return "enum";
        case SymbolKind::Parameter: return "parameter";
        case SymbolKind::Field: return "field";
        case SymbolKind::Method: return "method";
        case SymbolKind::Builtin: return "builtin";
    }
    return "variable";
}

// A parameter's default value as source text, for signatures: "2", "\"John\"", "Color.RED"
std::string defaultText(const Expr* value) {
    if (!value) return "";
    switch (value->kind) {
        case ExprKind::Literal: {
            const Token& t = static_cast<const LiteralExpr*>(value)->token;
            if (t.type == TokenType::STRING) return "\"" + t.lexeme + "\"";
            if (t.type == TokenType::CHAR_LITERAL) return "'" + t.lexeme + "'";
            return t.lexeme;
        }
        case ExprKind::Identifier:
            return static_cast<const IdentifierExpr*>(value)->name.lexeme;
        case ExprKind::Member: {
            const auto* m = static_cast<const MemberExpr*>(value);
            std::string object = defaultText(m->object.get());
            return object == "..." ? object : object + "." + m->name.lexeme;
        }
        case ExprKind::Unary: {
            const auto* u = static_cast<const UnaryExpr*>(value);
            std::string operand = defaultText(u->operand.get());
            return operand == "..." ? operand : u->op.lexeme + operand;
        }
        default:
            return "...";
    }
}

void writeSymbol(std::ostream& out, const std::string& name, const Symbol& s, const std::string& file = "") {
    out << "{\"name\":" << json::quote(name) << ",\"kind\":" << json::quote(symbolKindName(s.kind))
        << ",\"type\":" << json::quote(typeString(s.type)) << ",\"line\":" << s.line;
    if (!file.empty()) out << ",\"file\":" << json::quote(file);
    if (s.kind == SymbolKind::Builtin) {
        // dessa::math::sqrt -> "math" (runtime/decl/math.ds); dessa::print -> "builtins"
        std::string library = "builtins";
        const std::string prefix = "dessa::";
        if (s.cppName.rfind(prefix, 0) == 0) {
            size_t sep = s.cppName.find("::", prefix.size());
            if (sep != std::string::npos) library = s.cppName.substr(prefix.size(), sep - prefix.size());
        }
        out << ",\"library\":" << json::quote(library);
    }
    if (!s.parameters.empty() || s.kind == SymbolKind::Function || s.kind == SymbolKind::Method ||
        (s.kind == SymbolKind::Builtin && s.type.kind == SemanticTypeKind::Function)) {
        out << ",\"params\":[";
        for (size_t i = 0; i < s.parameters.size(); ++i) {
            const auto& p = s.parameters[i];
            std::string ptype = p.type.isUnknown() && !p.type.name.empty() ? p.type.name : p.type.toString();
            out << (i ? "," : "") << "{\"name\":" << json::quote(p.name) << ",\"type\":" << json::quote(ptype);
            if (p.defaultValue) out << ",\"default\":" << json::quote(defaultText(p.defaultValue));
            if (p.variadic) out << ",\"variadic\":true";
            out << "}";
        }
        std::string ret = s.returnType.isUnknown() && !s.returnType.name.empty() ? s.returnType.name : s.returnType.toString();
        out << "],\"returns\":" << json::quote(ret);
    }
    out << "}";
}

int cmdSymbols(const std::string& path, bool fromStdin, const char* argv0) {
    Compilation c;
    std::string source;
    if (fromStdin) source = readStdin();
    if (web::isPage(path)) {
        if (!fromStdin) source = readTextFile(path);
        source = web::dessaOnly(path, source);
        fromStdin = true;
    }
    compileFrontEnd(c, path, fromStdin ? &source : nullptr);
    std::ostream& out = std::cout;
    // Folder with the builtin declaration files (runtime/decl/*.ds)
    std::string runtime = toolchain::runtimeDir(argv0);
    out << "{\n\"declarations\":" << json::quote(runtime.empty() ? "" : (fs::path(runtime) / "decl").string()) << ",\n";
    // Folder with the builtin libraries (include/<lib>/<lib>.ds declarations)
    out << "\"includes\":" << json::quote(toolchain::includeDir(argv0)) << ",\n";
    out << "\"globals\":[";
    bool first = true;
    if (!c.program.modules.empty()) {
        const auto* scope = c.analyser.moduleScope(c.program.entry().id);
        if (scope) {
            for (const Scope* s = scope; s; s = s->parent()) {
                for (const auto& [name, symbol] : s->all()) {
                    out << (first ? "\n  " : ",\n  ");
                    // Declared in another file (an import): say where, for go-to-definition
                    std::string file;
                    if (symbol.kind != SymbolKind::Builtin && symbol.moduleId >= 0 &&
                        static_cast<size_t>(symbol.moduleId) < c.program.modules.size() &&
                        symbol.moduleId != c.program.entry().id) {
                        file = c.program.modules[static_cast<size_t>(symbol.moduleId)]->path;
                    }
                    writeSymbol(out, name, symbol, file);
                    first = false;
                }
            }
        }
    }
    out << "],\n\"types\":[";
    first = true;
    // Where a type or member is declared, for go to definition (the edited file itself: "")
    auto fileOf = [&](int moduleId) -> std::string {
        if (moduleId < 0 || static_cast<size_t>(moduleId) >= c.program.modules.size() || moduleId == c.program.entry().id) return "";
        return c.program.modules[static_cast<size_t>(moduleId)]->path;
    };
    auto writeMembers = [&](const std::string& kind, const std::string& name, const std::vector<std::pair<std::string, Symbol>>& members,
                            int moduleId, int line) {
        out << (first ? "\n  " : ",\n  ") << "{\"name\":" << json::quote(name) << ",\"kind\":" << json::quote(kind)
            << ",\"line\":" << line;
        if (!fileOf(moduleId).empty()) out << ",\"file\":" << json::quote(fileOf(moduleId));
        out << ",\"members\":[";
        for (size_t i = 0; i < members.size(); ++i) {
            out << (i ? "," : "");
            const Symbol& m = members[i].second;
            writeSymbol(out, members[i].first, m, m.line > 0 ? fileOf(m.moduleId) : "");
        }
        out << "]}";
        first = false;
    };
    for (const auto& key : c.analyser.classOrder()) {
        const ClassInfo* info = c.analyser.findClass(SemanticType::userDefined(key.substr(key.find(':') + 1), std::stoi(key), UserTypeKind::Class));
        if (!info) continue;
        std::vector<std::pair<std::string, Symbol>> members;
        for (const ClassInfo* k = info; k; k = k->parent ? c.analyser.findClass(*k->parent) : nullptr) {
            for (const auto& f : k->fieldOrder) members.emplace_back(f, k->fields.at(f));
            for (const auto& [n, m] : k->methods) members.emplace_back(n, m);
        }
        writeMembers("class", info->name, members, info->moduleId, info->decl ? info->decl->line : 0);
    }
    for (const auto& key : c.analyser.interfaceOrder()) {
        const InterfaceInfo* info = c.analyser.findInterface(SemanticType::userDefined(key.substr(key.find(':') + 1), std::stoi(key), UserTypeKind::Interface));
        if (!info) continue;
        std::vector<std::pair<std::string, Symbol>> members;
        for (const auto& f : info->fieldOrder) {
            Symbol s; s.kind = SymbolKind::Field; s.name = f; s.type = info->fields.at(f);
            members.emplace_back(f, s);
        }
        for (const auto& m : info->methodOrder) members.emplace_back(m, info->methods.at(m));
        writeMembers("interface", info->name, members, info->moduleId, info->decl ? info->decl->line : 0);
    }
    for (const auto& key : c.analyser.enumOrder()) {
        const EnumInfo* info = c.analyser.findEnum(SemanticType::userDefined(key.substr(key.find(':') + 1), std::stoi(key), UserTypeKind::Enum));
        if (!info) continue;
        std::vector<std::pair<std::string, Symbol>> members;
        for (const auto& v : info->values) {
            Symbol s; s.kind = SymbolKind::Constant; s.name = v; s.type = info->type();
            members.emplace_back(v, s);
        }
        writeMembers("enum", info->name, members, info->moduleId, info->decl ? info->decl->line : 0);
    }
    out << "],\n\"imports\":[";
    first = true;
    if (!c.program.modules.empty()) {
        // Where each $(...) of the edited file points (ctrl+click in the editor)
        const Module& entry = c.program.entry();
        std::vector<std::string> sourceLines;
        {
            std::stringstream text(entry.source);
            std::string l;
            while (std::getline(text, l)) sourceLines.push_back(l);
        }
        for (const auto& statement : entry.ast) {
            if (!statement || statement->kind != StmtKind::Import) continue;
            for (const auto& spec : static_cast<const ImportStmt*>(statement.get())->modules) {
                std::string target;
                auto it = entry.resolvedImports.find(&spec);
                if (it != entry.resolvedImports.end()) {
                    target = c.program.modules[static_cast<size_t>(it->second)]->path;
                } else if (spec.isBuiltin) {
                    // A core library without a Dessa part: its declaration file
                    std::string relative = spec.path;
                    std::replace(relative.begin(), relative.end(), '.', '/');
                    std::string last = spec.path.substr(spec.path.rfind('.') + 1);
                    fs::path decl = fs::path(toolchain::includeDir(argv0)) / relative / (last + ".ds");
                    std::error_code ec;
                    if (fs::is_regular_file(decl, ec)) target = decl.string();
                }
                if (target.empty() || spec.line < 1 || static_cast<size_t>(spec.line) > sourceLines.size()) continue;
                // The whole <name> or "name"
                const std::string& l = sourceLines[static_cast<size_t>(spec.line - 1)];
                size_t start = static_cast<size_t>(spec.column > 0 ? spec.column - 1 : 0);
                size_t end = start;
                if (start < l.size() && (l[start] == '<' || l[start] == '"')) {
                    char close = l[start] == '<' ? '>' : '"';
                    size_t found = l.find(close, start + 1);
                    end = found == std::string::npos ? l.size() : found + 1;
                }
                out << (first ? "\n  " : ",\n  ") << "{\"spec\":" << json::quote(spec.path) << ",\"builtin\":"
                    << (spec.isBuiltin ? "true" : "false") << ",\"line\":" << spec.line << ",\"column\":" << spec.column
                    << ",\"length\":" << (end - start) << ",\"file\":" << json::quote(target) << "}";
                first = false;
            }
        }
    }
    out << "],\n\"libraries\":[";
    first = true;
    // Built into dessa, then the riv packages this program imports
    std::vector<const BuiltinLibrary*> libraries;
    for (const auto& lib : builtinLibraries()) libraries.push_back(&lib);
    for (const BuiltinLibrary* lib : packageLibraries()) libraries.push_back(lib);
    for (const BuiltinLibrary* libPtr : libraries) {
        const BuiltinLibrary& lib = *libPtr;
        out << (first ? "\n  " : ",\n  ") << "{\"name\":" << json::quote(lib.name);
        if (!lib.baseDir.empty()) out << ",\"folder\":" << json::quote(lib.baseDir);
        out << ",\"members\":[";
        bool firstMember = true;
        for (const auto& fn : lib.functions) {
            out << (firstMember ? "" : ",") << "{\"name\":" << json::quote(fn.name) << ",\"kind\":\"function\",\"params\":[";
            for (size_t i = 0; i < fn.params.size(); ++i) {
                out << (i ? "," : "") << "{\"name\":" << json::quote(fn.params[i].name) << ",\"type\":" << json::quote(fn.params[i].type) << "}";
            }
            out << "],\"returns\":" << json::quote(fn.returnType) << "}";
            firstMember = false;
        }
        for (const auto& k : lib.constants) {
            out << (firstMember ? "" : ",") << "{\"name\":" << json::quote(k.name) << ",\"kind\":\"constant\",\"type\":" << json::quote(k.type) << "}";
            firstMember = false;
        }
        out << "]}";
        first = false;
    }
    out << "],\n\"diagnostics\":";
    printDiagnostics(c.diagnostics, true, out);
    out << "}\n";
    return 0;
}

// ============================================================
// Editor support: dessa complete / dessa hover
// ============================================================

const char* const kCompletionPlaceholder = "__ds_complete__";

std::string readSource(const std::string& path, bool fromStdin) {
    if (fromStdin) return readStdin();
    std::ifstream file(path, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// "12:5" -> line 12, column 5 (both from 1)
bool parsePosition(const std::string& text, int& line, int& column) {
    size_t colon = text.find(':');
    if (colon == std::string::npos) return false;
    try {
        line = std::stoi(text.substr(0, colon));
        column = std::stoi(text.substr(colon + 1));
    } catch (...) {
        return false;
    }
    return line >= 1 && column >= 1;
}

bool isWordChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// The source with the half-typed word at the cursor replaced by the
// placeholder, and the current line closed off so it parses:
//     print(customer.na|      ->  print(customer.__ds_complete__);
//     if (list.|              ->  if (list.__ds_complete__) {}
// The rest of the line after the cursor is dropped. With insertPlaceholder
// false the typed text is kept (signature help: `greet("Ann", |`).
std::string completionSource(const std::string& source, int line, int column, bool insertPlaceholder = true) {
    std::vector<std::string> lines;
    std::string current;
    for (char ch : source) {
        if (ch == '\n') { lines.push_back(current); current.clear(); }
        else current += ch;
    }
    lines.push_back(current);
    if (line > static_cast<int>(lines.size())) return source;

    std::string text = lines[static_cast<size_t>(line - 1)];
    if (!text.empty() && text.back() == '\r') text.pop_back();
    size_t cursor = std::min(static_cast<size_t>(column - 1), text.size());
    size_t wordStart = cursor;
    if (insertPlaceholder) {
        while (wordStart > 0 && isWordChar(text[wordStart - 1])) --wordStart;
    }
    std::string before = text.substr(0, wordStart);
    if (!insertPlaceholder) {
        // `f(a, ` -> `f(a` so the closed call still parses
        while (!before.empty() && (before.back() == ' ' || before.back() == '\t')) before.pop_back();
        if (!before.empty() && before.back() == ',') before.pop_back();
    }

    // Brackets opened on this line and not closed yet (outside strings)
    std::string open;
    char quote = 0;
    for (size_t i = 0; i < before.size(); ++i) {
        char ch = before[i];
        if (quote) {
            if (ch == '\\') ++i;
            else if (ch == quote) quote = 0;
            continue;
        }
        if (ch == '/' && i + 1 < before.size() && before[i + 1] == '/') return source; // in a comment
        if (ch == '"' || ch == '\'') quote = ch;
        else if (ch == '(' || ch == '[' || ch == '{') open += ch;
        else if ((ch == ')' || ch == ']' || ch == '}') && !open.empty()) open.pop_back();
    }
    if (quote) return source; // inside a string
    // A `{` still open on this line is a block whose `}` comes later; only
    // close what was opened after it.
    size_t lastBrace = open.rfind('{');
    if (lastBrace != std::string::npos) open = open.substr(lastBrace + 1);
    std::string closers;
    for (auto it = open.rbegin(); it != open.rend(); ++it) closers += (*it == '(' ? ')' : ']');

    size_t first = before.find_first_not_of(" \t");
    std::string head = first == std::string::npos ? "" : before.substr(first);
    auto startsWithWord = [&](const std::string& word) {
        return head.compare(0, word.size(), word) == 0 && (head.size() == word.size() || !isWordChar(head[word.size()]));
    };
    bool control = startsWithWord("if") || startsWithWord("while") || startsWithWord("for") ||
                   startsWithWord("switch") || (startsWithWord("else") && head.find("if") != std::string::npos);
    std::string ending = control && !closers.empty() ? closers + " {}" : closers + ";";

    lines[static_cast<size_t>(line - 1)] = before + (insertPlaceholder ? kCompletionPlaceholder : "") + ending;
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) out += (i ? "\n" : "") + lines[i];
    return out;
}

// The file a symbol was declared in, when it isn't the edited file itself
std::string declaringFile(const Compilation& c, const Symbol& symbol) {
    if (symbol.kind == SymbolKind::Builtin || symbol.line <= 0 || symbol.moduleId < 0 ||
        static_cast<size_t>(symbol.moduleId) >= c.program.modules.size() ||
        symbol.moduleId == c.program.entry().id) {
        return "";
    }
    return c.program.modules[static_cast<size_t>(symbol.moduleId)]->path;
}

int cmdComplete(const std::string& path, bool fromStdin, const std::string& at) {
    int line = 0, column = 0;
    if (!parsePosition(at, line, column)) {
        std::cerr << "dessa complete: expected --at <line>:<column>\n";
        return 1;
    }
    std::string raw = readSource(path, fromStdin);
    if (web::isPage(path)) raw = web::dessaOnly(path, raw, web::offsetOf(raw, line, column));
    std::string source = completionSource(raw, line, column);
    Compilation c;
    compileFrontEnd(c, path, &source, [](Compilation& comp) { comp.analyser.setCompletionProbe(kCompletionPlaceholder); });

    const auto& probe = c.analyser.completionProbe();
    std::ostream& out = std::cout;
    out << "{\"found\":" << (probe.found ? "true" : "false");
    if (!probe.found) {
        out << "}\n";
        return 0;
    }
    out << ",\"member\":" << (probe.isMember ? "true" : "false");
    std::vector<Symbol> items;
    if (probe.isMember) {
        out << ",\"objectType\":" << json::quote(probe.objectType.toString());
        if (probe.objectIsType) {
            if (const EnumInfo* info = c.analyser.findEnum(probe.objectType)) {
                for (const auto& v : info->values) {
                    Symbol s;
                    s.kind = SymbolKind::Constant;
                    s.name = v;
                    s.type = info->type();
                    items.push_back(s);
                }
            }
        } else if (!probe.objectType.isUnknown()) {
            items = c.analyser.membersOf(probe.objectType);
        }
    } else {
        items = probe.visible;
    }
    out << ",\"items\":[";
    bool first = true;
    for (const auto& s : items) {
        if (s.name == kCompletionPlaceholder || s.name.empty()) continue;
        // _helpers are private to their class: only offered after `self.`
        if (probe.isMember && !probe.objectIsSelf && s.name[0] == '_') continue;
        out << (first ? "\n  " : ",\n  ");
        writeSymbol(out, s.name, s, declaringFile(c, s));
        first = false;
    }
    out << "]}\n";
    return 0;
}

// --cursor L:C (signature help): the line being typed at L:C is closed off
// first, so `greet("Ann", ` still parses as a call.
int cmdHover(const std::string& path, bool fromStdin, const std::string& at, const std::string& cursor) {
    int line = 0, column = 0;
    if (!parsePosition(at, line, column)) {
        std::cerr << "dessa hover: expected --at <line>:<column>\n";
        return 1;
    }
    std::string source = readSource(path, fromStdin);
    if (web::isPage(path)) source = web::dessaOnly(path, source, web::offsetOf(source, line, column));
    int cursorLine = 0, cursorColumn = 0;
    if (!cursor.empty() && parsePosition(cursor, cursorLine, cursorColumn)) {
        source = completionSource(source, cursorLine, cursorColumn, false);
    }
    Compilation c;
    compileFrontEnd(c, path, &source, [&](Compilation& comp) {
        comp.analyser.setHoverProbe(comp.program.entry().id, line, column);
    });

    const auto& probe = c.analyser.hoverProbe();
    std::ostream& out = std::cout;
    out << "{\"found\":" << (probe.found ? "true" : "false");
    if (probe.found) {
        out << ",\"type\":" << json::quote(probe.type.toString());
        if (!probe.memberOf.empty()) out << ",\"memberOf\":" << json::quote(probe.memberOf);
        out << ",\"symbol\":";
        writeSymbol(out, probe.symbol.name, probe.symbol, declaringFile(c, probe.symbol));
    }
    out << "}\n";
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    g_argv0 = argv[0];
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "-h" || args[0] == "--help" || args[0] == "help") {
        printUsage();
        return args.empty() ? 1 : 0;
    }
    if (args[0] == "--version" || args[0] == "version") {
        std::cout << "dessa " << kVersion << "\n";
        return 0;
    }

    const std::string command = args[0];
    std::string file;
    std::string output;
    std::string at;
    std::string cursor;
    bool asJson = false;
    bool fromStdin = false;
    std::vector<std::string> programArgs;

    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--") {
            programArgs.assign(args.begin() + static_cast<long>(i) + 1, args.end());
            break;
        } else if (a == "--json") {
            asJson = true;
        } else if (a == "--stdin") {
            fromStdin = true;
        } else if (a == "--at" && i + 1 < args.size()) {
            at = args[++i];
        } else if (a == "--target" && i + 1 < args.size()) {
            g_target = args[++i];
            if (g_target != "native" && g_target != "js" && g_target != "web") {
                std::cerr << "dessa: unknown target '" << g_target << "' (use native, js or web)\n";
                return 1;
            }
        } else if (a == "--port" && i + 1 < args.size()) {
            g_port = std::atoi(args[++i].c_str());
            if (g_port <= 0 || g_port > 65535) g_port = 8080;
        } else if (a == "--cursor" && i + 1 < args.size()) {
            cursor = args[++i];
        } else if (a == "-o" && i + 1 < args.size()) {
            output = args[++i];
        } else if (file.empty()) {
            file = a;
        } else {
            programArgs.push_back(a);
        }
    }

    if (command == "serve") {
        if (!file.empty() && !fs::is_directory(file)) {
            std::cerr << "dessa serve: " << file << " is not a folder\n";
            return 1;
        }
        return web::serve(file.empty() ? "." : file, g_port, "", compileScriptToJs);
    }
    if (file.empty()) {
        std::cerr << "dessa " << command << ": missing <file.ds>\n";
        printUsage();
        return 1;
    }
    if (!fromStdin && !fs::exists(file)) {
        std::cerr << "dessa: file not found: " << file << "\n";
        return 1;
    }
    if (fs::path(file).extension() == ".dm" && (command == "run" || command == "build" || command == "emit")) {
        std::cerr << "dessa: " << file << " is a module file (.dm, like a C++ header) and can't be run on its own;\n"
                  << "       import it from a .ds file, e.g. $(\"" << fs::path(file).stem().string() << "\");\n";
        return 1;
    }

    if (command == "check") return cmdCheck(file, asJson, fromStdin);
    if (command == "emit") return cmdEmit(file, output);
    if (command == "build") return cmdBuild(file, output, argv[0]);
    if (command == "run") return cmdRun(file, programArgs, argv[0]);
    if (command == "symbols") return cmdSymbols(file, fromStdin, argv[0]);
    if (command == "complete") return cmdComplete(file, fromStdin, at);
    if (command == "hover") return cmdHover(file, fromStdin, at, cursor);
    if (command == "tokens") return cmdTokens(file);
    if (command == "ast") return cmdAst(file);

    std::cerr << "dessa: unknown command '" << command << "'\n";
    printUsage();
    return 1;
}
