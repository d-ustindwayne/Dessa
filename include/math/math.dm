/***
 * $("math") -- the Dessa part: class Big, a helper for whole numbers of any
 * size (like Java's BigInteger). The native functions are in math.hpp and
 * declared in math.ds.
 *
 *     $("math") { Big }
 *     var[Big] f = new Big(1);
 *     for (var[Int] i = 1; i <= 30; i++) f = f.mul(i);
 *     print(f);           // 265252859812191058636308480000000
 *     print(f.sci());     // 2.65E32
 *
 * Every method returns a new Big; a Big never changes. Methods that take
 * another number accept a Big or an Int.
 */

$("math") { _bigNormalize, _bigAdd, _bigSub, _bigMul, _bigDiv, _bigMod, _bigPow, _bigCompare, _bigSci, _bigToInt, _bigToDbl, sqrt, idiv }

class Big {
    var[Str] digits = "0";

    // new Big(42), new Big("123456789012345678901234567890")
    init(value[Int | Str]=0) {
        if (value is Int) {
            this.digits = Str(value);
        } else if (value is Str) {
            this.digits = _bigNormalize(value);
        }
    }

    func[Str] _of(other[Big | Int]) {
        if (other is Int) return Str(other);
        if (other is Big) return other.digits;
        return "0";
    }

    func[Big] _make(digits[Str]) {
        return new Big(digits);
    }

    // ---- Arithmetic ----
    func[Big] add(other[Big | Int]) { return this._make(_bigAdd(this.digits, this._of(other))); }
    func[Big] sub(other[Big | Int]) { return this._make(_bigSub(this.digits, this._of(other))); }
    func[Big] mul(other[Big | Int]) { return this._make(_bigMul(this.digits, this._of(other))); }

    // The whole part of the division: new Big(7).div(2) is 3.
    func[Big] div(other[Big | Int]) { return this._make(_bigDiv(this.digits, this._of(other))); }

    // The remainder, never negative (same rule as %).
    func[Big] mod(other[Big | Int]) { return this._make(_bigMod(this.digits, this._of(other))); }

    // The greatest common divisor, never negative.
    func[Big] gcd(other[Big | Int]) {
        var[Big] a = this.abs();
        var[Big] b = this._make(this._of(other)).abs();
        while (!b.isZero()) {
            var[Big] rest = a.mod(b);
            a = b;
            b = rest;
        }
        return a;
    }

    // The least common multiple, never negative.
    func[Big] lcm(other[Big | Int]) {
        var[Big] o = this._make(this._of(other));
        if (this.isZero() || o.isZero()) return new Big(0);
        return this.abs().div(this.gcd(o)).mul(o.abs());
    }

    // this ^ exponent % modulus, quick even for huge numbers.
    func[Big] modPow(exponent[Big | Int], modulus[Big | Int]) {
        var[Big] m = this._make(this._of(modulus));
        var[Big] e = this._make(this._of(exponent));
        if (m.isZero()) throw "Division by zero";
        if (e.isNegative()) throw "modPow: the exponent can't be negative";
        var[Big] result = new Big(1).mod(m);
        var[Big] base = this.mod(m);
        while (!e.isZero()) {
            if (!e.mod(2).isZero()) result = result.mul(base).mod(m);
            base = base.mul(base).mod(m);
            e = e.div(2);
        }
        return result;
    }

    // The whole part of the square root. Error if negative.
    func[Big] isqrt() {
        if (this.isNegative()) throw "isqrt of a negative number";
        if (this.isZero()) return this;
        var[Big] x = new Big(10).pow(Int((this.length() + 1) / 2));
        var[Big] next = x.add(this.div(x)).div(2);
        while (next.compare(x) < 0) {
            x = next;
            next = x.add(this.div(x)).div(2);
        }
        return x;
    }

    func[Big] pow(exponent[Int]) { return this._make(_bigPow(this.digits, exponent)); }

    func[Big] negate() { return this._make(_bigSub("0", this.digits)); }

    func[Big] abs() { return this.isNegative() ? this.negate() : this; }

    // ---- Comparing ----
    // -1 if smaller than `other`, 0 if equal, 1 if bigger.
    func[Int] compare(other[Big | Int]) { return _bigCompare(this.digits, this._of(other)); }
    func[Bul] equals(other[Big | Int]) { return this.compare(other) == 0; }
    func[Bul] isNegative() { return this.digits.startsWith("-"); }
    func[Bul] isZero() { return this.digits == "0"; }
    func[Bul] isEven() { return "02468".includes(Str(this.digits<this.digits.length() - 1>)); }

