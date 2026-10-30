/* Native part of $("sql") in C, for C programs and WebAssembly
 * (dsx_sql_<name>, see src/codegen/cglue.hpp); sql.cpp is the same for the
 * C++ backend. ODBC (SQL Server and any database with an ODBC driver) and
 * SQLite. WebAssembly has SQLite only.
 *
 * Parameters and cells travel as tagged text:
 *   "i:42"  "d:2.5"  "s:text"  "b:1"  "n:"  (Int, Dbl, Str, Bul, nil) */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* ODBC: always on Windows. Elsewhere only when unixODBC's headers are
 * installed, and the library (libodbc) is loaded when the first ODBC
 * connection is opened -- so SQLite works on machines without unixODBC. */
#if defined(__wasm__)
#define DESSA_HAS_ODBC 0
#elif defined(_WIN32) || __has_include(<sql.h>)
#define DESSA_HAS_ODBC 1
#include <sql.h>
#include <sqlext.h>
#else
#define DESSA_HAS_ODBC 0
#endif

#include "dessa_rt.h"
#include "sqlite/sqlite3.h"

#if DESSA_HAS_ODBC && !defined(_WIN32)
#include <dlfcn.h>

/* The ODBC function `name`, from libodbc loaded on first use */
static void* odbc_library(void) {
    static bool tried = false;
    static void* library = NULL;
    if (!tried) {
        tried = true;
        const char* files[] = {"libodbc.so.2", "libodbc.so", "libodbc.2.dylib", "libodbc.dylib",
                               "/opt/homebrew/lib/libodbc.2.dylib", "/usr/local/lib/libodbc.2.dylib"};
        for (size_t i = 0; i < sizeof files / sizeof *files && !library; ++i) library = dlopen(files[i], RTLD_NOW | RTLD_GLOBAL);
    }
    return library;
}

static void* odbc_function(const char* name) {
    void* library = odbc_library();
    if (!library) ds_fail("SQL error: ODBC isn't installed (install unixODBC, e.g. `sudo apt install unixodbc` or `brew install unixodbc`)");
    void* function = dlsym(library, name);
    if (!function) ds_fail("SQL error: the ODBC library has no %s", name);
    return function;
}

/* Every ODBC call goes through odbc_function (a macro isn't expanded again
 * inside its own expansion, so &name there is the declaration) */
#define DESSA_ODBC(name) ((__typeof__(&name))odbc_function(#name))
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

static void push_text(ds_list* list, ds_str* text) {
    if (list->len == list->cap) ds_list_grow(list);
    ((ds_str**)list->data)[list->len++] = text;
}

/* "<tag>:<text>" as a new Str */
static ds_str* tagged(char tag, const char* text, int64_t len) {
    ds_sb b;
    ds_sb_init(&b);
    char head[2] = {tag, ':'};
    ds_sb_bytes(&b, head, 2);
    ds_sb_bytes(&b, text, len);
    return ds_sb_done(&b);
}
static ds_str* tagged_int(char tag, int64_t v) {
    char buffer[32];
    int n = snprintf(buffer, sizeof buffer, "%lld", (long long)v);
    return tagged(tag, buffer, n);
}
static ds_str* tagged_dbl(double v) {
    ds_str* s = ds_dbl_str(v);
    ds_str* t = tagged('d', s->data, s->len);
    ds_release(s);
    return t;
}

/* A parameter: its tag and the text after "x:" */
static char param_tag(ds_str* p) { return p->len ? p->data[0] : 'n'; }
static const char* param_text(ds_str* p) { return p->len > 2 ? p->data + 2 : ""; }
static int64_t param_len(ds_str* p) { return p->len > 2 ? p->len - 2 : 0; }

typedef struct connection {
    bool open, sqlite;
#if DESSA_HAS_ODBC
    SQLHENV env;
    SQLHDBC dbc;
#endif
    sqlite3* db;
} connection;

static connection* connections = NULL;
static int64_t connectionCount = 0, connectionCap = 0;

static connection* get(ds_int handle) {
    if (handle < 0 || handle >= connectionCount || !connections[handle].open) ds_fail("SQL error: this database connection is closed");
    return &connections[handle];
}

static ds_int remember(connection c) {
    if (connectionCount == connectionCap) {
        connectionCap = connectionCap ? connectionCap * 2 : 4;
        connections = (connection*)realloc(connections, (size_t)connectionCap * sizeof(connection));
    }
    c.open = true;
    connections[connectionCount] = c;
    return connectionCount++;
}

