#include "builtins.hpp"

#include <algorithm>
#include <deque>
#include <fstream>

namespace dessa {

namespace {

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(' ');
    size_t e = s.find_last_not_of(' ');
    return b == std::string::npos ? "" : s.substr(b, e - b + 1);
}

} // namespace

SemanticType parseBuiltinType(const std::string& raw) {
    std::string text = trim(raw);

    size_t bar = text.find('|');
    if (bar != std::string::npos) {
        std::vector<SemanticType> members;
        size_t start = 0;
        while (true) {
            size_t next = text.find('|', start);
            members.push_back(parseBuiltinType(text.substr(start, next == std::string::npos ? std::string::npos : next - start)));
            if (next == std::string::npos) break;
            start = next + 1;
        }
        return SemanticType::unionOf(std::move(members));
    }

    if (text.size() > 2 && text.compare(text.size() - 2, 2, "<>") == 0) {
        return SemanticType::listOf(parseBuiltinType(text.substr(0, text.size() - 2)));
    }
    if (text.size() > 2 && text.compare(text.size() - 2, 2, "{}") == 0) {
        return SemanticType::setOf(parseBuiltinType(text.substr(0, text.size() - 2)));
    }

    if (text == "Str") return SemanticType::simple(SemanticTypeKind::Str);
    if (text == "Int") return SemanticType::simple(SemanticTypeKind::Int);
    if (text == "Bul") return SemanticType::simple(SemanticTypeKind::Bul);
    if (text == "Flt") return SemanticType::simple(SemanticTypeKind::Flt);
    if (text == "Dbl") return SemanticType::simple(SemanticTypeKind::Dbl);
    if (text == "Chr") return SemanticType::simple(SemanticTypeKind::Chr);
    if (text == "Emp") return SemanticType::simple(SemanticTypeKind::Emp);
    if (text == "Nil") return SemanticType::simple(SemanticTypeKind::Nil);
    if (text == "Obj") return SemanticType::simple(SemanticTypeKind::Obj);

    SemanticType pseudo = SemanticType::unknown();
    pseudo.name = text; // "Num", "T", "Any"
    return pseudo;
}

const std::vector<BuiltinFunction>& globalBuiltins() {
    static const std::vector<BuiltinFunction> functions = {
        {"print", {{"values", "Any"}}, "Emp", "dessa::print", true},
        {"input", {{"prompt", "Str"}}, "Str", "dessa::input", false},
        {"typeof", {{"value", "Any"}}, "Str", "dessa::type_of", false},
        {"zip", {{"a", "Any"}, {"b", "Any"}}, "Any", "dessa::zip", false},   // typed by Analyser::analyseZip
        // Fields by name (class instances and anonymous objects)
        {"fieldsOf", {{"value", "Any"}}, "Str<>", "dessa::fields_of", false},
        {"getField", {{"value", "Any"}, {"name", "Str"}}, "Obj", "dessa::get_field", false},
        {"setField", {{"value", "Any"}, {"name", "Str"}, {"newValue", "Any"}}, "Emp", "dessa::set_field", false},
    };
    return functions;
}

