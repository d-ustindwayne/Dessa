// $(<sql>) implementation: ODBC (MSSQL and any database with an ODBC
// driver) and SQLite. Compiled once by dessa and linked into programs that
// import $(<sql>).
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

// ODBC: always on Windows. Elsewhere only when unixODBC's headers are
// installed, and the library (libodbc) is loaded when the first ODBC
// connection is opened -- so SQLite works on machines without unixODBC.
#if defined(_WIN32) || __has_include(<sql.h>)
#define DESSA_HAS_ODBC 1
#include <sql.h>
#include <sqlext.h>
#else
#define DESSA_HAS_ODBC 0
#endif

#include <memory>

#include "sql.hpp"
#include "sqlite/sqlite3.h"

#if DESSA_HAS_ODBC && !defined(_WIN32)
#include <dlfcn.h>

namespace dessa::sql {

// The ODBC function `name`, from libodbc loaded on first use.
inline void* odbcLibrary() {
    static void* library = [] {
        for (const char* file : {"libodbc.so.2", "libodbc.so", "libodbc.2.dylib", "libodbc.dylib",
                                 "/opt/homebrew/lib/libodbc.2.dylib", "/usr/local/lib/libodbc.2.dylib"}) {
            if (void* handle = dlopen(file, RTLD_NOW | RTLD_GLOBAL)) return handle;
        }
        return static_cast<void*>(nullptr);
    }();
    return library;
}

template <class F>
F odbcFunction(const char* name) {
    void* library = odbcLibrary();
    if (!library) fail("SQL error: ODBC isn't installed (install unixODBC, e.g. `sudo apt install unixodbc` or `brew install unixodbc`)");
    void* function = dlsym(library, name);
    if (!function) fail(Str("SQL error: the ODBC library has no ") + name);
    return reinterpret_cast<F>(function);
}

} // namespace dessa::sql

// Every ODBC call goes through odbcFunction (a function-like macro isn't
// expanded again inside its own expansion, so ::SQLFetch is the declaration).
#define DESSA_ODBC(name) ::dessa::sql::odbcFunction<decltype(&::name)>(#name)
#define SQLAllocHandle(...) DESSA_ODBC(SQLAllocHandle)(__VA_ARGS__)
#define SQLBindParameter(...) DESSA_ODBC(SQLBindParameter)(__VA_ARGS__)
#define SQLDescribeColW(...) DESSA_ODBC(SQLDescribeColW)(__VA_ARGS__)
#define SQLDisconnect(...) DESSA_ODBC(SQLDisconnect)(__VA_ARGS__)
#define SQLDriverConnectW(...) DESSA_ODBC(SQLDriverConnectW)(__VA_ARGS__)
#define SQLDriversW(...) DESSA_ODBC(SQLDriversW)(__VA_ARGS__)
#define SQLExecDirectW(...) DESSA_ODBC(SQLExecDirectW)(__VA_ARGS__)
#define SQLFetch(...) DESSA_ODBC(SQLFetch)(__VA_ARGS__)
#define SQLFreeHandle(...) DESSA_ODBC(SQLFreeHandle)(__VA_ARGS__)
#define SQLGetData(...) DESSA_ODBC(SQLGetData)(__VA_ARGS__)
#define SQLGetDiagRecW(...) DESSA_ODBC(SQLGetDiagRecW)(__VA_ARGS__)
#define SQLMoreResults(...) DESSA_ODBC(SQLMoreResults)(__VA_ARGS__)
#define SQLNumResultCols(...) DESSA_ODBC(SQLNumResultCols)(__VA_ARGS__)
#define SQLRowCount(...) DESSA_ODBC(SQLRowCount)(__VA_ARGS__)
#define SQLSetConnectAttr(...) DESSA_ODBC(SQLSetConnectAttr)(__VA_ARGS__)
#define SQLSetEnvAttr(...) DESSA_ODBC(SQLSetEnvAttr)(__VA_ARGS__)
#endif

