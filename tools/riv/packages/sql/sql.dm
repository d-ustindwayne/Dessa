/***
 * $("sql") -- databases: Microsoft SQL Server (and anything else with an
 * ODBC driver: MySQL, PostgreSQL, ...) and SQLite.
 *
 *     $("sql") { connectMssql, openSqlite }
 *
 *     var[Database] db = connectMssql("Server=localhost;Database=shop;Trusted_Connection=yes;");
 *     var[Database] db2 = connectMssql("Server=school;Database=shop;User Id=student;Password=secret;");
 *     var[Database] local = openSqlite("app.db");                 // a file (":memory:" = temporary)
 *
 *     for (var[Obj] row in db.queryAll("SELECT name, price FROM products WHERE price > ?", 10)) {
 *         print(row.name, row.price);
 *     }
 *     var[Obj] first = db.query("SELECT name FROM products WHERE id = ?", 3);   // one row, or nil
 *     var[Int] changed = db.execute("UPDATE products SET price = ? WHERE id = ?", 9.99, 3);
 *     db.close();
 *
 * `?` in the SQL is filled from the extra arguments, in order. Values are
 * sent separately from the SQL text, so they can't break the query (no SQL
 * injection). Rows come back as objects (Obj): row.name. To get your own
 * classes instead, use queryAs / queryAllAs.
 */

$("sql") { _odbcDrivers, _odbcOpen, _sqliteOpen, _query, _execute, _close }   // native part

class SqlError {
    var[Str] message = "";
    init(message[Str]) {
        this.message = message;
    }
    func[Str] toStr() {
        return $"SqlError: {this.message}";
    }
}

// A query parameter as tagged text for the native part
func[Str] _tag(value[Int | Dbl | Str | Bul | Nil]) {
    if (value == nil) return "n:";
    if (value is Int) return $"i:{value}";
    if (value is Dbl) return $"d:{value}";
    if (value is Bul) return value ? "b:1" : "b:0";
    if (value is Str) return $"s:{value}";
    return "n:";
}

// A cell as a value of its own type (for queryAs)
func[Obj] _cellValue(text[Str]) {
    var[Str] value = text.substr(2, text.length() - 2);
    switch (text.substr(0, 1)) {
        case "i" := { return Int(value); }
        case "d" := { return Dbl(value); }
        case "b" := { return value == "1"; }
        case "s" := { return value; }
        default := { return nil; }
    }
}

// "first_name", "FirstName" and "firstname" are the same name
func[Str] _nameKey(name[Str]) {
    return name.replaceAll("_", "").lower();
}

// For each column, the field it fills ("" if none)
func[Str<>] _matchFields(columns[Str<>], fields[Str<>]) {
    var[Map[Str, Str]] byKey = {:};
    for (var[Str] f in fields) byKey<_nameKey(f)> = f;
    var[Str<>] out = [];
    for (var[Str] c in columns) out.push(byKey.getOr(_nameKey(c), ""));
    return out;
}

// Sets a field, converting where it's safe: 0/1 into a Bul (SQLite has no
// booleans), a whole decimal into an Int, anything into a Str
func[Emp] _fill(item[Obj], field[Str], value[Obj], column[Str]) {
    try {
        setField(item, field, value);
        return;
    } catch (e[Str]) {
        var[Obj] current = getField(item, field);
        try {
            if (current is Bul && value is Int) {
                setField(item, field, Int(value) != 0);
                return;
            }
            if (current is Int && value is Dbl && Dbl(value) == Dbl(Int(value))) {
                setField(item, field, Int(value));
                return;
            }
            if (current is Str && value != nil) {
                setField(item, field, Str(value));
                return;
            }
        } catch (again[Str]) {}
        throw new SqlError($"column '{column}': {e}");
    }
}

class Database {
    var[Int] handle = -1;
    var[Str] kind = "";          // "odbc" or "sqlite"

    func[Str<>] _params(values[(Int | Dbl | Str | Bul | Nil)<>]) {
        var[Str<>] tagged = [];
        for (var[Int | Dbl | Str | Bul | Nil] v in values) tagged.push(_tag(v));
        return tagged;
    }

    // Runs a SELECT; the column names and the cells (tagged text) of every row
    func[Str<>] _select(sql[Str], params[(Int | Dbl | Str | Bul | Nil)<>]) {
        try {
            return _query(this.handle, sql, this._params(params));
        } catch (e[Str]) {
            throw new SqlError(e);
        }
    }

