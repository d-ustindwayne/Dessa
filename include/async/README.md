# async

Functions that wait without stopping everything: `async`, `await`, `Task`, timers.

**Install:** Comes with Dessa, and is imported for you when a file uses `async`, `await` or `Task:T`.

**Runs in:** Native programs and WebAssembly (pages and Node).

## Example

```
async func[Str] slowHello(name[Str]) {
    await delay(100);
    return $"Hello {name}";
}

async func[Emp] main() {
    var[Task:Str] a = slowHello("Ann"), b = slowHello("Bo");   // both are running
    print(await a, await b);
    print(await all(<slowHello("Cy"), slowHello("Di")>));
    try {
        await failing();
    } catch (e[Error]) {
        print(e.message);
    }
}
main();
```

## What it has

| Name | Gives |
|---|---|
| `Task:T` | something going on, which will give a `T`. An `async func[T]` gives one; `async func[Emp]` gives a `Task:Emp` |
| `await task` | its value, or throws what it failed with |
| `task.isDone()`, `task.failed()` | how it is doing |
| `delay(ms)` | a Task that finishes after that long |
| `all(tasks)` | waits for all of them, and gives their values in order |
| `done(value)` | a Task that has finished already |

In a page, webutils also has `fetchTask(url)` and `postTask(url, body)`. The `request` package has `getTask(url)`, which
waits for the network with everything else stopped, as Dessa has one thread.

## Good to know

There is one thread: code runs until it waits, and a long loop or `sleep` holds everything up. After the
program's last line, Dessa runs what is still waiting. A failed Task nobody awaited then ends the program
with its error; in a page it's logged.
