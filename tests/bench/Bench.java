// The Java benchmark: the same tasks as bench.ds. Prints
// "task<TAB>result<TAB>milliseconds" per task. Add "quick" for smaller sizes.
//     javac Bench.java && java Bench
import java.util.Arrays;
import java.util.HashMap;

public class Bench {
    static long millis() { return System.nanoTime() / 1_000_000; }

    static void report(String name, long result, long start) {
        System.out.println(name + "\t" + result + "\t" + (millis() - start));
        System.out.flush();
    }

    static long loopTask(long n) {
        long acc = 0;
        for (long i = 0; i < n; i++) acc = (acc * 31 + i) % 1000000007L;
        return acc;
    }

    static long fib(long n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

    static long sieve(int limit) {
        boolean[] composite = new boolean[limit + 1];
        long count = 0;
        for (int i = 2; i <= limit; i++) {
            if (!composite[i]) {
                count++;
                for (long j = (long) i * i; j <= limit; j += i) composite[(int) j] = true;
            }
        }
        return count;
    }

    static long strings(int n) {
        StringBuilder text = new StringBuilder();
        for (int i = 0; i < n; i++) text.append(i).append(',');
        return text.length();
    }

    static long sorting(int n) {
        long[] values = new long[n];
        long x = 42;
        for (int i = 0; i < n; i++) {
            x = (x * 48271) % 2147483647L;
            values[i] = x;
        }
        Arrays.sort(values);
        return values[0] + values[n / 2] + values[n - 1];
    }

    static long maps(int n) {
        HashMap<Long, Long> m = new HashMap<>();
        for (long i = 0; i < n; i++) m.put((i * 7919) % 1000003, i);
        long total = 0;
        for (long i = 0; i < n; i++) total += m.getOrDefault(i, 0L);
        return total;
    }

    static final class Vec {
        final long x, y;
        Vec(long x, long y) { this.x = x; this.y = y; }
        Vec add(Vec other) { return new Vec(x + other.x, y + other.y); }
    }

    static long objects(int n) {
        Vec v = new Vec(0, 0);
        for (int i = 0; i < n; i++) v = v.add(new Vec(i % 10, i % 7));
        return v.x + v.y;
    }

    static long mandelbrot(int width, int height, int limit) {
        long inside = 0;
        for (int py = 0; py < height; py++) {
            double cy = -1.2 + 2.4 * py / height;
            for (int px = 0; px < width; px++) {
                double cx = -2.0 + 3.0 * px / width;
                double zx = 0.0, zy = 0.0;
                int k = 0;
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

    public static void main(String[] args) {
        boolean quick = args.length > 0 && args[0].equals("quick");
        long start = millis(); report("loop", loopTask(quick ? 100_000_000L : 1_000_000_000L), start);
        start = millis(); report("fib", fib(32), start);
        start = millis(); report("sieve", sieve(10_000_000), start);
        start = millis(); report("strings", strings(1_000_000), start);
        start = millis(); report("sorting", sorting(1_000_000), start);
        start = millis(); report("maps", maps(1_000_000), start);
        start = millis(); report("objects", objects(3_000_000), start);
        start = millis(); report("mandelbrot", mandelbrot(800, 600, 100), start);
    }
}