    // Every row of a SELECT as an object: row.name, row.price
    //
    //     for (var[Obj] row in db.queryAll("SELECT name, price FROM products")) print(row.name);
    func[Obj<>] queryAll(sql[Str], params[(Int | Dbl | Str | Bul | Nil)*]) {
        var[Str<>] raw = this._select(sql, params);
        var[Int] columns = Int(raw<0>);
        var[Obj<>] rows = [];
        var[Int] at = columns + 1;
        while (at + columns <= raw.length() && columns > 0) {
            var[Obj] row = new Obj();
            for (var[Int] c = 0; c < columns; c++) setField(row, raw<1 + c>, _cellValue(raw<at + c>));
            rows.push(row);
            at += columns;
        }
        return rows;
    }

    // The first row of a SELECT as an object, or nil if there is none.
    func[Obj] query(sql[Str], params[(Int | Dbl | Str | Bul | Nil)*]) {
        var[Obj<>] rows = this.queryAll(sql, ...params);
        if (rows.isEmpty()) return nil;
        return rows<0>;
    }

    // Every row of a SELECT, each put in a new object made by `make`: the
    // columns go into the fields with the same name (case and _ don't matter,
    // so first_name fills firstName). Columns without a field are left out.
    //
    //     class Person { var[Int] id = 0; var[Str] name = ""; }
    //     for (var[Person] p in db.queryAllAs(() := new Person(), "SELECT * FROM people")) print(p.name);
    func[Obj<>] queryAllAs(make[Func[Obj]], sql[Str], params[(Int | Dbl | Str | Bul | Nil)*]) {
        var[Str<>] raw = this._select(sql, params);
        var[Int] columns = Int(raw<0>);
        var[Str<>] names = raw.slice(1, columns + 1);
        var[Obj<>] items = [];
        var[Str<>] fieldFor = [];     // column -> field name ("" if none)
        var[Int] at = columns + 1;
        while (at + columns <= raw.length() && columns > 0) {
            var[Obj] item = make();
            if (fieldFor.isEmpty()) fieldFor = _matchFields(names, fieldsOf(item));
            for (var[Int] c = 0; c < columns; c++) {
                if (!fieldFor<c>.isEmpty()) _fill(item, fieldFor<c>, _cellValue(raw<at + c>), names<c>);
            }
            items.push(item);
            at += columns;
        }
        return items;
    }

    // The first row as an object of your class (see queryAllAs), or nil if there is none.
    //
    //     var[Obj] one = db.queryAs(() := new Person(), "SELECT * FROM people WHERE id = ?", 7);
    //     if (one is Person) print(one.name);
    func[Obj] queryAs(make[Func[Obj]], sql[Str], params[(Int | Dbl | Str | Bul | Nil)*]) {
        var[Obj<>] items = this.queryAllAs(make, sql, ...params);
        if (items.isEmpty()) return nil;
        return items<0>;
    }

    // INSERT / UPDATE / DELETE / CREATE ...; returns how many rows changed.
    // With SQLite, several statements separated by ; can run at once.
    func[Int] execute(sql[Str], params[(Int | Dbl | Str | Bul | Nil)*]) {
        try {
            return _execute(this.handle, sql, this._params(params));
        } catch (e[Str]) {
            throw new SqlError(e);
        }
    }

    func[Emp] close() {
        if (this.handle >= 0) _close(this.handle);
        this.handle = -1;
    }

    func[Str] toStr() {
        return $"<Database {this.kind}{this.handle < 0 ? " (closed)" : ""}>";
    }
}

// true / 1 / yes -> "yes" (ODBC only knows yes and no)
func[Str] yesNo(value[Str]) {
    return ["true", "yes", "1"].includes(value.trim().lower()) ? "yes" : "no";
}

