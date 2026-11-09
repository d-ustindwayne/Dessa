/***
 * $("chimb") -- tests for your Dessa code.
 *
 *     $("chimb") { test, expect, expectTrue, expectFalse, expectThrows, expectNear, done }
 *
 *     func[Int] add(a[Int], b[Int]) { return a + b; }
 *
 *     test("adds two numbers", () := {
 *         expect(add(2, 3)).toBe(5);
 *         expect(add(-1, 1)).notToBe(1);
 *     });
 *     test("lists", () := {
 *         expect(<1, 2>.map((x[Int]) := x * 2)).toBe(<2, 4>);
 *         expectTrue(<1, 2>.includes(2));
 *     });
 *     test("errors", () := {
 *         expectThrows(() := { var[Int<>] none = <>; print(none<0>); });
 *     });
 *     done();
 *
 * Each test runs right away and prints one line:
 *
 *       ok   adds two numbers
 *       FAIL lists: expected <2, 4>, got <2, 5>
 *
 * done() prints the totals and ends the program with exit code 1 when a test
 * failed (0 otherwise), so a CI job sees it.
 *
 * `dessa test` runs every *_test.ds and *.test.ds file of the project with
 * chimb and adds up the results of all of them.
 */

$("sys") { args, exit }
$("os") { appendFile }

// What a failed expect... throws (test() catches it)
class ChimbFailure(Error) {}

var[Int] _passed = 0;
var[Int] _failed = 0;
var[Str | Nil] _report = _reportFile();

// dessa test passes --chimb-report=<file>: every result goes there too
func[Str | Nil] _reportFile() {
    for (var[Str] a in args()) {
        if (a.startsWith("--chimb-report=")) return a.substr(15, a.length() - 15);
    }
    return nil;
}

func[Emp] _record(passed[Bul], name[Str], message[Str]) {
    if (passed) {
        _passed++;
        print($"  ok   {name}");
    } else {
        _failed++;
        print($"  FAIL {name}: {message}");
    }
    var[Str | Nil] report = _report;
    if (report != nil) {
        var[Str] clean = message.replaceAll("\n", " ");
        appendFile(report, (passed ? "pass\t" : "fail\t") + name + "\t" + clean + "\n");
    }
}

// Inside group(): the group's name, and what runs around each of its tests
var[Str] _prefix = "";
var[Func[Emp]<>] _before = <>;
var[Func[Emp]<>] _after = <>;

// Tests that belong together: their names start with the group's, and
// beforeEach and afterEach inside it only reach its own tests.
//     group("cart", () := {
//         beforeEach(() := { cart = new Cart(); });
//         test("starts empty", () := { expect(cart.size()).toBe(0); });
//     });
func[Emp] group(name[Str], body[Func[Emp]]) {
    var[Str] outerPrefix = _prefix;
    var[Func[Emp]<>] outerBefore = _before;
    var[Func[Emp]<>] outerAfter = _after;
    _prefix = outerPrefix + name + " > ";
    try {
        body();
    } catch (e[Error]) {
        _record(false, _prefix + "(the group itself)", $"threw {typeof(e)}: {e.message}");
    } catch (e[Str]) {
        _record(false, _prefix + "(the group itself)", $"threw: {e}");
    }
    _prefix = outerPrefix;
    _before = outerBefore;
    _after = outerAfter;
}

// Runs before each test from here on (to the end of the group, or of the file)
func[Emp] beforeEach(action[Func[Emp]]) { _before.push(action); }

// Runs after each test, also a failed one
func[Emp] afterEach(action[Func[Emp]]) { _after.insert(0, action); }

// Runs one test now: it passes when `body` finishes without a failed expect
// and without an error.
func[Emp] test(name[Str], body[Func[Emp]]) {
    var[Str] full = _prefix + name;
    var[Str] problem = "";
    var[Bul] passed = true;
    try {
        for (var[Func[Emp]] action in _before) action();
        body();
    } catch (e[ChimbFailure]) {
        passed = false;
        problem = e.message;
    } catch (e[Error]) {
        passed = false;
        problem = $"threw {typeof(e)}: {e.message}";
    } catch (e[Str]) {
        passed = false;
        problem = $"threw: {e}";
    }
    try {
        for (var[Func[Emp]] action in _after) action();
    } catch (e[Error]) {
        if (passed) problem = $"afterEach threw {typeof(e)}: {e.message}";
        passed = false;
    } catch (e[Str]) {
        if (passed) problem = $"afterEach threw: {e}";
        passed = false;
    }
    _record(passed, full, problem);
}

// Prints the totals; exit code 1 if a test failed
func[Emp] done() {
    print("");
    print($"{_passed} passed, {_failed} failed");
    if (_failed > 0) exit(1);
}

// How many passed and failed so far
func[Int] passedCount() { return _passed; }
func[Int] failedCount() { return _failed; }

// Fails the test right here, with a message
func[Emp] fail(message[Str]) {
    throw new ChimbFailure(message);
}

// expect(actual).toBe(expected): compares with ==
class Expect:T {
    var[T] actual;
    init(actual[T]) { this.actual = actual; }

    func[Emp] toBe(expected[T]) {
        if (!(this.actual == expected)) throw new ChimbFailure($"expected {_show(expected)}, got {_show(this.actual)}");
    }

    func[Emp] notToBe(unexpected[T]) {
        if (this.actual == unexpected) throw new ChimbFailure($"expected anything but {_show(unexpected)}");
    }

    // One of these values
    func[Emp] toBeIn(choices[T<>]) {
        for (var[T] c in choices) if (this.actual == c) return;
        throw new ChimbFailure($"expected one of {choices}, got {_show(this.actual)}");
    }
}

func[Expect:T] expect:T(actual[T]) {
    return new Expect:T(actual);
}

func[Str] _show(value[Obj]) {
    return value is Str ? $"\"{value}\"" : Str(value);
}

func[Emp] expectTrue(condition[Bul], message[Str] = "expected true, got false") {
    if (!condition) throw new ChimbFailure(message);
}

func[Emp] expectFalse(condition[Bul], message[Str] = "expected false, got true") {
    if (condition) throw new ChimbFailure(message);
}

// Two decimals that may differ a little (0.1 + 0.2 and 0.3)
func[Emp] expectNear(actual[Dbl], expected[Dbl], tolerance[Dbl] = 0.000001) {
    var[Dbl] diff = actual - expected;
    if (diff < 0) diff = -diff;
    if (diff > tolerance) throw new ChimbFailure($"expected {expected} (within {tolerance}), got {actual}");
}

// `body` must throw (an Error or a Str); gives the message, to check further
func[Str] expectThrows(body[Func[Emp]]) {
    try {
        body();
    } catch (e[Error]) {
        return e.message;
    } catch (e[Str]) {
        return e;
    }
    throw new ChimbFailure("expected an error, but nothing was thrown");
}