static void close_connection(connection* c) {
    if (!c->open) return;
    if (c->db) sqlite3_close(c->db);
#if DESSA_HAS_ODBC
    if (c->dbc != SQL_NULL_HDBC) {
        SQLDisconnect(c->dbc);
        SQLFreeHandle(SQL_HANDLE_DBC, c->dbc);
    }
    if (c->env != SQL_NULL_HENV) SQLFreeHandle(SQL_HANDLE_ENV, c->env);
#endif
    c->open = false;
}

/* ---- ODBC ---- */
#if DESSA_HAS_ODBC

/* UTF-8 -> UTF-16 (ODBC's wide strings), 0-terminated; free it */
static SQLWCHAR* widen(const char* text, int64_t len, int64_t* count) {
    SQLWCHAR* out = (SQLWCHAR*)malloc(((size_t)len * 2 + 1) * sizeof(SQLWCHAR));
    int64_t n = 0;
    for (int64_t i = 0; i < len;) {
        unsigned char b = (unsigned char)text[i];
        uint32_t c;
        int extra = b < 0x80 ? 0 : b < 0xE0 ? 1 : b < 0xF0 ? 2 : 3;
        c = extra == 0 ? b : extra == 1 ? (b & 0x1F) : extra == 2 ? (b & 0x0F) : (b & 0x07);
        ++i;
        for (int k = 0; k < extra && i < len; ++k, ++i) c = (c << 6) | ((unsigned char)text[i] & 0x3F);
        if (c >= 0x10000) {
            c -= 0x10000;
            out[n++] = (SQLWCHAR)(0xD800 + (c >> 10));
            out[n++] = (SQLWCHAR)(0xDC00 + (c & 0x3FF));
        } else {
            out[n++] = (SQLWCHAR)c;
        }
    }
    out[n] = 0;
    if (count) *count = n;
    return out;
}