const std::vector<BuiltinLibrary>& builtinLibraries() {
    static const std::vector<BuiltinLibrary> libraries = {
        {
            "math", "math/math.hpp",
            {
                {"sqrt",  {{"x", "Dbl"}}, "Dbl", "dessa::math::sqrt"},
                {"cbrt",  {{"x", "Dbl"}}, "Dbl", "dessa::math::cbrt"},
                {"pow",   {{"base", "Dbl"}, {"exponent", "Dbl"}}, "Dbl", "dessa::math::pow"},
                {"abs",   {{"x", "Num"}}, "T", "dessa::math::abs"},
                {"min",   {{"a", "Num"}, {"b", "Num"}}, "T", "dessa::math::min"},
                {"max",   {{"a", "Num"}, {"b", "Num"}}, "T", "dessa::math::max"},
                {"clamp", {{"x", "Num"}, {"low", "Num"}, {"high", "Num"}}, "T", "dessa::math::clamp"},
                {"floor", {{"x", "Dbl"}}, "Int", "dessa::math::floor"},
                {"ceil",  {{"x", "Dbl"}}, "Int", "dessa::math::ceil"},
                {"round", {{"x", "Dbl"}}, "Int", "dessa::math::round"},
                {"sin",   {{"x", "Dbl"}}, "Dbl", "dessa::math::sin"},
                {"cos",   {{"x", "Dbl"}}, "Dbl", "dessa::math::cos"},
                {"tan",   {{"x", "Dbl"}}, "Dbl", "dessa::math::tan"},
                {"atan2", {{"y", "Dbl"}, {"x", "Dbl"}}, "Dbl", "dessa::math::atan2"},
                {"log",   {{"x", "Dbl"}}, "Dbl", "dessa::math::log"},
                {"log10", {{"x", "Dbl"}}, "Dbl", "dessa::math::log10"},
                {"exp",   {{"x", "Dbl"}}, "Dbl", "dessa::math::exp"},
                {"random",    {}, "Dbl", "dessa::math::random"},
                {"randomInt", {{"low", "Int"}, {"high", "Int"}}, "Int", "dessa::math::randomInt"},
                // helpers for class Big in math.dm (digits as text)
                {"_bigNormalize", {{"text", "Str"}}, "Str", "dessa::math::_bigNormalize"},
                {"_bigAdd", {{"a", "Str"}, {"b", "Str"}}, "Str", "dessa::math::_bigAdd"},
                {"_bigSub", {{"a", "Str"}, {"b", "Str"}}, "Str", "dessa::math::_bigSub"},
                {"_bigMul", {{"a", "Str"}, {"b", "Str"}}, "Str", "dessa::math::_bigMul"},
                {"_bigDiv", {{"a", "Str"}, {"b", "Str"}}, "Str", "dessa::math::_bigDiv"},
                {"_bigMod", {{"a", "Str"}, {"b", "Str"}}, "Str", "dessa::math::_bigMod"},
                {"_bigPow", {{"a", "Str"}, {"exponent", "Int"}}, "Str", "dessa::math::_bigPow"},
                {"_bigCompare", {{"a", "Str"}, {"b", "Str"}}, "Int", "dessa::math::_bigCompare"},
                {"_bigSci", {{"a", "Str"}, {"digits", "Int"}}, "Str", "dessa::math::_bigSci"},
                {"_bigToInt", {{"a", "Str"}}, "Int", "dessa::math::_bigToInt"},
                {"_bigToDbl", {{"a", "Str"}}, "Dbl", "dessa::math::_bigToDbl"},
            },
            {
                {"PI", "Dbl", "dessa::math::PI"},
                {"E",  "Dbl", "dessa::math::E"},
            },
            {}, {}, {}, "math/math.c",
        },
        {
            "os", "os/os.hpp",
            {
                {"readFile",   {{"path", "Str"}}, "Str", "dessa::os::readFile"},
                {"writeFile",  {{"path", "Str"}, {"content", "Str"}}, "Emp", "dessa::os::writeFile"},
                {"appendFile", {{"path", "Str"}, {"content", "Str"}}, "Emp", "dessa::os::appendFile"},
                {"fileExists", {{"path", "Str"}}, "Bul", "dessa::os::fileExists"},
                {"isDir",      {{"path", "Str"}}, "Bul", "dessa::os::isDir"},
                {"deleteAll",  {{"path", "Str"}}, "Int", "dessa::os::deleteAll"},
                {"deleteFile", {{"path", "Str"}}, "Bul", "dessa::os::deleteFile"},
                {"listDir",    {{"path", "Str"}}, "Str<>", "dessa::os::listDir"},
                {"makeDir",    {{"path", "Str"}}, "Bul", "dessa::os::makeDir"},
                {"cwd",        {}, "Str", "dessa::os::cwd"},
                {"getEnv",     {{"name", "Str"}}, "Str | Nil", "dessa::os::getEnv"},
                {"run",        {{"command", "Str"}}, "Int", "dessa::os::run"},
                // helpers for class File in os.dm
                {"_fileOpen",     {{"path", "Str"}, {"mode", "Str"}}, "Int", "dessa::os::_fileOpen"},
                {"_fileReadLine", {{"handle", "Int"}}, "Str | Nil", "dessa::os::_fileReadLine"},
                {"_fileRead",     {{"handle", "Int"}}, "Str", "dessa::os::_fileRead"},
                {"_fileAtEnd",    {{"handle", "Int"}}, "Bul", "dessa::os::_fileAtEnd"},
                {"_fileWrite",    {{"handle", "Int"}, {"text", "Str"}}, "Emp", "dessa::os::_fileWrite"},
                {"_fileFlush",    {{"handle", "Int"}}, "Emp", "dessa::os::_fileFlush"},
                {"_fileClose",    {{"handle", "Int"}}, "Emp", "dessa::os::_fileClose"},
            },
            {},
            {}, {}, {}, "os/os.c",
        },
        {
            "sys", "sys/sys.hpp",
            {
                {"args",     {}, "Str<>", "dessa::sys::args"},
                {"exit",     {{"code", "Int"}}, "Emp", "dessa::sys::exit"},
                {"platform", {}, "Str", "dessa::sys::platform"},
            },
            {},
        },
        {
            "time", "time/time.hpp",
            {
                {"now",    {}, "Dbl", "dessa::time::now"},
                {"millis", {}, "Int", "dessa::time::millis"},
                {"sleep",  {{"ms", "Int"}}, "Emp", "dessa::time::sleep"},
                {"date",   {}, "Str", "dessa::time::date"},
                // helpers for class DateTime in time.dm
                {"_epochMillis", {}, "Int", "dessa::time::_epochMillis"},
                {"_epochOf", {{"year", "Int"}, {"month", "Int"}, {"day", "Int"}, {"hour", "Int"}, {"minute", "Int"},
                              {"second", "Int"}, {"ms", "Int"}}, "Int", "dessa::time::_epochOf"},
                {"_civil", {{"utc", "Int"}, {"offsetMinutes", "Int"}}, "Int<>", "dessa::time::_civil"},
                {"_offsetMinutes", {{"zone", "Str"}, {"utc", "Int"}}, "Int", "dessa::time::_offsetMinutes"},
                {"_isZone", {{"zone", "Str"}}, "Bul", "dessa::time::_isZone"},
            },
            {},
            {}, {}, {}, "time/time.c",
        },
        {
            "random", "random/random.hpp",
            {
                {"seed",    {{"value", "Int"}}, "Emp", "dessa::random::seed"},
                {"rand",    {}, "Dbl", "dessa::random::rand"},
                {"randInt", {{"low", "Int"}, {"high", "Int"}}, "Int", "dessa::random::randInt"},
                {"randDbl", {{"low", "Dbl"}, {"high", "Dbl"}}, "Dbl", "dessa::random::randDbl"},
                {"chance",  {{"p", "Dbl"}}, "Bul", "dessa::random::chance"},
                {"pick",    {{"items", "List"}}, "Elem", "dessa::random::pick"},
                {"shuffle", {{"items", "List"}}, "Same", "dessa::random::shuffle"},
                {"sample",  {{"items", "List"}, {"count", "Int"}}, "Same", "dessa::random::sample"},
                {"randStr", {{"length", "Int"}}, "Str", "dessa::random::randStr"},
                {"uuid",    {}, "Str", "dessa::random::uuid"},
            },
            {},
        },
        {
            "dcrypto", "dcrypto/dcrypto.hpp",
            {
                {"md5",            {{"text", "Str"}}, "Str", "dessa::dcrypto::md5"},
                {"sha1",           {{"text", "Str"}}, "Str", "dessa::dcrypto::sha1"},
                {"sha256",         {{"text", "Str"}}, "Str", "dessa::dcrypto::sha256"},
                {"sha512",         {{"text", "Str"}}, "Str", "dessa::dcrypto::sha512"},
                {"hmac",           {{"key", "Str"}, {"text", "Str"}}, "Str", "dessa::dcrypto::hmac"},
                {"hashPassword",   {{"password", "Str"}}, "Str", "dessa::dcrypto::hashPassword"},
                {"verifyPassword", {{"password", "Str"}, {"stored", "Str"}}, "Bul", "dessa::dcrypto::verifyPassword"},
                {"encrypt",        {{"text", "Str"}, {"password", "Str"}}, "Str", "dessa::dcrypto::encrypt"},
                {"decrypt",        {{"data", "Str"}, {"password", "Str"}}, "Str", "dessa::dcrypto::decrypt"},
                {"toBase64",       {{"text", "Str"}}, "Str", "dessa::dcrypto::toBase64"},
                {"fromBase64",     {{"text", "Str"}}, "Str", "dessa::dcrypto::fromBase64"},
                {"toHex",          {{"text", "Str"}}, "Str", "dessa::dcrypto::toHex"},
                {"fromHex",        {{"text", "Str"}}, "Str", "dessa::dcrypto::fromHex"},
                {"token",          {{"bytes", "Int"}}, "Str", "dessa::dcrypto::token"},
            },
            {},
            {}, {}, {}, "dcrypto/dcrypto.c",
        },
    };
    return libraries;
}