    // -1, 0 or 1
    func[Int] sign() { return this.isZero() ? 0 : this.isNegative() ? -1 : 1; }

    // ---- Showing and converting ----
    // Scientific notation: 265252859812191058636308480000000 -> "2.65E32"
    func[Str] sci(digits[Int]=2) { return _bigSci(this.digits, digits); }

    // Number of digits.
    func[Int] length() { return this.isNegative() ? this.digits.length() - 1 : this.digits.length(); }

    // Error if it's too big for an Int.
    func[Int] toInt() { return _bigToInt(this.digits); }

    // May lose digits.
    func[Dbl] toDbl() { return _bigToDbl(this.digits); }

    // All the digits; print(big) shows these.
    func[Str] toStr() { return this.digits; }

    // Operators: a + b, a * 2, a < b, a == b, -a, a ^ 3
    func[Big] _add(other[Big | Int]) { return this.add(other); }
    func[Big] _sub(other[Big | Int]) { return this.sub(other); }
    func[Big] _mul(other[Big | Int]) { return this.mul(other); }
    func[Big] _div(other[Big | Int]) { return this.div(other); }
    func[Big] _mod(other[Big | Int]) { return this.mod(other); }
    func[Big] _pow(exponent[Int]) { return this.pow(exponent); }
    func[Big] _neg() { return this.negate(); }
    func[Bul] _eq(other[Big]) { return this.digits == other.digits; }
    func[Int] _cmp(other[Big]) { return this.compare(other); }
    func[Int] _hash() {
        var[Int] h = 7;
        for (var[Chr] c in this.digits) h = (h % 1000000007) * 31 + c.code();
        return h;
    }
}

// The greatest common divisor, never negative: gcd(12, 18) is 6
func[Int] gcd(a[Int], b[Int]) {
    var[Int] x = a;
    var[Int] y = b;
    while (y != 0) {
        var[Int] rest = x % y;
        x = y;
        y = rest;
    }
    return x < 0 ? -x : x;
}

// The least common multiple, never negative: lcm(4, 6) is 12
func[Int] lcm(a[Int], b[Int]) {
    if (a == 0 || b == 0) return 0;
    var[Int] out = idiv(a, gcd(a, b)) * b;
    return out < 0 ? -out : out;
}

// -1, 0 or 1
func[Int] sign(x[Dbl]) {
    return x > 0 ? 1 : x < 0 ? -1 : 0;
}

// ============================================================
// Statistics on a list of numbers (Int lists: write them as <1, 2, 3>
// or convert with .map((x[Int]) := Dbl(x)))
// ============================================================

func[Emp] _needNumbers(numbers[Dbl<>], what[Str]) {
    if (numbers.isEmpty()) throw $"{what} of an empty list";
}

// 1 + 2 + 3 = 6
func[Dbl] sum(numbers[Dbl<>]) {
    var[Dbl] total = 0;
    for (var[Dbl] n in numbers) total += n;
    return total;
}

// The average: mean(<2, 4, 9>) is 5.
func[Dbl] mean(numbers[Dbl<>]) {
    _needNumbers(numbers, "mean");
    return sum(numbers) / numbers.length();
}

// The middle value once sorted (the average of the two middle ones for an even count).
func[Dbl] median(numbers[Dbl<>]) {
    _needNumbers(numbers, "median");
    var[Dbl<>] sorted = numbers.copy();
    sorted.sort();
    var[Int] middle = Int(sorted.length() / 2);
    if (sorted.length() % 2 == 1) return sorted<middle>;
    return (sorted<middle - 1> + sorted<middle>) / 2;
}

// The value that appears most often (the first one, if several tie).
func[Dbl] mode(numbers[Dbl<>]) {
    _needNumbers(numbers, "mode");
    var[Map[Dbl, Int]] counts = {:};
    var[Dbl] best = numbers<0>;
    for (var[Dbl] n in numbers) {
        counts<n> = counts.getOr(n, 0) + 1;
        if (counts<n> > counts<best>) best = n;
    }
    return best;
}

// How spread out the numbers are (population standard deviation).
func[Dbl] stdev(numbers[Dbl<>]) {
    var[Dbl] average = mean(numbers);
    var[Dbl] squares = 0;
    for (var[Dbl] n in numbers) squares += (n - average) * (n - average);
    return sqrt(squares / numbers.length());
}