static void narrow_into(ds_sb* b, const SQLWCHAR* text, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        uint32_t c = text[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < length) {
            uint32_t low = text[i + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        ds_sb_chr(b, (ds_chr)c);
    }
}

static ds_str* narrow(const SQLWCHAR* text, size_t length) {
    ds_sb b;
    ds_sb_init(&b);
    narrow_into(&b, text, length);
    return ds_sb_done(&b);
}

static bool ok(SQLRETURN r) { return r == SQL_SUCCESS || r == SQL_SUCCESS_WITH_INFO; }

/* The driver's messages, each without its "[Microsoft][ODBC Driver ...]" prefix (owned) */
static ds_str* odbc_error(SQLSMALLINT type, SQLHANDLE handle) {
    ds_sb b;
    ds_sb_init(&b);
    bool any = false;
    SQLWCHAR state[6], text[1024];
    SQLINTEGER native = 0;
    SQLSMALLINT length = 0;
    for (SQLSMALLINT i = 1; SQLGetDiagRecW(type, handle, i, state, &native, text, 1024, &length) == SQL_SUCCESS; ++i) {
        size_t start = 0;
        for (size_t k = 0; k < (size_t)length; ++k) {
            if (text[k] == ']' && k + 1 < (size_t)length) start = k + 1;
        }
        if (any) ds_sb_cstr(&b, "; ");
        narrow_into(&b, text + start, (size_t)length - start);
        any = true;
    }
    if (!any) ds_sb_cstr(&b, "unknown ODBC error");
    return ds_sb_done(&b);
}

DS_NORETURN static void odbc_fail(const char* prefix, SQLSMALLINT type, SQLHANDLE handle, SQLHSTMT freeIt) {
    ds_str* message = odbc_error(type, handle);
    if (freeIt != SQL_NULL_HSTMT) SQLFreeHandle(SQL_HANDLE_STMT, freeIt);
    ds_fail("%s%s", prefix, message->data);
}

typedef struct bound_param {
    SQLBIGINT integer;
    double decimal;
    unsigned char bit;
    SQLWCHAR* text;
    SQLLEN indicator;
} bound_param;

/* Runs a statement on an ODBC connection; `rows` collects results (or NULL) */
static ds_int odbc_run(connection* c, ds_str* sql, ds_list* params, ds_list* rows) {
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!ok(SQLAllocHandle(SQL_HANDLE_STMT, c->dbc, &stmt))) odbc_fail("SQL error: ", SQL_HANDLE_DBC, c->dbc, SQL_NULL_HSTMT);

    const int64_t count = params->len;
    bound_param* bound = (bound_param*)calloc((size_t)(count ? count : 1), sizeof(bound_param));
    for (int64_t i = 0; i < count; ++i) {
        ds_str* p = ((ds_str**)params->data)[i];
        bound_param* b = &bound[i];
        const SQLUSMALLINT n = (SQLUSMALLINT)(i + 1);
        SQLRETURN r;
        switch (param_tag(p)) {
            case 'i':
                b->integer = strtoll(param_text(p), NULL, 10);
                r = SQLBindParameter(stmt, n, SQL_PARAM_INPUT, SQL_C_SBIGINT, SQL_BIGINT, 0, 0, &b->integer, 0, NULL);
                break;
            case 'd':
                b->decimal = strtod(param_text(p), NULL);
                r = SQLBindParameter(stmt, n, SQL_PARAM_INPUT, SQL_C_DOUBLE, SQL_DOUBLE, 0, 0, &b->decimal, 0, NULL);
                break;
            case 'b':
                b->bit = param_text(p)[0] == '1';
                r = SQLBindParameter(stmt, n, SQL_PARAM_INPUT, SQL_C_BIT, SQL_BIT, 0, 0, &b->bit, 0, NULL);
                break;
            case 's': {
                int64_t chars = 0;
                b->text = widen(param_text(p), param_len(p), &chars);
                b->indicator = (SQLLEN)(chars * (int64_t)sizeof(SQLWCHAR));
                r = SQLBindParameter(stmt, n, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, chars ? (SQLULEN)chars : 1, 0, b->text,
                                     b->indicator, &b->indicator);
                break;
            }
            default:
                b->indicator = SQL_NULL_DATA;
                r = SQLBindParameter(stmt, n, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 1, 0, NULL, 0, &b->indicator);
                break;
        }
        if (!ok(r)) {
            char prefix[64];
            snprintf(prefix, sizeof prefix, "SQL error binding parameter %lld: ", (long long)i + 1);
            odbc_fail(prefix, SQL_HANDLE_STMT, stmt, stmt);
        }
    }

    SQLWCHAR* wide = widen(sql->data, sql->len, NULL);
    SQLRETURN r = SQLExecDirectW(stmt, wide, SQL_NTS);
    free(wide);
    for (int64_t i = 0; i < count; ++i) free(bound[i].text);   /* bound only until the statement ran */
    free(bound);
    if (r != SQL_NO_DATA && !ok(r)) odbc_fail("SQL error: ", SQL_HANDLE_STMT, stmt, stmt);

    if (!rows) {
        SQLLEN affected = 0;
        if (r != SQL_NO_DATA) SQLRowCount(stmt, &affected);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return affected < 0 ? 0 : (ds_int)affected;
    }

    /* Skip results without columns (e.g. SET NOCOUNT, INSERT before a SELECT) */
    SQLSMALLINT columns = 0;
    for (;;) {
        SQLNumResultCols(stmt, &columns);
        if (columns > 0) break;
        if (!ok(SQLMoreResults(stmt))) break;
    }
    push_text(rows, ds_int_str(columns));
    SQLSMALLINT* types = (SQLSMALLINT*)calloc((size_t)(columns ? columns : 1), sizeof(SQLSMALLINT));
    for (SQLSMALLINT col = 1; col <= columns; ++col) {
        SQLWCHAR name[256];
        SQLSMALLINT nameLength = 0, type = 0, digits = 0, nullable = 0;
        SQLULEN size = 0;
        SQLDescribeColW(stmt, col, name, 256, &nameLength, &type, &size, &digits, &nullable);
        types[col - 1] = type;
        push_text(rows, narrow(name, (size_t)nameLength));
    }
    while (ok(SQLFetch(stmt))) {
        for (SQLSMALLINT col = 1; col <= columns; ++col) {
            SQLLEN indicator = 0;
            switch (types[col - 1]) {
                case SQL_INTEGER: case SQL_SMALLINT: case SQL_TINYINT: case SQL_BIGINT: {
                    SQLBIGINT v = 0;
                    SQLGetData(stmt, col, SQL_C_SBIGINT, &v, 0, &indicator);
                    push_text(rows, indicator == SQL_NULL_DATA ? ds_str_cstr("n:") : tagged_int('i', (int64_t)v));
                    break;
                }
                case SQL_REAL: case SQL_FLOAT: case SQL_DOUBLE: {
                    double v = 0;
                    SQLGetData(stmt, col, SQL_C_DOUBLE, &v, 0, &indicator);
                    push_text(rows, indicator == SQL_NULL_DATA ? ds_str_cstr("n:") : tagged_dbl(v));
                    break;
                }
                case SQL_BIT: {
                    unsigned char v = 0;
                    SQLGetData(stmt, col, SQL_C_BIT, &v, 0, &indicator);
                    push_text(rows, ds_str_cstr(indicator == SQL_NULL_DATA ? "n:" : v ? "b:1" : "b:0"));
                    break;
                }
                default: {
                    /* Text (and everything else, e.g. dates and decimals) as text, in chunks */
                    const SQLSMALLINT type = types[col - 1];
                    const bool number = type == SQL_DECIMAL || type == SQL_NUMERIC;
                    ds_sb text;
                    ds_sb_init(&text);
                    ds_sb_cstr(&text, number ? "d:" : "s:");
                    SQLWCHAR buffer[2048];
                    bool isNull = false;
                    for (;;) {
                        SQLRETURN g = SQLGetData(stmt, col, SQL_C_WCHAR, buffer, sizeof buffer, &indicator);
                        if (g == SQL_NO_DATA) break;
                        if (!ok(g)) {
                            free(types);
                            ds_release(ds_sb_done(&text));
                            odbc_fail("SQL error reading a value: ", SQL_HANDLE_STMT, stmt, stmt);
                        }
                        if (indicator == SQL_NULL_DATA) {
                            isNull = true;
                            break;
                        }
                        size_t chars = (indicator == SQL_NO_TOTAL || indicator >= (SQLLEN)sizeof buffer)
                                           ? sizeof buffer / sizeof(SQLWCHAR) - 1
                                           : (size_t)indicator / sizeof(SQLWCHAR);
                        narrow_into(&text, buffer, chars);
                        if (g == SQL_SUCCESS) break;
                    }
                    ds_str* value = ds_sb_done(&text);
                    if (isNull) {
                        ds_release(value);
                        value = ds_str_cstr("n:");
                    }
                    push_text(rows, value);
                    break;
                }
            }
        }
    }
    free(types);
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return 0;
}

