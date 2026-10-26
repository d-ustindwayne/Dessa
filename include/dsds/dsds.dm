/***
 * $("dsds") -- Dessa data structures: Stack, Queue, Deque, LinkedList,
 * PriorityQueue, SearchTree and Graph. All but Graph are generic: say what
 * they hold, e.g. Stack:Str (Stack:Obj holds anything).
 *
 *     $("dsds") { Stack, Queue, PriorityQueue, Graph }
 *
 *     var[Stack:Str] undo = new Stack();
 *     undo.push("typed a");
 *     undo.push("typed b");
 *     print(undo.pop());                         // typed b
 *
 *     var[PriorityQueue:Int] tasks = new PriorityQueue();
 *     tasks.push(5); tasks.push(1); tasks.push(3);
 *     print(tasks.pop());                        // 1 (the smallest first)
 *
 *     var[Graph] map = new Graph();
 *     map.addEdge("Cebu", "Manila", 570);
 *     map.addEdge("Manila", "Baguio", 250);
 *     print(map.shortestPath("Cebu", "Baguio"));  // <"Cebu", "Manila", "Baguio">
 *
 * Written in Dessa itself (no native part), so it works everywhere Dessa runs.
 */

// How two values compare: negative, 0 or positive. Numbers by value, text
// alphabetically, anything else by how it prints.
func[Int] compareValues(a[Obj], b[Obj]) {
    if ((a is Int || a is Dbl || a is Flt) && (b is Int || b is Dbl || b is Flt)) {
        var[Dbl] x = Dbl(a);
        var[Dbl] y = Dbl(b);
        return x < y ? -1 : x > y ? 1 : 0;
    }
    return Str(a).compare(Str(b));
}

// ============================================================
// Stack: last in, first out
// ============================================================

class Stack:T {
    var[T<>] _items = <>;

    // Puts a value on top.
    func[Emp] push(value[T]) {
        this._items.push(value);
    }

    // Takes the value on top. Error if the stack is empty.
    func[T] pop() {
        if (this._items.isEmpty()) throw "pop: the stack is empty";
        return this._items.pop();
    }

    // The value on top, without taking it.
    func[T] peek() {
        if (this._items.isEmpty()) throw "peek: the stack is empty";
        return this._items.last();
    }

    func[Int] size() { return this._items.length(); }
    func[Bul] isEmpty() { return this._items.isEmpty(); }
    func[Emp] clear() { this._items.clear(); }

    // From the bottom to the top.
    func[T<>] toList() { return this._items.copy(); }

    func[Str] toStr() { return $"Stack{this._items}"; }
}

// ============================================================
// Queue: first in, first out
// ============================================================

class Queue:T {
    var[T<>] _items = <>;
    var[Int] _head = 0;

    // Adds a value at the back.
    func[Emp] enqueue(value[T]) {
        this._items.push(value);
    }

    // Takes the value at the front. Error if the queue is empty.
    func[T] dequeue() {
        if (this.isEmpty()) throw "dequeue: the queue is empty";
        var[T] value = this._items<this._head>;
        this._head++;
        // Drop the taken part now and then
        if (this._head > 32 && this._head * 2 > this._items.length()) {
            this._items = this._items.slice(this._head, this._items.length());
            this._head = 0;
        }
        return value;
    }

    // The value at the front, without taking it.
    func[T] peek() {
        if (this.isEmpty()) throw "peek: the queue is empty";
        return this._items<this._head>;
    }

    func[Int] size() { return this._items.length() - this._head; }
    func[Bul] isEmpty() { return this.size() == 0; }
    func[Emp] clear() { this._items.clear(); this._head = 0; }

    // From the front to the back.
    func[T<>] toList() { return this._items.slice(this._head, this._items.length()); }

    func[Str] toStr() { return $"Queue{this.toList()}"; }
}

// ============================================================
// Deque: add and take at both ends
// ============================================================

class Deque:T {
    var[T<>] _items = <>;

    func[Emp] pushFront(value[T]) { this._items.insert(0, value); }
    func[Emp] pushBack(value[T]) { this._items.push(value); }

