// $("random")
#pragma once
#include "dessa_runtime.hpp"
#include <chrono>
#include <cstdint>
#include <random>

namespace dessa::random {

// xoshiro256**, seeded with splitmix64. The JavaScript runtime has the same
// generator, so seed(n) gives the same numbers in both.
struct Generator {
    std::uint64_t s[4];

    static std::uint64_t splitmix(std::uint64_t& x) {
        std::uint64_t z = (x += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    void seed(std::uint64_t value) {
        for (auto& part : s) part = splitmix(value);
    }
    static std::uint64_t rotl(std::uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    std::uint64_t next() {
        const std::uint64_t result = rotl(s[1] * 5, 7) * 9;
        const std::uint64_t t = s[1] << 17;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= t;
        s[3] = rotl(s[3], 45);
        return result;
    }
};

inline Generator& generator() {
    static Generator g = [] {
        Generator fresh;
        std::random_device device;
        std::uint64_t seed = (static_cast<std::uint64_t>(device()) << 32) ^ device() ^
                             static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        fresh.seed(seed);
        return fresh;
    }();
    return g;
}

// The same numbers every run: seed(42)
inline void seed(Int value) { generator().seed(static_cast<std::uint64_t>(value)); }

// A decimal from 0 (included) to 1 (not included)
inline Dbl rand() { return static_cast<Dbl>(generator().next() >> 11) * 0x1.0p-53; }

// A whole number from low to high, both included
inline Int randInt(Int low, Int high) {
    if (low > high) fail("randInt: low (" + std::to_string(low) + ") is bigger than high (" + std::to_string(high) + ")");
    const std::uint64_t range = static_cast<std::uint64_t>(high) - static_cast<std::uint64_t>(low) + 1;
    if (range == 0) return static_cast<Int>(generator().next());   // the whole Int range
    const std::uint64_t limit = (UINT64_MAX / range) * range;      // no bias
    std::uint64_t r;
    do r = generator().next(); while (r >= limit);
    return static_cast<Int>(static_cast<std::uint64_t>(low) + r % range);
}

// A decimal from low to high
inline Dbl randDbl(Dbl low, Dbl high) { return low + rand() * (high - low); }

// true with probability p (0.25: one time in four)
inline Bul chance(Dbl p) { return rand() < p; }

// One item of a list
template <class T>
T pick(const List<T>& items) {
    if (items.empty()) fail("pick: the list is empty");
    return items[static_cast<size_t>(randInt(0, static_cast<Int>(items.size()) - 1))];
}

// A shuffled copy
template <class T>
List<T> shuffle(const List<T>& items) {
    List<T> out = items;
    for (Int i = static_cast<Int>(out.size()) - 1; i > 0; --i) {
        std::swap(out[static_cast<size_t>(i)], out[static_cast<size_t>(randInt(0, i))]);
    }
    return out;
}

// `count` different items, in random order
template <class T>
List<T> sample(const List<T>& items, Int count) {
    if (count < 0 || count > static_cast<Int>(items.size())) {
        fail("sample: can't take " + std::to_string(count) + " items from a list of " + std::to_string(items.size()));
    }
    List<T> out = shuffle(items);
    out.resize(static_cast<size_t>(count));
    return out;
}

// Random letters and digits
inline Str randStr(Int length) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    if (length < 0) fail("randStr: length can't be negative");
    Str out;
    for (Int i = 0; i < length; ++i) out += alphabet[randInt(0, 61)];
    return out;
}

// A random UUID (version 4), e.g. "3f2b8c1e-9a4d-4c2b-8f1e-2d3c4b5a6e7f"
inline Str uuid() {
    unsigned char bytes[16];
    for (int half = 0; half < 2; ++half) {
        std::uint64_t r = generator().next();
        for (int i = 0; i < 8; ++i) bytes[half * 8 + i] = static_cast<unsigned char>(r >> (56 - 8 * i));
    }
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);
    static const char hex[] = "0123456789abcdef";
    Str out;
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out += '-';
        out += hex[bytes[i] >> 4];
        out += hex[bytes[i] & 0x0F];
    }
    return out;
}

} // namespace dessa::random