#else

DS_NORETURN static void no_odbc(void) {
#ifdef __wasm__
    ds_fail("SQL error: SQL Server and other ODBC databases aren't available in WebAssembly; SQLite works");
#else
    const char* tmp = getenv("TMPDIR");
    ds_fail("SQL error: ODBC (SQL Server, ...) isn't available: install unixODBC (e.g. `sudo apt install unixodbc-dev` "
            "or `brew install unixodbc`), then delete the sql_* files in %s/dessa/objects so they're rebuilt. "
            "SQLite works without it.",
            tmp ? tmp : "/tmp");
#endif
}

static ds_int odbc_run(connection* c, ds_str* sql, ds_list* params, ds_list* rows) {
    (void)c, (void)sql, (void)params, (void)rows;
    no_odbc();
}

#endif

/* ---- SQLite ---- */

DS_NORETURN static void sqlite_fail(connection* c, sqlite3_stmt* stmt) {
    ds_str* message = ds_str_cstr(sqlite3_errmsg(c->db));
    if (stmt) sqlite3_finalize(stmt);
    ds_fail("SQL error: %s", message->data);
}

static ds_int sqlite_run(connection* c, ds_str* sql, ds_list* params, ds_list* rows) {
    const char* tail = sql->data;
    ds_int changes = 0;
    bool first = true;
    while (tail && *tail) {
        sqlite3_stmt* stmt = NULL;
        if (sqlite3_prepare_v2(c->db, tail, -1, &stmt, &tail) != SQLITE_OK) sqlite_fail(c, stmt);
        if (!stmt) continue;   /* only whitespace or a comment was left */

        if (first) {
            for (int64_t i = 0; i < params->len; ++i) {
                ds_str* p = ((ds_str**)params->data)[i];
                const int n = (int)(i + 1);
                switch (param_tag(p)) {
                    case 'i': sqlite3_bind_int64(stmt, n, strtoll(param_text(p), NULL, 10)); break;
                    case 'd': sqlite3_bind_double(stmt, n, strtod(param_text(p), NULL)); break;
                    case 'b': sqlite3_bind_int(stmt, n, param_text(p)[0] == '1' && param_len(p) == 1); break;
                    case 's': sqlite3_bind_text(stmt, n, param_text(p), (int)param_len(p), SQLITE_TRANSIENT); break;
                    default: sqlite3_bind_null(stmt, n); break;
                }
            }
        }

        const int columns = sqlite3_column_count(stmt);
        const bool collect = rows && columns > 0 && rows->len == 0;
        if (collect) {
            push_text(rows, ds_int_str(columns));
            for (int col = 0; col < columns; ++col) push_text(rows, ds_str_cstr(sqlite3_column_name(stmt, col)));
        }
        int step;
        while ((step = sqlite3_step(stmt)) == SQLITE_ROW) {
            if (!collect) continue;
            for (int col = 0; col < columns; ++col) {
                switch (sqlite3_column_type(stmt, col)) {
                    case SQLITE_INTEGER: push_text(rows, tagged_int('i', sqlite3_column_int64(stmt, col))); break;
                    case SQLITE_FLOAT: push_text(rows, tagged_dbl(sqlite3_column_double(stmt, col))); break;
                    case SQLITE_NULL: push_text(rows, ds_str_cstr("n:")); break;
                    default: {
                        const unsigned char* text = sqlite3_column_text(stmt, col);
                        const int bytes = sqlite3_column_bytes(stmt, col);
                        push_text(rows, tagged('s', text ? (const char*)text : "", text ? bytes : 0));
                    }
                }
            }
        }
        if (step != SQLITE_DONE) sqlite_fail(c, stmt);
        sqlite3_finalize(stmt);
        changes += sqlite3_changes(c->db);
        first = false;
    }
    if (rows && rows->len == 0) push_text(rows, ds_str_cstr("0"));
    return changes;
}