    func[T] popFront() {
        if (this._items.isEmpty()) throw "popFront: the deque is empty";
        return this._items.removeAt(0);
    }

    func[T] popBack() {
        if (this._items.isEmpty()) throw "popBack: the deque is empty";
        return this._items.pop();
    }

    func[T] peekFront() {
        if (this._items.isEmpty()) throw "peekFront: the deque is empty";
        return this._items.first();
    }

    func[T] peekBack() {
        if (this._items.isEmpty()) throw "peekBack: the deque is empty";
        return this._items.last();
    }

    func[Int] size() { return this._items.length(); }
    func[Bul] isEmpty() { return this._items.isEmpty(); }
    func[Emp] clear() { this._items.clear(); }
    func[T<>] toList() { return this._items.copy(); }
    func[Str] toStr() { return $"Deque{this._items}"; }
}

// ============================================================
// LinkedList: values linked one to the next
// ============================================================

class ListNode:T {
    var[T] value;
    var[ListNode:T | Nil] next = nil;

    init(value[T]) {
        this.value = value;
    }
}

class LinkedList:T {
    var[ListNode:T | Nil] _first = nil;
    var[ListNode:T | Nil] _last = nil;
    var[Int] _size = 0;

    // Adds a value at the start.
    func[Emp] addFirst(value[T]) {
        var[ListNode:T] node = new ListNode:T(value);
        node.next = this._first;
        this._first = node;
        this._last ?= node;
        this._size++;
    }

    // Adds a value at the end.
    func[Emp] addLast(value[T]) {
        var[ListNode:T] node = new ListNode:T(value);
        var[ListNode:T | Nil] last = this._last;
        if (last == nil) {
            this._first = node;
        } else {
            last.next = node;
        }
        this._last = node;
        this._size++;
    }

    // The node at a position (0 is the first).
    func[ListNode:T] _nodeAt(index[Int], what[Str]) {
        if (index < 0 || index >= this._size) throw $"{what}: index {index} is out of range for a list of {this._size}";
        var[ListNode:T | Nil] node = this._first;
        for (var[Int] i = 0; i < index; i++) {
            if (node != nil) node = node.next;
        }
        return node ?! $"{what}: index {index} is out of range";
    }

    // The value at a position (0 is the first).
    func[T] get(index[Int]) {
        return this._nodeAt(index, "get").value;
    }

    // Replaces the value at a position.
    func[Emp] set(index[Int], value[T]) {
        this._nodeAt(index, "set").value = value;
    }

    // Puts a value at a position, moving the rest one place on.
    func[Emp] insertAt(index[Int], value[T]) {
        if (index == 0) {
            this.addFirst(value);
            return;
        }
        if (index == this._size) {
            this.addLast(value);
            return;
        }
        var[ListNode:T] before = this._nodeAt(index - 1, "insertAt");
        var[ListNode:T] node = new ListNode:T(value);
        node.next = before.next;
        before.next = node;
        this._size++;
    }

    // Takes the value at a position out of the list.
    func[T] removeAt(index[Int]) {
        var[ListNode:T] node = this._nodeAt(index, "removeAt");
        if (index == 0) {
            this._first = node.next;
        } else {
            var[ListNode:T] before = this._nodeAt(index - 1, "removeAt");
            before.next = node.next;
            if (node.next == nil) this._last = before;
        }
        if (this._first == nil) this._last = nil;
        this._size--;
        return node.value;
    }

    func[T] removeFirst() {
        if (this._size == 0) throw "removeFirst: the list is empty";
        return this.removeAt(0);
    }

    func[T] removeLast() {
        if (this._size == 0) throw "removeLast: the list is empty";
        return this.removeAt(this._size - 1);
    }

    // The position of the first value equal to `value`, or -1.
    func[Int] indexOf(value[T]) {
        var[Int] index = 0;
        var[ListNode:T | Nil] node = this._first;
        while (node != nil) {
            if (node.value == value) return index;
            node = node.next;
            index++;
        }
        return -1;
    }

    func[Bul] includes(value[T]) { return this.indexOf(value) >= 0; }

