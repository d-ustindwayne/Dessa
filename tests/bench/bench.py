"""The Python benchmark: the same tasks as bench.ds. Prints
"task<TAB>result<TAB>milliseconds" per task. Add "quick" for smaller sizes."""
import sys
import time

QUICK = len(sys.argv) > 1 and sys.argv[1] == "quick"
LOOP = 100_000_000 if QUICK else 1_000_000_000


def millis():
    return time.perf_counter_ns() // 1_000_000


def report(name, result, start):
    print(f"{name}\t{result}\t{millis() - start}", flush=True)


def loop_task(n):
    acc = 0
    for i in range(n):
        acc = (acc * 31 + i) % 1000000007
    return acc


def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)


def sieve(limit):
    composite = [False] * (limit + 1)
    count = 0
    for i in range(2, limit + 1):
        if not composite[i]:
            count += 1
            j = i * i
            while j <= limit:
                composite[j] = True
                j += i
    return count


def strings(n):
    text = ""
    for i in range(n):
        text += str(i) + ","
    return len(text)


def sorting(n):
    values = []
    x = 42
    for _ in range(n):
        x = (x * 48271) % 2147483647
        values.append(x)
    values.sort()
    return values[0] + values[n // 2] + values[n - 1]


def maps(n):
    m = {}
    for i in range(n):
        m[(i * 7919) % 1000003] = i
    total = 0
    for i in range(n):
        total += m.get(i, 0)
    return total


class Vec:
    __slots__ = ("x", "y")

    def __init__(self, x, y):
        self.x = x
        self.y = y

    def add(self, other):
        return Vec(self.x + other.x, self.y + other.y)


def objects(n):
    v = Vec(0, 0)
    for i in range(n):
        v = v.add(Vec(i % 10, i % 7))
    return v.x + v.y


def mandelbrot(width, height, limit):
    inside = 0
    for py in range(height):
        cy = -1.2 + 2.4 * py / height
        for px in range(width):
            cx = -2.0 + 3.0 * px / width
            zx = zy = 0.0
            k = 0
            while k < limit and zx * zx + zy * zy <= 4.0:
                t = zx * zx - zy * zy + cx
                zy = 2.0 * zx * zy + cy
                zx = t
                k += 1
            if k == limit:
                inside += 1
    return inside


start = millis(); report("loop", loop_task(LOOP), start)
start = millis(); report("fib", fib(32), start)
start = millis(); report("sieve", sieve(10_000_000), start)
start = millis(); report("strings", strings(1_000_000), start)
start = millis(); report("sorting", sorting(1_000_000), start)
start = millis(); report("maps", maps(1_000_000), start)
start = millis(); report("objects", objects(3_000_000), start)
start = millis(); report("mandelbrot", mandelbrot(800, 600, 100), start)
