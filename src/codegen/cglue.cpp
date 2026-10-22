#include "cglue.hpp"

#include <sstream>

namespace dessa {

namespace {

bool isPlainNumber(const SemanticType& t) {
    return t.is(SemanticTypeKind::Int) || t.is(SemanticTypeKind::Dbl) || t.is(SemanticTypeKind::Flt) ||
           t.is(SemanticTypeKind::Bul) || t.is(SemanticTypeKind::Chr);
}

// dessa::Int, dessa::List<dessa::Str>, dessa::Opt<dessa::Str>
std::string cppType(const SemanticType& t) {
    switch (t.kind) {
        case SemanticTypeKind::Int: return "dessa::Int";
        case SemanticTypeKind::Dbl: return "dessa::Dbl";
        case SemanticTypeKind::Flt: return "dessa::Flt";
        case SemanticTypeKind::Bul: return "dessa::Bul";
        case SemanticTypeKind::Chr: return "dessa::Chr";
        case SemanticTypeKind::Str: return "dessa::Str";
        case SemanticTypeKind::Emp: return "void";
        case SemanticTypeKind::List: return "dessa::List<" + cppType(*t.elementType) + ">";
        case SemanticTypeKind::Union: return "dessa::Opt<" + cppType(withoutNil(t)) + ">";
        default: return "void";
    }
}

std::string elemOf(const SemanticType& t) {
    switch (t.kind) {
        case SemanticTypeKind::Int: return "&ds_elem_int";
        case SemanticTypeKind::Dbl: return "&ds_elem_dbl";
        case SemanticTypeKind::Flt: return "&ds_elem_flt";
        case SemanticTypeKind::Bul: return "&ds_elem_bul";
        case SemanticTypeKind::Chr: return "&ds_elem_chr";
        case SemanticTypeKind::Str: return "&ds_elem_str";
        default: return "&ds_elem_ref";
    }
}

// A C value (expression `e` of glueCType(t)) as the C++ value
std::string toCpp(const std::string& e, const SemanticType& t) {
    switch (t.kind) {
        case SemanticTypeKind::Chr: return "static_cast<dessa::Chr>(" + e + ")";
        case SemanticTypeKind::Str: return "dessa::Str((" + e + ")->data, static_cast<size_t>((" + e + ")->len))";
        case SemanticTypeKind::List: {
            const SemanticType& et = *t.elementType;
            return "[](ds_list* l) { " + cppType(t) + " v; v.reserve(static_cast<size_t>(l->len)); for (int64_t i = 0; i < l->len; ++i) v.push_back(" +
                   toCpp("((" + glueCType(et) + "*)l->data)[i]", et) + "); return v; }(" + e + ")";
        }
        case SemanticTypeKind::Union: {
            const SemanticType base = withoutNil(t);
            if (isPlainNumber(base)) return "((" + e + ").has ? " + cppType(t) + "(" + toCpp("(" + e + ").value", base) + ") : " + cppType(t) + "())";
            return "((" + e + ") ? " + cppType(t) + "(" + toCpp(e, base) + ") : " + cppType(t) + "())";
        }
        default: return e;
    }
}

// A C++ value (expression `e` of cppType(t)) as a new C value
std::string toC(const std::string& e, const SemanticType& t) {
    switch (t.kind) {
        case SemanticTypeKind::Chr: return "static_cast<ds_chr>(" + e + ")";
        case SemanticTypeKind::Str: return "[](const dessa::Str& s) { return ds_str_new(s.data(), static_cast<int64_t>(s.size())); }(" + e + ")";
        case SemanticTypeKind::List: {
            const SemanticType& et = *t.elementType;
            return "[](const " + cppType(t) + "& v) { ds_list* l = ds_list_new(" + elemOf(et) + ", static_cast<int64_t>(v.size())); for (const auto& x : v) ((" +
                   glueCType(et) + "*)l->data)[l->len++] = " + toC("x", et) + "; return l; }(" + e + ")";
        }
        case SemanticTypeKind::Union: {
            const SemanticType base = withoutNil(t);
            if (isPlainNumber(base)) {
                return "[](const " + cppType(t) + "& o) { " + glueCType(t) + " r{}; r.has = o.has_value(); if (o) r.value = " + toC("*o", base) +
                       "; return r; }(" + e + ")";
            }
            return "[](const " + cppType(t) + "& o) -> " + glueCType(t) + " { return o ? " + toC("*o", base) + " : nullptr; }(" + e + ")";
        }
        default: return e;
    }
}

std::string sanitize(const std::string& name) {
    std::string out;
    for (char c : name) out += (std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
    return out;
}

const std::vector<const BuiltinLibrary*> allLibraries() {
    std::vector<const BuiltinLibrary*> all;
    for (const auto& lib : builtinLibraries()) all.push_back(&lib);
    for (const auto* lib : packageLibraries()) all.push_back(lib);
    return all;
}

} // namespace

std::string glueCType(const SemanticType& t) {
    switch (t.kind) {
        case SemanticTypeKind::Int: return "ds_int";
        case SemanticTypeKind::Dbl: return "ds_dbl";
        case SemanticTypeKind::Flt: return "ds_flt";
        case SemanticTypeKind::Bul: return "ds_bul";
        case SemanticTypeKind::Chr: return "ds_chr";
        case SemanticTypeKind::Str: return "ds_str*";
        case SemanticTypeKind::Emp: return "void";
        case SemanticTypeKind::List: {
            if (!t.elementType) return "";
            const SemanticType& et = *t.elementType;
            if (et.kind == SemanticTypeKind::Union || glueCType(et).empty() || et.is(SemanticTypeKind::Emp)) return "";
            return "ds_list*";
        }
        case SemanticTypeKind::Union: {
            if (!t.containsNil()) return "";
            const SemanticType base = withoutNil(t);
            switch (base.kind) {
                case SemanticTypeKind::Int: return "ds_opt_int";
                case SemanticTypeKind::Dbl: return "ds_opt_dbl";
                case SemanticTypeKind::Flt: return "ds_opt_flt";
                case SemanticTypeKind::Bul: return "ds_opt_bul";
                case SemanticTypeKind::Chr: return "ds_opt_chr";
                case SemanticTypeKind::Str: return "ds_str*";
                case SemanticTypeKind::List: return glueCType(base);
                default: return "";
            }
        }
        default: return "";
    }
}

const BuiltinLibrary* glueLibraryOf(const std::string& cppName, const BuiltinFunction** function, const BuiltinConstant** constant) {
    for (const auto* lib : allLibraries()) {
        for (const auto& f : lib->functions) {
            if (f.cpp == cppName) {
                if (function) *function = &f;
                return lib;
            }
        }
        for (const auto& c : lib->constants) {
            if (c.cpp == cppName) {
                if (constant) *constant = &c;
                return lib;
            }
        }
    }
    return nullptr;
}

bool glueable(const BuiltinFunction& f) {
    if (f.variadic) return false;
    if (glueCType(parseBuiltinType(f.returnType)).empty()) return false;
    for (const auto& p : f.params) {
        const SemanticType t = parseBuiltinType(p.type);
        if (t.is(SemanticTypeKind::Emp) || glueCType(t).empty()) return false;
    }
    return true;
}

bool glueable(const BuiltinConstant& c) {
    const SemanticType t = parseBuiltinType(c.type);
    return !t.is(SemanticTypeKind::Emp) && !glueCType(t).empty();
}

std::string glueName(const BuiltinLibrary& library, const std::string& name) {
    return "dsx_" + sanitize(library.name) + "_" + sanitize(name);
}

std::string gluePrototype(const BuiltinLibrary& library, const BuiltinFunction& f) {
    std::string s = "extern " + glueCType(parseBuiltinType(f.returnType)) + " " + glueName(library, f.name) + "(";
    for (size_t i = 0; i < f.params.size(); ++i) s += (i ? ", " : "") + glueCType(parseBuiltinType(f.params[i].type));
    if (f.params.empty()) s += "void";
    return s + ");";
}

std::string gluePrototype(const BuiltinLibrary& library, const BuiltinConstant& c) {
    return "extern " + glueCType(parseBuiltinType(c.type)) + " " + glueName(library, c.name) + "(void);";
}

std::string glueSource(const BuiltinLibrary& library) {
    std::ostringstream out;
    out << "// The C bridge of $(\"" << library.name << "\") (generated by the Dessa compiler)\n";
    out << "#include \"dessa_runtime.hpp\"\n";
    out << "#include \"" << library.header << "\"\n";
    out << "extern \"C\" {\n#include \"dessa_rt.h\"\n}\n\n";
    for (const auto& f : library.functions) {
        if (!glueable(f)) continue;
        const SemanticType ret = parseBuiltinType(f.returnType);
        const std::string cret = glueCType(ret);
        out << "extern \"C\" " << cret << " " << glueName(library, f.name) << "(";
        std::string args;
        for (size_t i = 0; i < f.params.size(); ++i) {
            const SemanticType t = parseBuiltinType(f.params[i].type);
            out << (i ? ", " : "") << glueCType(t) << " a" << i;
            args += (i ? ", " : "") + toCpp("a" + std::to_string(i), t);
        }
        if (f.params.empty()) out << "void";
        out << ") {\n";
        // No C++ value may be alive when the error is thrown (longjmp)
        out << "    ds_str* error = nullptr;\n";
        if (!ret.is(SemanticTypeKind::Emp)) out << "    " << cret << " result{};\n";
        out << "    try {\n";
        if (ret.is(SemanticTypeKind::Emp)) out << "        " << f.cpp << "(" << args << ");\n";
        else out << "        result = " << toC(f.cpp + "(" + args + ")", ret) << ";\n";
        out << "    } catch (const std::exception& e) {\n        error = ds_str_cstr(e.what());\n"
            << "    } catch (...) {\n        error = ds_str_cstr(\"Error in the library '" << library.name << "'\");\n    }\n";
        out << "    if (error) ds_throw_str(error);\n";
        if (!ret.is(SemanticTypeKind::Emp)) out << "    return result;\n";
        out << "}\n\n";
    }
    for (const auto& c : library.constants) {
        if (!glueable(c)) continue;
        const SemanticType t = parseBuiltinType(c.type);
        out << "extern \"C\" " << glueCType(t) << " " << glueName(library, c.name) << "(void) {\n    return " << toC(c.cpp, t) << ";\n}\n\n";
    }
    return out.str();
}

} // namespace dessa
