// $("sql") -- native part: talking to databases. The friendly API
// (Database, connect, openSqlite, ...) is written in Dessa in sql.dm.
//
// The implementation lives in sql.cpp, which is compiled once and linked in,
// so that <windows.h> and the ODBC headers never touch your program.
#pragma once
#include "dessa_runtime.hpp"

namespace dessa::sql {

// Parameters and cells travel as tagged text:
//   "i:42"  "d:2.5"  "s:text"  "b:1"  "n:"  (Int, Dbl, Str, Bul, nil)

List<Str> _odbcDrivers();
Int _odbcOpen(const Str& connection);
Int _sqliteOpen(const Str& path);
// [columnCount, column names..., cells row by row...]
List<Str> _query(Int handle, const Str& sql, const List<Str>& params);
Int _execute(Int handle, const Str& sql, const List<Str>& params);
void _close(Int handle);

} // namespace dessa::sql
