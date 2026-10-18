// The C++ benchmark: the same tasks as bench.ds. Prints
// "task<TAB>result<TAB>milliseconds" per task. Add "quick" for smaller sizes.
//     g++ -O2 -std=c++20 bench.cpp -o bench_cpp
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using Int = std::int64_t;

static Int millis() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static void report(const char* name, Int result, Int start) {
    std::printf("%s\t%lld\t%lld\n", name, static_cast<long long>(result), static_cast<long long>(millis() - start));
    std::fflush(stdout);
}

static Int loopTask(Int n) {
    Int acc = 0;
    for (Int i = 0; i < n; i++) acc = (acc * 31 + i) % 1000000007;
    return acc;
}

static Int fib(Int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

static Int sieve(Int limit) {
    std::vector<bool> composite(static_cast<size_t>(limit + 1), false);
    Int count = 0;
    for (Int i = 2; i <= limit; i++) {
        if (!composite[static_cast<size_t>(i)]) {
            count++;
            for (Int j = i * i; j <= limit; j += i) composite[static_cast<size_t>(j)] = true;
        }
    }
    return count;
}

static Int strings(Int n) {
    std::string text;
    for (Int i = 0; i < n; i++) text += std::to_string(i) + ",";
    return static_cast<Int>(text.size());
}

static Int sorting(Int n) {
    std::vector<Int> values;
    Int x = 42;
    for (Int i = 0; i < n; i++) {
        x = (x * 48271) % 2147483647;
        values.push_back(x);
    }
    std::sort(values.begin(), values.end());
    return values[0] + values[static_cast<size_t>(n / 2)] + values[static_cast<size_t>(n - 1)];
}

static Int maps(Int n) {
    std::unordered_map<Int, Int> m;
    for (Int i = 0; i < n; i++) m[(i * 7919) % 1000003] = i;
    Int total = 0;
    for (Int i = 0; i < n; i++) {
        auto it = m.find(i);
        total += it == m.end() ? 0 : it->second;
    }
    return total;
}

struct Vec {
    Int x, y;
    Vec(Int x, Int y) : x(x), y(y) {}
    std::shared_ptr<Vec> add(const Vec& other) const { return std::make_shared<Vec>(x + other.x, y + other.y); }
};

static Int objects(Int n) {
    auto v = std::make_shared<Vec>(0, 0);
    for (Int i = 0; i < n; i++) {
        auto step = std::make_shared<Vec>(i % 10, i % 7);
        v = v->add(*step);
    }
    return v->x + v->y;
}

static Int mandelbrot(Int width, Int height, Int limit) {
    Int inside = 0;
    for (Int py = 0; py < height; py++) {
        double cy = -1.2 + 2.4 * static_cast<double>(py) / static_cast<double>(height);
        for (Int px = 0; px < width; px++) {
            double cx = -2.0 + 3.0 * static_cast<double>(px) / static_cast<double>(width);
            double zx = 0.0, zy = 0.0;
            Int k = 0;
            while (k < limit && zx * zx + zy * zy <= 4.0) {
                double t = zx * zx - zy * zy + cx;
                zy = 2.0 * zx * zy + cy;
                zx = t;
                k++;
            }
            if (k == limit) inside++;
        }
    }
    return inside;
}

int main(int argc, char** argv) {
    const bool quick = argc > 1 && std::string(argv[1]) == "quick";
    Int start = millis(); report("loop", loopTask(quick ? 100000000 : 1000000000), start);
    start = millis(); report("fib", fib(32), start);
    start = millis(); report("sieve", sieve(10000000), start);
    start = millis(); report("strings", strings(1000000), start);
    start = millis(); report("sorting", sorting(1000000), start);
    start = millis(); report("maps", maps(1000000), start);
    start = millis(); report("objects", objects(3000000), start);
    start = millis(); report("mandelbrot", mandelbrot(800, 600, 100), start);
}