/* ---- the library ---- */

ds_list* dsx_sql__odbcDrivers(void) {
    ds_list* drivers = ds_list_new(&ds_elem_str, 4);
#if DESSA_HAS_ODBC
#ifndef _WIN32
    if (!odbc_library()) return drivers;   /* no unixODBC: no drivers */
#endif
    SQLHENV env = SQL_NULL_HENV;
    if (!ok(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env))) return drivers;
    SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    SQLWCHAR name[256], attributes[1024];
    SQLSMALLINT nameLength = 0, attributesLength = 0;
    SQLUSMALLINT direction = SQL_FETCH_FIRST;
    while (ok(SQLDriversW(env, direction, name, 256, &nameLength, attributes, 1024, &attributesLength))) {
        push_text(drivers, narrow(name, (size_t)nameLength));
        direction = SQL_FETCH_NEXT;
    }
    SQLFreeHandle(SQL_HANDLE_ENV, env);
#endif
    return drivers;
}

ds_int dsx_sql__odbcOpen(ds_str* text) {
#if !DESSA_HAS_ODBC
    (void)text;
    no_odbc();
#else
    connection c;
    memset(&c, 0, sizeof c);
    if (!ok(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &c.env))) ds_fail("SQL error: could not start ODBC");
    SQLSetEnvAttr(c.env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    if (!ok(SQLAllocHandle(SQL_HANDLE_DBC, c.env, &c.dbc))) odbc_fail("SQL error: ", SQL_HANDLE_ENV, c.env, SQL_NULL_HSTMT);
    SQLSetConnectAttr(c.dbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)10, 0);
    SQLWCHAR* wide = widen(text->data, text->len, NULL);
    SQLRETURN r = SQLDriverConnectW(c.dbc, NULL, wide, SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT);
    free(wide);
    if (!ok(r)) odbc_fail("Could not connect: ", SQL_HANDLE_DBC, c.dbc, SQL_NULL_HSTMT);
    return remember(c);
#endif
}

ds_int dsx_sql__sqliteOpen(ds_str* path) {
    connection c;
    memset(&c, 0, sizeof c);
    c.sqlite = true;
    if (sqlite3_open_v2(path->data, &c.db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
        ds_str* message = ds_str_cstr(c.db ? sqlite3_errmsg(c.db) : "out of memory");
        if (c.db) sqlite3_close(c.db);
        ds_fail("Could not open SQLite database '%s': %s", path->data, message->data);
    }
    return remember(c);
}

/* [columnCount, column names..., cells row by row...] */
ds_list* dsx_sql__query(ds_int handle, ds_str* sql, ds_list* params) {
    connection* c = get(handle);
    ds_list* rows = ds_list_new(&ds_elem_str, 16);
    if (c->sqlite) sqlite_run(c, sql, params, rows);
    else odbc_run(c, sql, params, rows);
    return rows;
}

ds_int dsx_sql__execute(ds_int handle, ds_str* sql, ds_list* params) {
    connection* c = get(handle);
    return c->sqlite ? sqlite_run(c, sql, params, NULL) : odbc_run(c, sql, params, NULL);
}

void dsx_sql__close(ds_int handle) {
    if (handle >= 0 && handle < connectionCount) close_connection(&connections[handle]);
}