namespace dessa::sql {

namespace {

#if DESSA_HAS_ODBC
// ---- UTF-8 <-> UTF-16 (ODBC's wide strings) ----
std::basic_string<SQLWCHAR> widen(const Str& text) {
    std::basic_string<SQLWCHAR> out;
    for (size_t i = 0; i < text.size();) {
        char32_t c = utf8_next(text, i);
        if (c >= 0x10000) {
            c -= 0x10000;
            out += static_cast<SQLWCHAR>(0xD800 + (c >> 10));
            out += static_cast<SQLWCHAR>(0xDC00 + (c & 0x3FF));
        } else {
            out += static_cast<SQLWCHAR>(c);
        }
    }
    return out;
}

Str narrow(const SQLWCHAR* text, size_t length) {
    Str out;
    for (size_t i = 0; i < length; ++i) {
        char32_t c = text[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < length) {
            char32_t low = text[i + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        out += utf8_encode(c);
    }
    return out;
}
#endif

struct Connection {
    bool sqlite = false;
#if DESSA_HAS_ODBC
    SQLHENV env = SQL_NULL_HENV;
    SQLHDBC dbc = SQL_NULL_HDBC;
#endif
    sqlite3* db = nullptr;

    ~Connection() {
        if (db) sqlite3_close(db);
#if DESSA_HAS_ODBC
        if (dbc != SQL_NULL_HDBC) {
            SQLDisconnect(dbc);
            SQLFreeHandle(SQL_HANDLE_DBC, dbc);
        }
        if (env != SQL_NULL_HENV) SQLFreeHandle(SQL_HANDLE_ENV, env);
#endif
    }
};

std::vector<std::unique_ptr<Connection>>& connections() {
    static std::vector<std::unique_ptr<Connection>> all;
    return all;
}

Connection& get(Int handle) {
    auto& all = connections();
    if (handle < 0 || handle >= static_cast<Int>(all.size()) || !all[static_cast<size_t>(handle)]) {
        fail("SQL error: this database connection is closed");
    }
    return *all[static_cast<size_t>(handle)];
}

Int remember(std::unique_ptr<Connection> connection) {
    connections().push_back(std::move(connection));
    return static_cast<Int>(connections().size() - 1);
}

// ---- ODBC ----
#if DESSA_HAS_ODBC
Str odbcError(SQLSMALLINT type, SQLHANDLE handle) {
    Str message;
    SQLWCHAR state[6];
    SQLWCHAR text[1024];
    SQLINTEGER native = 0;
    SQLSMALLINT length = 0;
    for (SQLSMALLINT i = 1; SQLGetDiagRecW(type, handle, i, state, &native, text, 1024, &length) == SQL_SUCCESS; ++i) {
        Str line = narrow(text, static_cast<size_t>(length));
        // "[Microsoft][ODBC Driver 18 for SQL Server][SQL Server]Invalid object name 'x'." -> the last part
        size_t bracket = line.rfind(']');
        if (bracket != Str::npos && bracket + 1 < line.size()) line = line.substr(bracket + 1);
        if (!message.empty()) message += "; ";
        message += line;
    }
    return message.empty() ? "unknown ODBC error" : message;
}

bool ok(SQLRETURN r) { return r == SQL_SUCCESS || r == SQL_SUCCESS_WITH_INFO; }

struct BoundParam {
    SQLBIGINT integer = 0;
    double decimal = 0;
    unsigned char bit = 0;
    std::basic_string<SQLWCHAR> text;
    SQLLEN indicator = 0;
};

// Runs a statement on an ODBC connection; `rows` collects results (or nullptr).
Int odbcRun(Connection& c, const Str& sql, const List<Str>& params, List<Str>* rows) {
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!ok(SQLAllocHandle(SQL_HANDLE_STMT, c.dbc, &stmt))) fail("SQL error: " + odbcError(SQL_HANDLE_DBC, c.dbc));
    struct Guard {
        SQLHSTMT s;
        ~Guard() { SQLFreeHandle(SQL_HANDLE_STMT, s); }
    } guard{stmt};

    std::vector<BoundParam> bound(params.size());
    for (size_t i = 0; i < params.size(); ++i) {
        const Str& p = params[i];
        BoundParam& b = bound[i];
        const char tag = p.empty() ? 'n' : p[0];
        const Str value = p.size() > 2 ? p.substr(2) : "";
        SQLRETURN r;
        const SQLUSMALLINT n = static_cast<SQLUSMALLINT>(i + 1);
        switch (tag) {
            case 'i':
                b.integer = std::stoll(value);
                r = SQLBindParameter(stmt, n, SQL_PARAM_INPUT, SQL_C_SBIGINT, SQL_BIGINT, 0, 0, &b.integer, 0, nullptr);
                break;
            case 'd':
                b.decimal = std::stod(value);
                r = SQLBindParameter(stmt, n, SQL_PARAM_INPUT, SQL_C_DOUBLE, SQL_DOUBLE, 0, 0, &b.decimal, 0, nullptr);
                break;
            case 'b':
                b.bit = value == "1" ? 1 : 0;
                r = SQLBindParameter(stmt, n, SQL_PARAM_INPUT, SQL_C_BIT, SQL_BIT, 0, 0, &b.bit, 0, nullptr);
                break;
            case 's':
                b.text = widen(value);
                b.indicator = static_cast<SQLLEN>(b.text.size() * sizeof(SQLWCHAR));
                r = SQLBindParameter(stmt, n, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                                     b.text.empty() ? 1 : b.text.size(), 0, const_cast<SQLWCHAR*>(b.text.c_str()),
                                     b.indicator, &b.indicator);
                break;
            default:
                b.indicator = SQL_NULL_DATA;
                r = SQLBindParameter(stmt, n, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 1, 0, nullptr, 0, &b.indicator);
                break;
        }
        if (!ok(r)) fail("SQL error binding parameter " + std::to_string(i + 1) + ": " + odbcError(SQL_HANDLE_STMT, stmt));
    }

    auto wide = widen(sql);
    SQLRETURN r = SQLExecDirectW(stmt, const_cast<SQLWCHAR*>(wide.c_str()), SQL_NTS);
    if (r != SQL_NO_DATA && !ok(r)) fail("SQL error: " + odbcError(SQL_HANDLE_STMT, stmt));

    if (!rows) {
        SQLLEN affected = 0;
        if (r == SQL_NO_DATA) return 0;
        SQLRowCount(stmt, &affected);
        return affected < 0 ? 0 : static_cast<Int>(affected);
    }

    // Skip results without columns (e.g. SET NOCOUNT, INSERT before a SELECT)
    SQLSMALLINT columns = 0;
    while (true) {
        SQLNumResultCols(stmt, &columns);
        if (columns > 0) break;
        if (!ok(SQLMoreResults(stmt))) break;
    }
    rows->push_back(std::to_string(columns));
    std::vector<SQLSMALLINT> types(static_cast<size_t>(columns));
    for (SQLSMALLINT col = 1; col <= columns; ++col) {
        SQLWCHAR name[256];
        SQLSMALLINT nameLength = 0, type = 0, digits = 0, nullable = 0;
        SQLULEN size = 0;
        SQLDescribeColW(stmt, col, name, 256, &nameLength, &type, &size, &digits, &nullable);
        types[static_cast<size_t>(col - 1)] = type;
        rows->push_back(narrow(name, static_cast<size_t>(nameLength)));
    }
    while (ok(SQLFetch(stmt))) {
        for (SQLSMALLINT col = 1; col <= columns; ++col) {
            SQLLEN indicator = 0;
            switch (types[static_cast<size_t>(col - 1)]) {
                case SQL_INTEGER: case SQL_SMALLINT: case SQL_TINYINT: case SQL_BIGINT: {
                    SQLBIGINT v = 0;
                    SQLGetData(stmt, col, SQL_C_SBIGINT, &v, 0, &indicator);
                    rows->push_back(indicator == SQL_NULL_DATA ? Str("n:") : "i:" + std::to_string(v));
                    break;
                }
                case SQL_REAL: case SQL_FLOAT: case SQL_DOUBLE: {
                    double v = 0;
                    SQLGetData(stmt, col, SQL_C_DOUBLE, &v, 0, &indicator);
                    rows->push_back(indicator == SQL_NULL_DATA ? Str("n:") : "d:" + to_str(v));
                    break;
                }
                case SQL_BIT: {
                    unsigned char v = 0;
                    SQLGetData(stmt, col, SQL_C_BIT, &v, 0, &indicator);
                    rows->push_back(indicator == SQL_NULL_DATA ? Str("n:") : Str(v ? "b:1" : "b:0"));
                    break;
                }
                default: {
                    // Text (and everything else, e.g. dates and decimals) as text, in chunks
                    Str text;
                    SQLWCHAR buffer[2048];
                    bool isNull = false;
                    while (true) {
                        SQLRETURN g = SQLGetData(stmt, col, SQL_C_WCHAR, buffer, sizeof buffer, &indicator);
                        if (g == SQL_NO_DATA) break;
                        if (!ok(g)) fail("SQL error reading a value: " + odbcError(SQL_HANDLE_STMT, stmt));
                        if (indicator == SQL_NULL_DATA) {
                            isNull = true;
                            break;
                        }
                        size_t chars = (indicator == SQL_NO_TOTAL || indicator >= static_cast<SQLLEN>(sizeof buffer))
                                           ? sizeof buffer / sizeof(SQLWCHAR) - 1
                                           : static_cast<size_t>(indicator) / sizeof(SQLWCHAR);
                        text += narrow(buffer, chars);
                        if (g == SQL_SUCCESS) break;
                    }
                    const SQLSMALLINT type = types[static_cast<size_t>(col - 1)];
                    const bool number = type == SQL_DECIMAL || type == SQL_NUMERIC;
                    rows->push_back(isNull ? Str("n:") : Str(number ? "d:" : "s:") + text);
                    break;
                }
            }
        }
    }
    return 0;
}
#else
[[noreturn]] void noOdbc() {
    std::error_code ec;
    Str cache = (std::filesystem::temp_directory_path(ec) / "dessa" / "objects").string();
    fail("SQL error: ODBC (SQL Server, ...) isn't available: install unixODBC (e.g. `sudo apt install unixodbc-dev` "
         "or `brew install unixodbc`), then delete the sql_* files in " + cache + " so they're rebuilt. "
         "SQLite works without it.");
}
Int odbcRun(Connection&, const Str&, const List<Str>&, List<Str>*) { noOdbc(); }
#endif

// ---- SQLite ----
Int sqliteRun(Connection& c, const Str& sql, const List<Str>& params, List<Str>* rows) {
    const char* tail = sql.c_str();
    Int changes = 0;
    bool first = true;
    while (tail && *tail) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(c.db, tail, -1, &stmt, &tail) != SQLITE_OK) {
            fail(Str("SQL error: ") + sqlite3_errmsg(c.db));
        }
        if (!stmt) continue; // only whitespace or a comment was left
        struct Guard {
            sqlite3_stmt* s;
            ~Guard() { sqlite3_finalize(s); }
        } guard{stmt};

        if (first) {
            for (size_t i = 0; i < params.size(); ++i) {
                const Str& p = params[i];
                const char tag = p.empty() ? 'n' : p[0];
                const Str value = p.size() > 2 ? p.substr(2) : "";
                const int n = static_cast<int>(i + 1);
                switch (tag) {
                    case 'i': sqlite3_bind_int64(stmt, n, std::stoll(value)); break;
                    case 'd': sqlite3_bind_double(stmt, n, std::stod(value)); break;
                    case 'b': sqlite3_bind_int(stmt, n, value == "1"); break;
                    case 's': sqlite3_bind_text(stmt, n, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT); break;
                    default: sqlite3_bind_null(stmt, n); break;
                }
            }
        }

        const int columns = sqlite3_column_count(stmt);
        const bool collect = rows && columns > 0 && rows->empty();
        if (collect) {
            rows->push_back(std::to_string(columns));
            for (int col = 0; col < columns; ++col) rows->push_back(sqlite3_column_name(stmt, col));
        }
        int step;
        while ((step = sqlite3_step(stmt)) == SQLITE_ROW) {
            if (!collect) continue;
            for (int col = 0; col < columns; ++col) {
                switch (sqlite3_column_type(stmt, col)) {
                    case SQLITE_INTEGER: rows->push_back("i:" + std::to_string(sqlite3_column_int64(stmt, col))); break;
                    case SQLITE_FLOAT: rows->push_back("d:" + to_str(sqlite3_column_double(stmt, col))); break;
                    case SQLITE_NULL: rows->push_back("n:"); break;
                    default: {
                        const unsigned char* text = sqlite3_column_text(stmt, col);
                        rows->push_back("s:" + Str(text ? reinterpret_cast<const char*>(text) : ""));
                    }
                }
            }
        }
        if (step != SQLITE_DONE) fail(Str("SQL error: ") + sqlite3_errmsg(c.db));
        changes += sqlite3_changes(c.db);
        first = false;
    }
    if (rows && rows->empty()) rows->push_back("0");
    return changes;
}

} // namespace

