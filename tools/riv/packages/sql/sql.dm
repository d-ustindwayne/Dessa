/***
 * $(<sql>) -- databases: Microsoft SQL Server (and anything else with an
 * ODBC driver: MySQL, PostgreSQL, ...) and SQLite.
 *
 *     { connectMssql, openSqlite } from $(<sql>);
 *
 *     var[Database] db = connectMssql("Server=localhost;Database=shop;Trusted_Connection=yes;");
 *     var[Database] db2 = connectMssql("Server=school;Database=shop;User Id=student;Password=secret;");
 *     var[Database] local = openSqlite("app.db");                 // a file (":memory:" = temporary)
 *
 *     for (var[Json] row in db.query("SELECT name, price FROM products WHERE price > ?", 10)) {
 *         print(row.get("name").asStr(), row.get("price").asDbl());
 *     }
 *     var[Int] changed = db.execute("UPDATE products SET price = ? WHERE id = ?", 9.99, 3);
 *     db.close();
 *
 * `?` in the SQL is filled from the extra arguments, in order. Values are
 * sent separately from the SQL text, so they can't break the query (no SQL
 * injection). Rows come back as Json objects (see $(<json>)).
 */

{ _odbcDrivers, _odbcOpen, _sqliteOpen, _query, _execute, _close } from $(<sql>);   // native part
{ Json, jsonObject, jsonNull, jsonNum, jsonStr, jsonBool } from $(<json>);

class SqlError {
    var[Str] message = "";
    init(message[Str]) {
        self.message = message;
    }
    func[Str] toStr() {
        return $"SqlError: {self.message}";
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

func[Json] _cell(text[Str]) {
    var[Str] value = text.substr(2, text.length() - 2);
    switch (text.substr(0, 1)) {
        case "i" := { return jsonNum(Dbl(value)); }
        case "d" := { return jsonNum(Dbl(value)); }
        case "b" := { return jsonBool(value == "1"); }
        case "s" := { return jsonStr(value); }
        default := { return jsonNull(); }
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

    // Rows of a SELECT, each a Json object: row.get("name").asStr()
    func[Json<>] query(sql[Str], params[(Int | Dbl | Str | Bul | Nil)*]) {
        var[Str<>] raw = [];
        try {
            raw = _query(self.handle, sql, self._params(params));
        } catch (e[Str]) {
            throw new SqlError(e);
        }
        var[Int] columns = Int(raw<0>);
        var[Str<>] names = raw.slice(1, columns + 1);
        var[Json<>] rows = [];
        var[Int] at = columns + 1;
        while (at + columns <= raw.length() && columns > 0) {
            var[Json] row = jsonObject();
            for (var[Int] c = 0; c < columns; c++) {
                row.set(names<c>, _cell(raw<at + c>));
            }
            rows.push(row);
            at += columns;
        }
        return rows;
    }

    // The first row, or nil if there is none.
    func[Json | Nil] queryOne(sql[Str], params[(Int | Dbl | Str | Bul | Nil)*]) {
        var[Json<>] rows = self.query(sql, ...params);
        if (rows.isEmpty()) return nil;
        return rows<0>;
    }

    // INSERT / UPDATE / DELETE / CREATE ...; returns how many rows changed.
    // With SQLite, several statements separated by ; can run at once.
    func[Int] execute(sql[Str], params[(Int | Dbl | Str | Bul | Nil)*]) {
        try {
            return _execute(self.handle, sql, self._params(params));
        } catch (e[Str]) {
            throw new SqlError(e);
        }
    }

    func[Emp] close() {
        if (self.handle >= 0) _close(self.handle);
        self.handle = -1;
    }

    func[Str] toStr() {
        return $"<Database {self.kind}{self.handle < 0 ? " (closed)" : ""}>";
    }
}

// Any database through an ODBC connection string, e.g.
//   "Driver={ODBC Driver 18 for SQL Server};Server=localhost;Database=shop;Trusted_Connection=yes;TrustServerCertificate=yes;"
func[Database] connect(connection[Str]) {
    var[Database] db = new Database();
    try {
        db.handle = _odbcOpen(connection);
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
// and local servers usually have self-signed ones).
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
            case "trusted_connection" := { key = "Trusted_Connection"; }
            case "integrated security" := {
                key = "Trusted_Connection";
                value = ["true", "yes", "sspi"].includes(value.lower()) ? "yes" : "no";
            }
            default := {}
        }
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