// Any database through an ODBC connection string, e.g.
//   "Driver={ODBC Driver 18 for SQL Server};Server=localhost;Database=shop;Trusted_Connection=yes;TrustServerCertificate=yes;"
func[Database] connect(connection[Str]) {
    // TrustCertificate=yes is accepted for TrustServerCertificate=yes
    var[Str<>] pieces = connection.split(";");
    for (var[Int] i = 0; i < pieces.length(); i++) {
        var[Int] equals = pieces<i>.indexOf("=");
        if (equals <= 0) continue;
        var[Str] key = pieces<i>.substr(0, equals).trim().lower();
        if (key == "trustcertificate" || key == "trust certificate") {
            pieces<i> = "TrustServerCertificate=" + yesNo(pieces<i>.substr(equals + 1, pieces<i>.length() - equals - 1));
        }
    }
    var[Database] db = new Database();
    try {
        db.handle = _odbcOpen(pieces.join(";"));
    } catch (e[Str]) {
        throw new SqlError(e);
    }
    db.kind = "odbc";
    return db;
}

// Installed ODBC drivers (e.g. "ODBC Driver 18 for SQL Server").
func[Str<>] drivers() {
    return _odbcDrivers();
}

// Microsoft SQL Server, from a connection string. Both styles work:
//
//   Windows login:  "Server=localhost;Database=shop;Trusted_Connection=yes;"
//   SQL login:      "Server=school-db;Database=shop;User Id=student1;Password=secret;"
//   C# style:       "Data Source=school-db;Initial Catalog=shop;Integrated Security=true;"
//
// The newest installed SQL Server driver is picked automatically, and the
// server's certificate is trusted unless the string says otherwise (school
// and local servers usually have this-signed ones).
func[Database] connectMssql(connection[Str]) {
    // key=value; pairs, keeping their order. Values may be wrapped in { }.
    var[Map[Str, Str]] parts = {:};
    for (var[Str] piece in connection.split(";")) {
        var[Int] equals = piece.indexOf("=");
        if (equals <= 0) continue;
        var[Str] key = piece.substr(0, equals).trim();
        var[Str] value = piece.substr(equals + 1, piece.length() - equals - 1).trim();
        switch (key.lower()) {
            case "data source" := { key = "Server"; }
            case "address" := { key = "Server"; }
            case "addr" := { key = "Server"; }
            case "server" := { key = "Server"; }
            case "initial catalog" := { key = "Database"; }
            case "database" := { key = "Database"; }
            case "user id" := { key = "UID"; }
            case "user" := { key = "UID"; }
            case "uid" := { key = "UID"; }
            case "password" := { key = "PWD"; }
            case "pwd" := { key = "PWD"; }
            case "driver" := { key = "Driver"; }
            case "trustservercertificate" := { key = "TrustServerCertificate"; }
            case "trust server certificate" := { key = "TrustServerCertificate"; }
            case "trustcertificate" := { key = "TrustServerCertificate"; }
            case "trust certificate" := { key = "TrustServerCertificate"; }
            case "trusted_connection" := { key = "Trusted_Connection"; }
            case "integrated security" := {
                key = "Trusted_Connection";
                value = ["true", "yes", "sspi"].includes(value.lower()) ? "yes" : "no";
            }
            default := {}
        }
        if (key == "TrustServerCertificate") value = yesNo(value);
        parts<key> = value;
    }

    if (!parts.has("Driver")) {
        var[Str] driver = "";
        for (var[Str] candidate in ["ODBC Driver 18 for SQL Server", "ODBC Driver 17 for SQL Server", "SQL Server"]) {
            if (driver.isEmpty() && drivers().includes(candidate)) driver = candidate;
        }
        if (driver.isEmpty()) {
            throw new SqlError("No SQL Server ODBC driver is installed (install \"ODBC Driver 18 for SQL Server\" from Microsoft)");
        }
        parts<"Driver"> = "{" + driver + "}";
    }
    if (!parts.has("UID") && !parts.has("Trusted_Connection")) parts<"Trusted_Connection"> = "yes";
    if (!parts.has("TrustServerCertificate")) parts<"TrustServerCertificate"> = "yes";

    // Driver first, then the rest in the order given
    var[Str] odbc = $"Driver={parts<"Driver">};";
    for (var[Str] key in parts) {
        if (key != "Driver") odbc += $"{key}={parts<key>};";
    }
    return connect(odbc);
}

// A SQLite database file (created if it doesn't exist). ":memory:" is a temporary in-memory database.
func[Database] openSqlite(path[Str]) {
    var[Database] db = new Database();
    try {
        db.handle = _sqliteOpen(path);
    } catch (e[Str]) {
        throw new SqlError(e);
    }
    db.kind = "sqlite";
    return db;
}
