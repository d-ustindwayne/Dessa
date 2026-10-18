// The JavaScript benchmark: the same tasks as bench.ds. Prints
// "task<TAB>result<TAB>milliseconds" per task. Add "quick" for smaller sizes.
//     node bench.js
// Numbers stay below 2^53, so plain JavaScript numbers give exact results.
"use strict";
const quick = process.argv[2] === "quick";
const millis = () => Math.floor(performance.now());
const report = (name, result, start) => console.log(`${name}\t${result}\t${millis() - start}`);

function loopTask(n) {
    let acc = 0;
    for (let i = 0; i < n; i++) acc = (acc * 31 + i) % 1000000007;
    return acc;
}

function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

function sieve(limit) {
    const composite = new Uint8Array(limit + 1);
    let count = 0;
    for (let i = 2; i <= limit; i++) {
        if (!composite[i]) {
            count++;
            for (let j = i * i; j <= limit; j += i) composite[j] = 1;
        }
    }
    return count;
}

function strings(n) {
    let text = "";
    for (let i = 0; i < n; i++) text += String(i) + ",";
    return text.length;
}

function sorting(n) {
    const values = [];
    let x = 42;
    for (let i = 0; i < n; i++) {
        x = (x * 48271) % 2147483647;
        values.push(x);
    }
    values.sort((a, b) => a - b);
    return values[0] + values[Math.floor(n / 2)] + values[n - 1];
}

function maps(n) {
    const m = new Map();
    for (let i = 0; i < n; i++) m.set((i * 7919) % 1000003, i);
    let total = 0;
    for (let i = 0; i < n; i++) total += m.get(i) ?? 0;
    return total;
}

class Vec {
    constructor(x, y) { this.x = x; this.y = y; }
    add(other) { return new Vec(this.x + other.x, this.y + other.y); }
}

function objects(n) {
    let v = new Vec(0, 0);
    for (let i = 0; i < n; i++) v = v.add(new Vec(i % 10, i % 7));
    return v.x + v.y;
}

function mandelbrot(width, height, limit) {
    let inside = 0;
    for (let py = 0; py < height; py++) {
        const cy = -1.2 + 2.4 * py / height;
        for (let px = 0; px < width; px++) {
            const cx = -2.0 + 3.0 * px / width;
            let zx = 0.0, zy = 0.0, k = 0;
            while (k < limit && zx * zx + zy * zy <= 4.0) {
                const t = zx * zx - zy * zy + cx;
                zy = 2.0 * zx * zy + cy;
                zx = t;
                k++;
            }
            if (k === limit) inside++;
        }
    }
    return inside;
}

let start = millis(); report("loop", loopTask(quick ? 100000000 : 1000000000), start);
start = millis(); report("fib", fib(32), start);
start = millis(); report("sieve", sieve(10000000), start);
start = millis(); report("strings", strings(1000000), start);
start = millis(); report("sorting", sorting(1000000), start);
start = millis(); report("maps", maps(1000000), start);
start = millis(); report("objects", objects(3000000), start);
start = millis(); report("mandelbrot", mandelbrot(800, 600, 100), start);
