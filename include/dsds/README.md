# dsds

Data structures, written in Dessa. All but `Graph` are generic: say what they hold, like `Stack:Str` (`Stack:Obj` holds anything).

**Install:** Comes with Dessa, nothing to install.

**Runs in:** Native programs and WebAssembly (pages and Node).

## Example

```dessa
$("dsds") { Stack, PriorityQueue, Graph }

var[Stack:Str] undo = new Stack();
undo.push("typed a");
print(undo.pop());

var[PriorityQueue:Int] tasks = new PriorityQueue();   // smallest first
tasks.push(5);
tasks.push(1);
print(tasks.pop());                                   // 1

var[Graph] roads = new Graph();
roads.addEdge("Cebu", "Manila", 570);
roads.addEdge("Manila", "Baguio", 250);
print(roads.shortestPath("Cebu", "Baguio"), roads.distance("Cebu", "Baguio"));
```

## What it has

| Class | Main methods |
|---|---|
| `Stack` | `push`, `pop`, `peek` |
| `Queue` | `enqueue`, `dequeue`, `peek` |
| `Deque` | `pushFront`, `pushBack`, `popFront`, `popBack`, `peekFront`, `peekBack` |
| `LinkedList` | `addFirst`, `addLast`, `get`, `set`, `insertAt`, `removeAt`, `removeFirst`, `removeLast`, `indexOf`, `includes`, `reverse` |
| `PriorityQueue` | `push`, `pop`, `peek` (give it a compare function for another order) |
| `SearchTree` | `add`, `has`, `remove`, `min`, `max`, `toList` (sorted) |
| `Graph` | `addNode`, `addEdge`, `neighbors`, `bfs`, `dfs`, `hasPath`, `shortestPath`, `distance` (`new Graph(true)` for one-way edges) |

All of them have `size()`, `isEmpty()`, `clear()` and `toList()`.
`compareValues(a, b)` is the order they use: numbers by value, text alphabetically.