    // Turns the list around.
    func[Emp] reverse() {
        var[ListNode:T | Nil] previous = nil;
        var[ListNode:T | Nil] node = this._first;
        this._last = node;
        while (node != nil) {
            var[ListNode:T | Nil] next = node.next;
            node.next = previous;
            previous = node;
            node = next;
        }
        this._first = previous;
    }

    func[Int] size() { return this._size; }
    func[Bul] isEmpty() { return this._size == 0; }

    func[Emp] clear() {
        this._first = nil;
        this._last = nil;
        this._size = 0;
    }

    func[T<>] toList() {
        var[T<>] all = <>;
        var[ListNode:T | Nil] node = this._first;
        while (node != nil) {
            all.push(node.value);
            node = node.next;
        }
        return all;
    }

    func[Str] toStr() { return $"LinkedList{this.toList()}"; }
}

// ============================================================
// PriorityQueue: always takes the smallest value first
// ============================================================

class PriorityQueue:T {
    var[T<>] _heap = <>;
    var[Func[Int, [T, T]]] _compare = (a[T], b[T]) := compareValues(a, b);

    // The order: nil for smallest first, or a function giving a negative number
    // when a comes first, e.g. new PriorityQueue((a[Int], b[Int]) := compareValues(b, a))
    // for the largest first.
    init(compare[Func[Int, [T, T]] | Nil] = nil) {
        if (compare != nil) this._compare = compare;
    }

    func[Emp] push(value[T]) {
        this._heap.push(value);
        var[Int] i = this._heap.length() - 1;
        while (i > 0) {
            var[Int] parent = (i - 1) / 2;
            if (this._compare(this._heap<i>, this._heap<parent>) >= 0) break;
            this._swap(i, parent);
            i = parent;
        }
    }

    // Takes the first value. Error if the queue is empty.
    func[T] pop() {
        if (this._heap.isEmpty()) throw "pop: the priority queue is empty";
        var[T] top = this._heap<0>;
        var[T] last = this._heap.pop();
        if (!this._heap.isEmpty()) {
            this._heap<0> = last;
            var[Int] i = 0;
            loop {
                var[Int] left = i * 2 + 1;
                var[Int] right = left + 1;
                var[Int] smallest = i;
                if (left < this._heap.length() && this._compare(this._heap<left>, this._heap<smallest>) < 0) smallest = left;
                if (right < this._heap.length() && this._compare(this._heap<right>, this._heap<smallest>) < 0) smallest = right;
                if (smallest == i) break;
                this._swap(i, smallest);
                i = smallest;
            }
        }
        return top;
    }

    // The first value, without taking it.
    func[T] peek() {
        if (this._heap.isEmpty()) throw "peek: the priority queue is empty";
        return this._heap<0>;
    }

    func[Emp] _swap(a[Int], b[Int]) {
        var[T] keep = this._heap<a>;
        this._heap<a> = this._heap<b>;
        this._heap<b> = keep;
    }

    func[Int] size() { return this._heap.length(); }
    func[Bul] isEmpty() { return this._heap.isEmpty(); }
    func[Emp] clear() { this._heap.clear(); }

    // All values in the order they would come out.
    func[T<>] toList() {
        var[T<>] sorted = this._heap.copy();
        sorted.sort(this._compare);
        return sorted;
    }

    func[Str] toStr() { return $"PriorityQueue{this.toList()}"; }
}

// ============================================================
// SearchTree: a sorted set of values (a binary search tree)
// ============================================================

class TreeNode:T {
    var[T] value;
    var[TreeNode:T | Nil] left = nil;
    var[TreeNode:T | Nil] right = nil;

    init(value[T]) {
        this.value = value;
    }
}

class SearchTree:T {
    var[TreeNode:T | Nil] _root = nil;
    var[Int] _size = 0;
    var[Func[Int, [T, T]]] _compare = (a[T], b[T]) := compareValues(a, b);

    // The order: nil for the usual order (compareValues), or a function like
    // PriorityQueue's.
    init(compare[Func[Int, [T, T]] | Nil] = nil) {
        if (compare != nil) this._compare = compare;
    }