List<Str> _odbcDrivers() {
    List<Str> drivers;
#if DESSA_HAS_ODBC && !defined(_WIN32)
    if (!odbcLibrary()) return drivers; // no unixODBC: no drivers
#endif
#if DESSA_HAS_ODBC
    SQLHENV env = SQL_NULL_HENV;
    if (!ok(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env))) return drivers;
    SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0);
    SQLWCHAR name[256], attributes[1024];
    SQLSMALLINT nameLength = 0, attributesLength = 0;
    SQLUSMALLINT direction = SQL_FETCH_FIRST;
    while (ok(SQLDriversW(env, direction, name, 256, &nameLength, attributes, 1024, &attributesLength))) {
        drivers.push_back(narrow(name, static_cast<size_t>(nameLength)));
        direction = SQL_FETCH_NEXT;
    }
    SQLFreeHandle(SQL_HANDLE_ENV, env);
#endif
    return drivers;
}

Int _odbcOpen(const Str& connection) {
#if !DESSA_HAS_ODBC
    (void)connection;
    noOdbc();
#else
    auto c = std::make_unique<Connection>();
    if (!ok(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &c->env))) fail("SQL error: could not start ODBC");
    SQLSetEnvAttr(c->env, SQL_ATTR_ODBC_VERSION, reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0);
    if (!ok(SQLAllocHandle(SQL_HANDLE_DBC, c->env, &c->dbc))) fail("SQL error: " + odbcError(SQL_HANDLE_ENV, c->env));
    SQLSetConnectAttr(c->dbc, SQL_LOGIN_TIMEOUT, reinterpret_cast<SQLPOINTER>(10), 0);
    auto wide = widen(connection);
    SQLRETURN r = SQLDriverConnectW(c->dbc, nullptr, const_cast<SQLWCHAR*>(wide.c_str()), SQL_NTS, nullptr, 0, nullptr,
                                    SQL_DRIVER_NOPROMPT);
    if (!ok(r)) fail("Could not connect: " + odbcError(SQL_HANDLE_DBC, c->dbc));
    return remember(std::move(c));
#endif
}

Int _sqliteOpen(const Str& path) {
    auto c = std::make_unique<Connection>();
    c->sqlite = true;
    if (sqlite3_open_v2(path.c_str(), &c->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        Str message = c->db ? sqlite3_errmsg(c->db) : "out of memory";
        fail("Could not open SQLite database '" + path + "': " + message);
    }
    return remember(std::move(c));
}

List<Str> _query(Int handle, const Str& sql, const List<Str>& params) {
    Connection& c = get(handle);
    List<Str> rows;
    if (c.sqlite) sqliteRun(c, sql, params, &rows);
    else odbcRun(c, sql, params, &rows);
    return rows;
}

Int _execute(Int handle, const Str& sql, const List<Str>& params) {
    Connection& c = get(handle);
    return c.sqlite ? sqliteRun(c, sql, params, nullptr) : odbcRun(c, sql, params, nullptr);
}

void _close(Int handle) {
    auto& all = connections();
    if (handle >= 0 && handle < static_cast<Int>(all.size())) all[static_cast<size_t>(handle)].reset();
}

} // namespace dessa::sql
