/***
 * $("math") -- the Dessa part: class Big, a helper for whole numbers of any
 * size (like Java's BigInteger). The native functions are in math.hpp and
 * declared in math.ds.
 *
 *     { Big } from $("math");
 *     var[Big] f = new Big(1);
 *     for (var[Int] i = 1; i <= 30; i++) f = f.mul(i);
 *     print(f);           // 265252859812191058636308480000000
 *     print(f.sci());     // 2.65E32
 *
 * Every method returns a new Big; a Big never changes. Methods that take
 * another number accept a Big or an Int.
 */

{ _bigNormalize, _bigAdd, _bigSub, _bigMul, _bigDiv, _bigMod, _bigPow, _bigCompare, _bigSci, _bigToInt, _bigToDbl, sqrt } from $("math");

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

    func[Big] pow(exponent[Int]) { return this._make(_bigPow(this.digits, exponent)); }

    func[Big] negate() { return this._make(_bigSub("0", this.digits)); }

    func[Big] abs() { return this.isNegative() ? this.negate() : this; }

    // ---- Comparing ----
    // -1 if smaller than `other`, 0 if equal, 1 if bigger.
    func[Int] compare(other[Big | Int]) { return _bigCompare(this.digits, this._of(other)); }
    func[Bul] equals(other[Big | Int]) { return this.compare(other) == 0; }
    func[Bul] isNegative() { return this.digits.startsWith("-"); }
    func[Bul] isZero() { return this.digits == "0"; }

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
}

// ============================================================
// Statistics on a list of numbers (Int lists: write them as [1, 2, 3]
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

// The average: mean([2, 4, 9]) is 5.
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