    // Adds a value; false if an equal one is already there.
    func[Bul] add(value[T]) {
        var[TreeNode:T | Nil] root = this._root;
        if (root == nil) {
            this._root = new TreeNode:T(value);
            this._size++;
            return true;
        }
        var[TreeNode:T] node = root;
        loop {
            var[Int] order = this._compare(value, node.value);
            if (order == 0) return false;
            var[TreeNode:T | Nil] next = order < 0 ? node.left : node.right;
            if (next == nil) {
                if (order < 0) {
                    node.left = new TreeNode:T(value);
                } else {
                    node.right = new TreeNode:T(value);
                }
                this._size++;
                return true;
            } else {
                node = next;
            }
        }
        return false;
    }

    // Is an equal value in the tree?
    func[Bul] has(value[T]) {
        var[TreeNode:T | Nil] node = this._root;
        while (node != nil) {
            var[Int] order = this._compare(value, node.value);
            if (order == 0) return true;
            node = order < 0 ? node.left : node.right;
        }
        return false;
    }

    // Takes a value out; false if it wasn't there.
    func[Bul] remove(value[T]) {
        var[TreeNode:T | Nil] parent = nil;
        var[TreeNode:T | Nil] node = this._root;
        while (node != nil) {
            var[Int] order = this._compare(value, node.value);
            if (order == 0) break;
            parent = node;
            node = order < 0 ? node.left : node.right;
        }
        if (node == nil) return false;

        var[TreeNode:T | Nil] left = node.left;
        var[TreeNode:T | Nil] right = node.right;
        if (left != nil && right != nil) {
            // Two children: take the smallest value on the right instead
            var[TreeNode:T] successorParent = node;
            var[TreeNode:T] successor = right;
            var[TreeNode:T | Nil] smaller = successor.left;
            while (smaller != nil) {
                successorParent = successor;
                successor = smaller;
                smaller = successor.left;
            }
            node.value = successor.value;
            if (successorParent == node) {
                successorParent.right = successor.right;
            } else {
                successorParent.left = successor.right;
            }
        } else {
            var[TreeNode:T | Nil] child = left != nil ? left : right;
            if (parent == nil) {
                this._root = child;
            } else if (parent.left == node) {
                parent.left = child;
            } else {
                parent.right = child;
            }
        }
        this._size--;
        return true;
    }

    // The smallest value. Error if the tree is empty.
    func[T] min() {
        var[TreeNode:T] node = this._root ?! "min: the tree is empty";
        var[TreeNode:T | Nil] next = node.left;
        while (next != nil) {
            node = next;
            next = next.left;
        }
        return node.value;
    }

    // The largest value. Error if the tree is empty.
    func[T] max() {
        var[TreeNode:T] node = this._root ?! "max: the tree is empty";
        var[TreeNode:T | Nil] next = node.right;
        while (next != nil) {
            node = next;
            next = next.right;
        }
        return node.value;
    }

    func[Int] size() { return this._size; }
    func[Bul] isEmpty() { return this._size == 0; }

    func[Emp] clear() {
        this._root = nil;
        this._size = 0;
    }

    // All values, sorted.
    func[T<>] toList() {
        var[T<>] all = <>;
        var[TreeNode:T<>] pending = <>;
        var[TreeNode:T | Nil] node = this._root;
        while (node != nil || !pending.isEmpty()) {
            while (node != nil) {
                pending.push(node);
                node = node.left;
            }
            var[TreeNode:T] next = pending.pop();
            all.push(next.value);
            node = next.right;
        }
        return all;
    }

    func[Str] toStr() { return $"SearchTree{this.toList()}"; }
}

// ============================================================
// Graph: places (nodes) joined by roads (edges) with a length (weight)
// ============================================================

class Graph {
    var[Bul] directed = false;
    var[Map[Str, Map[Str, Dbl]]] _edges = {:};

    // directed: true when an edge only goes one way
    init(directed[Bul] = false) {
        this.directed = directed;
    }

    func[Emp] addNode(name[Str]) {
        if (!this._edges.has(name)) this._edges<name> = {:};
    }

    // Joins two nodes (adding them if needed).
    func[Emp] addEdge(source[Str], target[Str], weight[Dbl] = 1) {
        this.addNode(source);
        this.addNode(target);
        var[Map[Str, Dbl]] out = this._edges<source>;
        out<target> = weight;
        this._edges<source> = out;
        if (!this.directed) {
            var[Map[Str, Dbl]] back = this._edges<target>;
            back<source> = weight;
            this._edges<target> = back;
        }
    }

