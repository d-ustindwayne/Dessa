// $(<math>)
#pragma once
#include "dessa_runtime.hpp"
#include <random>

namespace dessa::math {

inline constexpr Dbl PI = 3.14159265358979323846;
inline constexpr Dbl E = 2.71828182845904523536;

inline Dbl sqrt(Dbl x) {
    if (x < 0) fail("sqrt of a negative number");
    return std::sqrt(x);
}
inline Dbl cbrt(Dbl x) { return std::cbrt(x); }
inline Dbl pow(Dbl base, Dbl exponent) { return std::pow(base, exponent); }

template <class T> T abs(T x) { return x < 0 ? -x : x; }
template <class A, class B> auto min(A a, B b) -> decltype(a + b) { return a < b ? a : b; }
template <class A, class B> auto max(A a, B b) -> decltype(a + b) { return a < b ? b : a; }
template <class A, class B, class C> auto clamp(A x, B low, C high) -> decltype(x + low + high) {
    if (high < low) fail("clamp: high is smaller than low");
    return x < low ? low : (high < x ? high : x);
}

inline Int floor(Dbl x) { return static_cast<Int>(std::floor(x)); }
inline Int ceil(Dbl x) { return static_cast<Int>(std::ceil(x)); }
inline Int round(Dbl x) { return static_cast<Int>(std::llround(x)); }

inline Dbl sin(Dbl x) { return std::sin(x); }
inline Dbl cos(Dbl x) { return std::cos(x); }
inline Dbl tan(Dbl x) { return std::tan(x); }
inline Dbl atan2(Dbl y, Dbl x) { return std::atan2(y, x); }
inline Dbl log(Dbl x) {
    if (x <= 0) fail("log of a number <= 0");
    return std::log(x);
}
inline Dbl log10(Dbl x) {
    if (x <= 0) fail("log10 of a number <= 0");
    return std::log10(x);
}
inline Dbl exp(Dbl x) { return std::exp(x); }

inline std::mt19937_64& generator() {
    static std::mt19937_64 engine{std::random_device{}()};
    return engine;
}
// [0, 1)
inline Dbl random() {
    return std::uniform_real_distribution<Dbl>(0.0, 1.0)(generator());
}
// [low, high]
inline Int randomInt(Int low, Int high) {
    if (high < low) fail("randomInt: high is smaller than low");
    return std::uniform_int_distribution<Int>(low, high)(generator());
}

// ============================================================
// Big: whole numbers of any size (used by class Big in math.dm)
// ============================================================

class BigNumber {
public:
    BigNumber() = default;
    BigNumber(Int value) {
        negative = value < 0;
        unsigned long long magnitude = negative ? 0ULL - static_cast<unsigned long long>(value) : static_cast<unsigned long long>(value);
        while (magnitude) {
            limbs.push_back(static_cast<uint32_t>(magnitude % BASE));
            magnitude /= BASE;
        }
    }
    explicit BigNumber(const Str& text) {
        size_t i = 0;
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        bool neg = false;
        if (i < text.size() && (text[i] == '-' || text[i] == '+')) neg = text[i++] == '-';
        size_t end = text.size();
        while (end > i && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
        if (i == end) fail("Cannot convert \"" + text + "\" to Big");
        for (size_t k = i; k < end; ++k) {
            if (!std::isdigit(static_cast<unsigned char>(text[k]))) fail("Cannot convert \"" + text + "\" to Big");
        }
        for (size_t stop = end; stop > i;) {
            size_t start = stop >= i + 9 ? stop - 9 : i;
            limbs.push_back(static_cast<uint32_t>(std::stoul(text.substr(start, stop - start))));
            stop = start;
        }
        trim();
        negative = neg && !isZero();
    }

    bool isZero() const { return limbs.empty(); }
    bool isNegative() const { return negative; }

    Str str() const {
        if (limbs.empty()) return "0";
        Str out = negative ? "-" : "";
        out += std::to_string(limbs.back());
        char buffer[16];
        for (size_t i = limbs.size() - 1; i-- > 0;) {
            std::snprintf(buffer, sizeof buffer, "%09u", limbs[i]);
            out += buffer;
        }
        return out;
    }

    // Scientific notation: 213000000.sci(2) -> "2.13E8"
    Str sci(Int digits) const {
        Str text = str();
        bool neg = !text.empty() && text[0] == '-';
        if (neg) text = text.substr(1);
        if (digits < 0) digits = 0;
        int exponent = static_cast<int>(text.size()) - 1;
        // round to `digits` decimals
        Str kept = text.substr(0, std::min<size_t>(text.size(), static_cast<size_t>(digits) + 1));
        if (text.size() > kept.size() && text[kept.size()] >= '5') {
            int k = static_cast<int>(kept.size()) - 1;
            while (k >= 0 && kept[k] == '9') kept[k--] = '0';
            if (k < 0) {
                kept.insert(kept.begin(), '1');
                kept.pop_back();
                ++exponent;
            } else {
                ++kept[k];
            }
        }
        Str out = neg ? "-" : "";
        out += kept.substr(0, 1);
        if (kept.size() > 1) out += "." + kept.substr(1);
        return out + "E" + std::to_string(exponent);
    }

    Int toInt() const {
        if (limbs.size() > 3 || *this > BigNumber(INT64_MAX) || *this < BigNumber(INT64_MIN)) {
            fail("Cannot convert " + sci(2) + " to Int: it's too big (Int goes up to about 9.22E18)");
        }
        long long result = 0;
        for (size_t i = limbs.size(); i-- > 0;) result = result * BASE + limbs[i];
        return negative ? -result : result;
    }

    Dbl toDbl() const {
        double result = 0;
        for (size_t i = limbs.size(); i-- > 0;) result = result * BASE + limbs[i];
        return negative ? -result : result;
    }

    friend BigNumber operator-(const BigNumber& a) {
        BigNumber r = a;
        if (!r.isZero()) r.negative = !r.negative;
        return r;
    }
    friend BigNumber operator+(const BigNumber& a, const BigNumber& b) {
        if (a.negative == b.negative) {
            BigNumber r = addMagnitude(a, b);
            r.negative = a.negative && !r.isZero();
            return r;
        }
        return a.negative ? subtractSigned(b, -a) : subtractSigned(a, -b);
    }
    friend BigNumber operator-(const BigNumber& a, const BigNumber& b) { return a + (-b); }
    friend BigNumber operator*(const BigNumber& a, const BigNumber& b) {
        if (a.isZero() || b.isZero()) return BigNumber();
        std::vector<unsigned long long> accum(a.limbs.size() + b.limbs.size() + 1, 0);
        for (size_t i = 0; i < a.limbs.size(); ++i) {
            unsigned long long carry = 0;
            for (size_t j = 0; j < b.limbs.size() || carry; ++j) {
                unsigned long long cur = accum[i + j] + carry + (j < b.limbs.size() ? static_cast<unsigned long long>(a.limbs[i]) * b.limbs[j] : 0);
                accum[i + j] = cur % BASE;
                carry = cur / BASE;
            }
        }
        BigNumber r;
        for (auto v : accum) r.limbs.push_back(static_cast<uint32_t>(v));
        r.trim();
        r.negative = (a.negative != b.negative) && !r.isZero();
        return r;
    }
    // Whole-number division, rounding toward zero
    friend BigNumber operator/(const BigNumber& a, const BigNumber& b) { return divide(a, b).first; }
    friend BigNumber operator%(const BigNumber& a, const BigNumber& b) { return divide(a, b).second; }

    friend bool operator==(const BigNumber& a, const BigNumber& b) { return a.negative == b.negative && a.limbs == b.limbs; }
    friend bool operator!=(const BigNumber& a, const BigNumber& b) { return !(a == b); }
    friend bool operator<(const BigNumber& a, const BigNumber& b) {
        if (a.negative != b.negative) return a.negative;
        int c = compareMagnitude(a, b);
        return a.negative ? c > 0 : c < 0;
    }
    friend bool operator>(const BigNumber& a, const BigNumber& b) { return b < a; }
    friend bool operator<=(const BigNumber& a, const BigNumber& b) { return !(b < a); }
    friend bool operator>=(const BigNumber& a, const BigNumber& b) { return !(a < b); }

    BigNumber& operator+=(const BigNumber& b) { return *this = *this + b; }
    BigNumber& operator-=(const BigNumber& b) { return *this = *this - b; }
    BigNumber& operator*=(const BigNumber& b) { return *this = *this * b; }

    size_t hash() const {
        size_t h = negative ? 1 : 0;
        for (auto l : limbs) h = h * 1000003 ^ l;
        return h;
    }

private:
    static constexpr uint32_t BASE = 1000000000;
    bool negative = false;
    std::vector<uint32_t> limbs; // base 1e9, least significant first

    void trim() {
        while (!limbs.empty() && limbs.back() == 0) limbs.pop_back();
        if (limbs.empty()) negative = false;
    }
    static int compareMagnitude(const BigNumber& a, const BigNumber& b) {
        if (a.limbs.size() != b.limbs.size()) return a.limbs.size() < b.limbs.size() ? -1 : 1;
        for (size_t i = a.limbs.size(); i-- > 0;) {
            if (a.limbs[i] != b.limbs[i]) return a.limbs[i] < b.limbs[i] ? -1 : 1;
        }
        return 0;
    }
    static BigNumber addMagnitude(const BigNumber& a, const BigNumber& b) {
        BigNumber r;
        uint64_t carry = 0;
        for (size_t i = 0; i < std::max(a.limbs.size(), b.limbs.size()) || carry; ++i) {
            uint64_t cur = carry + (i < a.limbs.size() ? a.limbs[i] : 0) + (i < b.limbs.size() ? b.limbs[i] : 0);
            r.limbs.push_back(static_cast<uint32_t>(cur % BASE));
            carry = cur / BASE;
        }
        return r;
    }
    // |a| - |b| where |a| >= |b|
    static BigNumber subtractMagnitude(const BigNumber& a, const BigNumber& b) {
        BigNumber r;
        int64_t borrow = 0;
        for (size_t i = 0; i < a.limbs.size(); ++i) {
            int64_t cur = static_cast<int64_t>(a.limbs[i]) - borrow - (i < b.limbs.size() ? b.limbs[i] : 0);
            borrow = cur < 0;
            if (cur < 0) cur += BASE;
            r.limbs.push_back(static_cast<uint32_t>(cur));
        }
        r.trim();
        return r;
    }
    // a - b for non-negative a, b
    static BigNumber subtractSigned(const BigNumber& a, const BigNumber& b) {
        if (compareMagnitude(a, b) >= 0) return subtractMagnitude(a, b);
        BigNumber r = subtractMagnitude(b, a);
        r.negative = !r.isZero();
        return r;
    }
    static BigNumber multiplySmall(const BigNumber& a, uint32_t m) {
        BigNumber r;
        uint64_t carry = 0;
        for (size_t i = 0; i < a.limbs.size() || carry; ++i) {
            uint64_t cur = carry + (i < a.limbs.size() ? static_cast<uint64_t>(a.limbs[i]) * m : 0);
            r.limbs.push_back(static_cast<uint32_t>(cur % BASE));
            carry = cur / BASE;
        }
        r.trim();
        return r;
    }
    static std::pair<BigNumber, BigNumber> divide(const BigNumber& a, const BigNumber& b) {
        if (b.isZero()) fail("Division by zero");
        BigNumber divisor = b;
        divisor.negative = false;
        BigNumber quotient, remainder;
        quotient.limbs.assign(a.limbs.size(), 0);
        for (size_t i = a.limbs.size(); i-- > 0;) {
            remainder.limbs.insert(remainder.limbs.begin(), a.limbs[i]);
            remainder.trim();
            // largest q with divisor * q <= remainder
            uint32_t low = 0, high = BASE - 1;
            while (low < high) {
                uint32_t mid = low + (high - low + 1) / 2;
                if (compareMagnitude(multiplySmall(divisor, mid), remainder) <= 0) low = mid;
                else high = mid - 1;
            }
            quotient.limbs[i] = low;
            remainder = subtractMagnitude(remainder, multiplySmall(divisor, low));
        }
        quotient.trim();
        quotient.negative = (a.negative != b.negative) && !quotient.isZero();
        remainder.negative = a.negative && !remainder.isZero();
        return {quotient, remainder};
    }
};

// BigNumber follows the same rules as Int: % is never negative, ^ needs a whole exponent.
inline BigNumber div(const BigNumber& a, const BigNumber& b) { return a / b; }
inline BigNumber mod(const BigNumber& a, const BigNumber& b) {
    BigNumber r = a % b;
    if (r < BigNumber(0)) r = r + (b < BigNumber(0) ? -b : b);
    return r;
}
inline BigNumber pow(const BigNumber& base, Int exponent) {
    if (exponent < 0) fail("Big.pow: the exponent can't be negative");
    BigNumber result(1), b = base;
    for (Int e = exponent; e > 0; e >>= 1) {
        if (e & 1) result = result * b;
        if (e > 1) b = b * b;
    }
    return result;
}


// ---- class Big (math.dm) works on digit strings through these ----
inline Str _bigNormalize(const Str& text) { return BigNumber(text).str(); }
inline Str _bigAdd(const Str& a, const Str& b) { return (BigNumber(a) + BigNumber(b)).str(); }
inline Str _bigSub(const Str& a, const Str& b) { return (BigNumber(a) - BigNumber(b)).str(); }
inline Str _bigMul(const Str& a, const Str& b) { return (BigNumber(a) * BigNumber(b)).str(); }
inline Str _bigDiv(const Str& a, const Str& b) { return (BigNumber(a) / BigNumber(b)).str(); }
inline Str _bigMod(const Str& a, const Str& b) { return mod(BigNumber(a), BigNumber(b)).str(); }
inline Str _bigPow(const Str& a, Int exponent) { return pow(BigNumber(a), exponent).str(); }
inline Int _bigCompare(const Str& a, const Str& b) {
    BigNumber x(a), y(b);
    return x < y ? -1 : (y < x ? 1 : 0);
}
inline Str _bigSci(const Str& a, Int digits) { return BigNumber(a).sci(digits); }
inline Int _bigToInt(const Str& a) { return BigNumber(a).toInt(); }
inline Dbl _bigToDbl(const Str& a) { return BigNumber(a).toDbl(); }

} // namespace dessa::math
