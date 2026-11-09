/***
 * async -- functions that wait without stopping everything.
 *
 *     async func[Str] slowHello(name[Str]) {
 *         await delay(100);
 *         return $"Hello {name}";
 *     }
 *
 *     async func[Emp] main() {
 *         var[Task:Str] a = slowHello("Ann"), b = slowHello("Bo");   // both are running
 *         print(await a, await b);
 *     }
 *     main();
 *
 * An async function gives a Task as soon as it reaches its first wait. await gives what
 * the Task finished with, or throws what it failed with. An async func[Emp] gives a Task:Emp.
 *
 * Everything runs on one thread: code runs until it waits, and only then does something else
 * get its turn. After the last line of the program, Dessa keeps running what is still waiting.
 * This file is imported for you when a file uses async, await or Task.
 */
$("time") { millis, sleep }

// What a Task:Emp holds
class _Nothing {}

// Whatever a frame or task needs to know, whatever T is
class _TaskBase {
    var[Bul] _done = false;
    var[Error | Nil] _error = nil;
    var[Str | Nil] _message = nil;
    var[Bul] _seen = false;      // someone is told if it fails
    var[_Frame<>] _waiters = <>;

    func[Bul] _isDone() { return this._done; }

    func[Emp] _settle() {
        this._done = true;
        for (var[_Frame] waiter in this._waiters) _wake(waiter);
        this._waiters = <>;
    }

    func[Emp] _waitBy(frame[_Frame]) {
        this._seen = true;
        this._waiters.push(frame);
    }

    func[Emp] _fail(e[Error]) {
        this._error = e;
        _failed.push(this);
        this._settle();
    }

    func[Emp] _failText(message[Str]) {
        this._message = message;
        _failed.push(this);
        this._settle();
    }

    func[Emp] _throwIfFailed() {
        var[Error | Nil] e = this._error;
        if (e != nil) throw e;
        var[Str | Nil] m = this._message;
        if (m != nil) throw m;
    }
}

// Something that is going on, and will give a T.
class Task:T(_TaskBase) {
    var[T<>] _box = <>;

    func[Emp] _resolve(value[T]) {
        this._box.push(value);
        this._settle();
    }

    func[T] _take() {
        this._seen = true;
        this._throwIfFailed();
        return this._box<0>;
    }

    // Is it finished, either way?
    func[Bul] isDone() { return this._done; }
    // Did it finish by throwing?
    func[Bul] failed() { return this._done && (this._error != nil || this._message != nil); }
}

// An async function, between its waits (see src/analyser/analyser_async.cpp)
class _Frame {
    var[Int] _s = 0;             // the state to go on from; -1 is finished
    var[Int] _h = -1;            // the state that handles errors right now
    var[_TaskBase | Nil] _base = nil;
    var[Error | Nil] _err = nil;
    var[Str | Nil] _msg = nil;

    func[Emp] _step() { }

    func[Emp] _caught(e[Error]) {
        this._err = e;
        this._msg = nil;
        if (this._h >= 0) {
            this._s = this._h;
            return;
        }
        this._s = -1;
        var[_TaskBase | Nil] task = this._base;
        if (task != nil) task._fail(e);
    }

    func[Emp] _caughtText(message[Str]) {
        this._err = nil;
        this._msg = message;
        if (this._h >= 0) {
            this._s = this._h;
            return;
        }
        this._s = -1;
        var[_TaskBase | Nil] task = this._base;
        if (task != nil) task._failText(message);
    }

    func[Str] _text() {
        var[Error | Nil] e = this._err;
        if (e != nil) return e.message;
        return this._msg ?? "";
    }

    // No catch of this try fits: on to the next one out
    func[Emp] _rethrow() {
        var[Error | Nil] e = this._err;
        if (e != nil) throw e;
        throw this._msg ?? "";
    }
}

// ---- Whose turn is it ----

var[_Frame<>] _ready = <>;
var[_TaskBase<>] _failed = <>;
var[Bul] _draining = false;
// In a page, the browser runs the timers: (milliseconds, what to do)
var[Func[Emp, [Int, Func[Emp]]] | Nil] _host = nil;

class _Timer {
    var[Int] due;
    var[Func[Emp]] action;
    init(due[Int], action[Func[Emp]]) {
        this.due = due;
        this.action = action;
    }
}
var[_Timer<>] _timers = <>;

func[Emp] _wake(frame[_Frame]) {
    _ready.push(frame);
    const[Func[Emp, [Int, Func[Emp]]] | Nil] host = _host;
    if (host != nil && !_draining) {
        _draining = true;
        host(0, () := _drain());
    }
}

func[Emp] _drain() {
    _draining = true;
    var[Int] next = 0;
    while (next < _ready.length()) {
        var[_Frame] frame = _ready<next>;
        next++;
        frame._step();
    }
    _ready = <>;
    _draining = false;
    if (_host != nil) _reportFailed(false);
}

// A failed Task nobody waits for, or looks at, is an error of the program
func[Emp] _reportFailed(stop[Bul]) {
    for (var[_TaskBase] task in _failed) {
        if (task._seen) continue;
        task._seen = true;
        if (stop) task._throwIfFailed();
        var[Error | Nil] e = task._error;
        var[Str | Nil] m = task._message;
        if (e != nil) print("Error in a Task nobody awaited:", e);
        else print("Error in a Task nobody awaited:", m);
    }
    _failed = <>;
}

// Dessa calls this after the last line of a program
func[Emp] _runLoop() {
    loop {
        _drain();
        if (_timers.length() == 0) break;
        var[_Timer] timer = _timers<0>;
        _timers.removeAt(0);
        var[Int] wait = timer.due - millis();
        if (wait > 0) sleep(wait);
        timer.action();
    }
    _reportFailed(true);
}

// In a page: Dessa calls this first
func[Emp] _useHost(host[Func[Emp, [Int, Func[Emp]]]]) { _host = host; }

// Runs `action` after `ms` milliseconds
func[Emp] _after(ms[Int], action[Func[Emp]]) {
    const[Func[Emp, [Int, Func[Emp]]] | Nil] host = _host;
    if (host != nil) {
        host(ms, action);
        return;
    }
    var[Int] due = millis() + (ms > 0 ? ms : 0);
    var[Int] at = _timers.length();
    while (at > 0 && _timers<at - 1>.due > due) at--;
    _timers.insert(at, new _Timer(due, action));
}

// ---- To use ----

// A Task that finishes after `ms` milliseconds.
func[Task:Emp] delay(ms[Int]) {
    var[Task:Emp] task = new Task:Emp();
    _after(ms, () := task._resolve(new _Nothing()));
    return task;
}

// A Task that has already finished with this value.
func[Task:T] done:T(value[T]) {
    var[Task:T] task = new Task:T();
    task._resolve(value);
    return task;
}

// Waits for all of them, and gives their values in the same order. Throws the first failure.
async func[T<>] all:T(tasks[Task:T<>]) {
    var[T<>] values = <>;
    for (var[Task:T] task in tasks) values.push(await task);
    return values;
}