namespace {

std::deque<BuiltinLibrary>& packageStore() {
    static std::deque<BuiltinLibrary> store; // deque: pointers stay valid
    return store;
}

std::vector<const BuiltinLibrary*>& packageList() {
    static std::vector<const BuiltinLibrary*> list;
    return list;
}

// "a[Str], b[Map[Str, Int]]" -> {"a[Str]", "b[Map[Str, Int]]"}
std::vector<std::string> splitTopLevel(const std::string& text) {
    std::vector<std::string> parts;
    std::string current;
    int depth = 0;
    for (char c : text) {
        if (c == '[' || c == '(') depth++;
        if (c == ']' || c == ')') depth--;
        if (c == ',' && depth == 0) {
            parts.push_back(trim(current));
            current.clear();
        } else {
            current += c;
        }
    }
    if (!trim(current).empty()) parts.push_back(trim(current));
    return parts;
}

// "func[Int] _open(path[Str], mode[Int])" -> name, return type, params
bool parseNativeFunction(const std::string& text, BuiltinFunction& fn) {
    size_t open = text.find('[');
    if (open == std::string::npos) return false;
    int depth = 0;
    size_t close = std::string::npos;
    for (size_t i = open; i < text.size(); ++i) {
        if (text[i] == '[') depth++;
        if (text[i] == ']' && --depth == 0) { close = i; break; }
    }
    size_t paren = text.find('(', close == std::string::npos ? 0 : close);
    if (close == std::string::npos || paren == std::string::npos || text.back() != ')') return false;
    fn.returnType = trim(text.substr(open + 1, close - open - 1));
    fn.name = trim(text.substr(close + 1, paren - close - 1));
    for (const auto& p : splitTopLevel(text.substr(paren + 1, text.size() - paren - 2))) {
        size_t bracket = p.find('[');
        if (bracket == std::string::npos || p.back() != ']') return false;
        fn.params.push_back({trim(p.substr(0, bracket)), trim(p.substr(bracket + 1, p.size() - bracket - 2))});
    }
    return !fn.name.empty() && !fn.returnType.empty();
}

} // namespace