    func[Bul] hasNode(name[Str]) { return this._edges.has(name); }

    func[Bul] hasEdge(source[Str], target[Str]) {
        return this._edges.has(source) && this._edges<source>.has(target);
    }

    func[Str<>] nodes() { return this._edges.keys(); }

    // The nodes an edge leads target source `name`.
    func[Str<>] neighbors(name[Str]) {
        if (!this._edges.has(name)) throw $"neighbors: there is no node '{name}'";
        return this._edges<name>.keys();
    }

    // Every node reachable source `start`, nearest first (breadth-first).
    func[Str<>] bfs(start[Str]) {
        if (!this._edges.has(start)) throw $"bfs: there is no node '{start}'";
        var[Str<>] order = <start>;
        var[Str{}] seen = {start};
        var[Int] i = 0;
        while (i < order.length()) {
            var[Str] here = order<i>;
            for (var[Str] next in this._edges<here>.keys()) {
                if (!seen.includes(next)) {
                    seen.add(next);
                    order.push(next);
                }
            }
            i++;
        }
        return order;
    }

    // Every node reachable source `start`, going deep first (depth-first).
    func[Str<>] dfs(start[Str]) {
        if (!this._edges.has(start)) throw $"dfs: there is no node '{start}'";
        var[Str<>] order = <>;
        var[Str{}] seen = {};
        var[Str<>] pending = <start>;
        while (!pending.isEmpty()) {
            var[Str] node = pending.pop();
            if (seen.includes(node)) continue;
            seen.add(node);
            order.push(node);
            var[Str<>] next = this._edges<node>.keys();
            next.reverse();
            for (var[Str] n in next) {
                if (!seen.includes(n)) pending.push(n);
            }
        }
        return order;
    }

    func[Bul] hasPath(source[Str], target[Str]) {
        if (!this._edges.has(source) || !this._edges.has(target)) return false;
        return this.bfs(source).includes(target);
    }

    // The shortest way source one node target another (Dijkstra), or [] if there is none.
    func[Str<>] shortestPath(source[Str], target[Str]) {
        if (!this._edges.has(source)) throw $"shortestPath: there is no node '{source}'";
        if (!this._edges.has(target)) throw $"shortestPath: there is no node '{target}'";
        var[Map[Str, Dbl]] distance = {source: 0.0};
        var[Map[Str, Str]] cameFrom = {:};
        var[Str{}] done = {};
        loop {
            // The nearest node not done yet
            var[Str] current = "";
            var[Dbl] best = -1;
            for (var[Str] node in distance) {
                if (!done.includes(node) && (best < 0 || distance<node> < best)) {
                    current = node;
                    best = distance<node>;
                }
            }
            if (best < 0) break;
            if (current == target) break;
            done.add(current);
            for (var[Str] next in this._edges<current>.keys()) {
                var[Dbl] through = best + this._edges<current><next>;
                if (!distance.has(next) || through < distance<next>) {
                    distance<next> = through;
                    cameFrom<next> = current;
                }
            }
        }
        if (!distance.has(target)) return <>;
        var[Str<>] path = <target>;
        var[Str] node = target;
        while (node != source) {
            node = cameFrom<node>;
            path.insert(0, node);
        }
        return path;
    }

    // The length of the shortest way, or -1 if there is none.
    func[Dbl] distance(source[Str], target[Str]) {
        var[Str<>] path = this.shortestPath(source, target);
        if (path.isEmpty()) return -1;
        var[Dbl] total = 0;
        for (var[Int] i = 1; i < path.length(); i++) {
            var[Str] a = path<i - 1>;
            var[Str] b = path<i>;
            total += this._edges<a><b>;
        }
        return total;
    }

    func[Str] toStr() {
        var[Str<>] parts = <>;
        for (var[Str] source in this._edges) {
            parts.push($"{source} -> {this._edges<source>.keys().join(", ")}");
        }
        return $"Graph({parts.join("; ")})";
    }
}
