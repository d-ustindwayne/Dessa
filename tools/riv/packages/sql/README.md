# sql

Databases: SQLite, built in, and SQL Server or anything else with an ODBC driver.

**Install:** Install with `riv add sql`.

**Runs in:** Native programs: SQLite and ODBC. WebAssembly: SQLite only.

## Example

```dessa
$("sql");

class Item {
    var[Str] name = "";
    var[Dbl] price = 0.0;
}

var[Database] db = openSqlite("shop.db");
db.execute("CREATE TABLE IF NOT EXISTS items (name TEXT, price REAL)");
db.execute("INSERT INTO items VALUES (?, ?)", "Mouse", 25.99);

for (var[Obj] row in db.queryAll("SELECT * FROM items WHERE price < ?", 50)) {
    print(row.name, row.price);
}
var[Item<>] items = db.queryAllAs:Item("SELECT * FROM items");
print(items.length());
```

## What it has

| Method | Gives |
|---|---|
| `queryAll(sql, ...)` | every row, as objects (`row.name`) |
| `query(sql, ...)` | the first row, or nil |
| `queryAllAs:T(sql, ...)` | every row, as objects of your class `T` |
| `queryAs:T(sql, ...)` | the first row as your class, or nil |
| `execute(sql, ...)` | how many rows changed |

| Connecting | |
|---|---|
| `openSqlite(path)` | a SQLite file |
| `connectMssql(connectionString)` | SQL Server |
| `connect(connectionString)` | any ODBC connection string |
| `drivers()` | the installed ODBC drivers |

Both ODBC forms accept `TrustCertificate=yes` as a shorter `TrustServerCertificate=yes`.

## Good to know

Values passed after the SQL fill the `?` places safely. Use them for anything that
comes from a user, instead of building the SQL text.

Columns fill the fields with the same name (case and `_` don't matter, so `first_name`
fills `firstName`). The class needs an `init()` without arguments, or no `init`. 0/1
columns fill `Bul` fields, and a column that doesn't fit its field is an error naming
the column.