const std::vector<const BuiltinLibrary*>& packageLibraries() {
    return packageList();
}

bool loadNativeLibrary(const std::string& folder, const std::string& name, std::string& error) {
    if (findBuiltinLibrary(name)) return true;
    const std::string path = folder + "/" + name + ".native";
    std::ifstream in(path);
    if (!in) {
        error = "cannot read " + path;
        return false;
    }
    BuiltinLibrary lib;
    lib.name = name;
    lib.baseDir = folder;
    std::string ns = "dessa::" + name;
    // "source.linux", "link.macos", ...: lines for one system. unix = Linux and macOS.
#ifdef _WIN32
    const std::vector<std::string> thisOs = {"windows"};
#elif defined(__APPLE__)
    const std::vector<std::string> thisOs = {"macos", "unix"};
#else
    const std::vector<std::string> thisOs = {"linux", "unix"};
#endif
    auto forThisOs = [&](const std::string& key, const std::string& base) {
        if (key == base) return true;
        for (const auto& os : thisOs) if (key == base + "." + os) return true;
        return false;
    };
    std::string line;
    int number = 0;
    while (std::getline(in, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t comment = line.find("//");
        if (comment != std::string::npos) line = line.substr(0, comment);
        line = trim(line);
        if (line.empty()) continue;
        size_t space = line.find_first_of(" [");
        std::string key = line.substr(0, space);
        std::string value = space == std::string::npos ? "" : trim(line.substr(space));
        auto bad = [&](const std::string& what) {
            error = path + ":" + std::to_string(number) + ": " + what;
            return false;
        };
        if (key == "header") {
            lib.header = folder + "/" + value;
        } else if (key == "c") {
            lib.cImpl = value;
        } else if (key == "wasm.cflags") {
            lib.wasmFlags += " " + value;
        } else if (key == "wasm.link") {
            lib.wasmLink += " " + value;
        } else if (forThisOs(key, "source")) {
            lib.cSources.push_back(value);
        } else if (forThisOs(key, "link")) {
            lib.linkFlags.push_back(value);
        } else if (key.rfind("link.", 0) == 0 || key.rfind("source.", 0) == 0) {
            // for another system
        } else if (key == "namespace") {
            ns = value;
        } else if (key == "func") {
            BuiltinFunction fn;
            if (!parseNativeFunction(line.substr(4), fn)) return bad("expected func[Type] name(param[Type], ...)");
            fn.cpp = ns + "::" + fn.name;
            lib.functions.push_back(fn);
        } else if (key == "const") {
            size_t close = line.find(']');
            if (line.size() < 7 || line[5] != '[' || close == std::string::npos) return bad("expected const[Type] NAME");
            std::string constName = trim(line.substr(close + 1));
            lib.constants.push_back({constName, trim(line.substr(6, close - 6)), ns + "::" + constName});
        } else {
            return bad("unknown entry '" + key + "' (expected header, source, c, link, namespace, func or const)");
        }
    }
    if (lib.header.empty()) {
        error = path + ": missing 'header <file>.hpp'";
        return false;
    }
    packageStore().push_back(std::move(lib));
    packageList().push_back(&packageStore().back());
    return true;
}

const BuiltinLibrary* findBuiltinLibrary(const std::string& name) {
    const auto& libs = builtinLibraries();
    auto it = std::find_if(libs.begin(), libs.end(), [&](const BuiltinLibrary& lib) { return lib.name == name; });
    if (it != libs.end()) return &*it;
    for (const BuiltinLibrary* lib : packageList()) {
        if (lib->name == name) return lib;
    }
    return nullptr;
}

} // namespace dessa
