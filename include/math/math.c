/* $("math") -- the native functions that C programs and WebAssembly call
 * (dsx_math_<name>, see src/codegen/cglue.hpp): whole numbers of any size
 * for class Big in math.dm, which keeps them as digit text. The other math
 * functions (sqrt, floor, ...) are in the runtime. */
#include "dessa_rt.h"
#include <ctype.h>

#define BASE 1000000000u

/* A number as limbs of 9 digits, least significant first */
typedef struct big {
    bool negative;
    int64_t len, cap;
    uint32_t* limbs;
} big;

static void big_push(big* b, uint32_t limb) {
    if (b->len == b->cap) {
        b->cap = b->cap ? b->cap * 2 : 4;
        b->limbs = (uint32_t*)realloc(b->limbs, (size_t)b->cap * sizeof(uint32_t));
    }
    b->limbs[b->len++] = limb;
}
static void big_free(big* b) {
    free(b->limbs);
    b->limbs = NULL;
    b->len = b->cap = 0;
}
static void big_trim(big* b) {
    while (b->len > 0 && b->limbs[b->len - 1] == 0) b->len--;
    if (b->len == 0) b->negative = false;
}
static big big_copy(const big* a) {
    big r = {a->negative, 0, 0, NULL};
    for (int64_t i = 0; i < a->len; ++i) big_push(&r, a->limbs[i]);
    return r;
}

DS_NORETURN static void not_a_number(ds_str* text) {
    ds_fail("Cannot convert \"%s\" to Big", text->data);
}

static big big_parse(ds_str* text) {
    big r = {false, 0, 0, NULL};
    int64_t i = 0, end = text->len;
    while (i < end && isspace((unsigned char)text->data[i])) ++i;
    bool neg = false;
    if (i < end && (text->data[i] == '-' || text->data[i] == '+')) neg = text->data[i++] == '-';
    while (end > i && isspace((unsigned char)text->data[end - 1])) --end;
    if (i == end) not_a_number(text);
    for (int64_t k = i; k < end; ++k) {
        if (!isdigit((unsigned char)text->data[k])) not_a_number(text);
    }
    for (int64_t stop = end; stop > i;) {
        int64_t start = stop >= i + 9 ? stop - 9 : i;
        uint32_t limb = 0;
        for (int64_t k = start; k < stop; ++k) limb = limb * 10 + (uint32_t)(text->data[k] - '0');
        big_push(&r, limb);
        stop = start;
    }
    big_trim(&r);
    r.negative = neg && r.len > 0;
    return r;
}

static big big_of_int(ds_int value) {
    big r = {value < 0, 0, 0, NULL};
    uint64_t magnitude = value < 0 ? 0ULL - (uint64_t)value : (uint64_t)value;
    while (magnitude) {
        big_push(&r, (uint32_t)(magnitude % BASE));
        magnitude /= BASE;
    }
    return r;
}

/* The digits as text (owned); the big is freed */
static ds_str* big_text(big* b) {
    if (b->len == 0) {
        big_free(b);
        return ds_str_cstr("0");
    }
    ds_sb s;
    ds_sb_init(&s);
    if (b->negative) ds_sb_cstr(&s, "-");
    char buffer[16];
    snprintf(buffer, sizeof buffer, "%u", b->limbs[b->len - 1]);
    ds_sb_cstr(&s, buffer);
    for (int64_t i = b->len - 1; i-- > 0;) {
        snprintf(buffer, sizeof buffer, "%09u", b->limbs[i]);
        ds_sb_cstr(&s, buffer);
    }
    big_free(b);
    return ds_sb_done(&s);
}

static int compare_magnitude(const big* a, const big* b) {
    if (a->len != b->len) return a->len < b->len ? -1 : 1;
    for (int64_t i = a->len; i-- > 0;) {
        if (a->limbs[i] != b->limbs[i]) return a->limbs[i] < b->limbs[i] ? -1 : 1;
    }
    return 0;
}

static int big_compare(const big* a, const big* b) {
    if (a->negative != b->negative) return a->negative ? -1 : 1;
    int c = compare_magnitude(a, b);
    return a->negative ? -c : c;
}

static big add_magnitude(const big* a, const big* b) {
    big r = {false, 0, 0, NULL};
    uint64_t carry = 0;
    for (int64_t i = 0; i < a->len || i < b->len || carry; ++i) {
        uint64_t cur = carry + (i < a->len ? a->limbs[i] : 0) + (i < b->len ? b->limbs[i] : 0);
        big_push(&r, (uint32_t)(cur % BASE));
        carry = cur / BASE;
    }
    return r;
}

/* |a| - |b| where |a| >= |b| */
static big subtract_magnitude(const big* a, const big* b) {
    big r = {false, 0, 0, NULL};
    int64_t borrow = 0;
    for (int64_t i = 0; i < a->len; ++i) {
        int64_t cur = (int64_t)a->limbs[i] - borrow - (i < b->len ? (int64_t)b->limbs[i] : 0);
        borrow = cur < 0;
        if (cur < 0) cur += BASE;
        big_push(&r, (uint32_t)cur);
    }
    big_trim(&r);
    return r;
}

/* a + b, signs included (with flip: a - b) */
static big big_add(const big* a, const big* b, bool flip) {
    const bool bneg = flip ? (b->len > 0 && !b->negative) : b->negative;
    if (a->negative == bneg) {
        big r = add_magnitude(a, b);
        r.negative = a->negative && r.len > 0;
        return r;
    }
    if (compare_magnitude(a, b) >= 0) {
        big r = subtract_magnitude(a, b);
        r.negative = a->negative && r.len > 0;
        return r;
    }
    big r = subtract_magnitude(b, a);
    r.negative = bneg && r.len > 0;
    return r;
}

static big big_mul(const big* a, const big* b) {
    big r = {false, 0, 0, NULL};
    if (a->len == 0 || b->len == 0) return r;
    const int64_t n = a->len + b->len + 1;
    uint64_t* accum = (uint64_t*)calloc((size_t)n, sizeof(uint64_t));
    for (int64_t i = 0; i < a->len; ++i) {
        uint64_t carry = 0;
        for (int64_t j = 0; j < b->len || carry; ++j) {
            uint64_t cur = accum[i + j] + carry + (j < b->len ? (uint64_t)a->limbs[i] * b->limbs[j] : 0);
            accum[i + j] = cur % BASE;
            carry = cur / BASE;
        }
    }
    for (int64_t i = 0; i < n; ++i) big_push(&r, (uint32_t)accum[i]);
    free(accum);
    big_trim(&r);
    r.negative = (a->negative != b->negative) && r.len > 0;
    return r;
}

static big multiply_small(const big* a, uint32_t m) {
    big r = {false, 0, 0, NULL};
    uint64_t carry = 0;
    for (int64_t i = 0; i < a->len || carry; ++i) {
        uint64_t cur = carry + (i < a->len ? (uint64_t)a->limbs[i] * m : 0);
        big_push(&r, (uint32_t)(cur % BASE));
        carry = cur / BASE;
    }
    big_trim(&r);
    return r;
}

/* Whole-number division rounding toward zero: quotient and remainder */
static void big_divide(const big* a, const big* b, big* quotient, big* remainder) {
    if (b->len == 0) ds_fail("Division by zero");
    big divisor = big_copy(b);
    divisor.negative = false;
    big q = {false, 0, 0, NULL}, rem = {false, 0, 0, NULL};
    for (int64_t i = 0; i < a->len; ++i) big_push(&q, 0);
    for (int64_t i = a->len; i-- > 0;) {
        /* rem = rem * BASE + a[i] */
        big_push(&rem, 0);
        for (int64_t k = rem.len - 1; k > 0; --k) rem.limbs[k] = rem.limbs[k - 1];
        rem.limbs[0] = a->limbs[i];
        big_trim(&rem);
        /* largest digit d with divisor * d <= rem */
        uint32_t low = 0, high = BASE - 1;
        while (low < high) {
            uint32_t mid = low + (high - low + 1) / 2;
            big product = multiply_small(&divisor, mid);
            int c = compare_magnitude(&product, &rem);
            big_free(&product);
            if (c <= 0) low = mid;
            else high = mid - 1;
        }
        q.limbs[i] = low;
        big product = multiply_small(&divisor, low);
        big next = subtract_magnitude(&rem, &product);
        big_free(&product);
        big_free(&rem);
        rem = next;
    }
    big_free(&divisor);
    big_trim(&q);
    q.negative = (a->negative != b->negative) && q.len > 0;
    rem.negative = a->negative && rem.len > 0;
    *quotient = q;
    *remainder = rem;
}

ds_str* dsx_math__bigNormalize(ds_str* text) {
    big a = big_parse(text);
    return big_text(&a);
}

ds_str* dsx_math__bigAdd(ds_str* x, ds_str* y) {
    big a = big_parse(x), b = big_parse(y);
    big r = big_add(&a, &b, false);
    big_free(&a);
    big_free(&b);
    return big_text(&r);
}

ds_str* dsx_math__bigSub(ds_str* x, ds_str* y) {
    big a = big_parse(x), b = big_parse(y);
    big r = big_add(&a, &b, true);
    big_free(&a);
    big_free(&b);
    return big_text(&r);
}

ds_str* dsx_math__bigMul(ds_str* x, ds_str* y) {
    big a = big_parse(x), b = big_parse(y);
    big r = big_mul(&a, &b);
    big_free(&a);
    big_free(&b);
    return big_text(&r);
}

ds_str* dsx_math__bigDiv(ds_str* x, ds_str* y) {
    big a = big_parse(x), b = big_parse(y), q, r;
    big_divide(&a, &b, &q, &r);
    big_free(&a);
    big_free(&b);
    big_free(&r);
    return big_text(&q);
}

/* Like Int: % is never negative */
ds_str* dsx_math__bigMod(ds_str* x, ds_str* y) {
    big a = big_parse(x), b = big_parse(y), q, r;
    big_divide(&a, &b, &q, &r);
    big_free(&q);
    if (r.negative) {
        big positive = big_copy(&b);
        positive.negative = false;
        big fixed = big_add(&r, &positive, false);
        big_free(&positive);
        big_free(&r);
        r = fixed;
    }
    big_free(&a);
    big_free(&b);
    return big_text(&r);
}

ds_str* dsx_math__bigPow(ds_str* x, ds_int exponent) {
    if (exponent < 0) ds_fail("Big.pow: the exponent can't be negative");
    big base = big_parse(x), result = big_of_int(1);
    for (ds_int e = exponent; e > 0; e >>= 1) {
        if (e & 1) {
            big next = big_mul(&result, &base);
            big_free(&result);
            result = next;
        }
        if (e > 1) {
            big next = big_mul(&base, &base);
            big_free(&base);
            base = next;
        }
    }
    big_free(&base);
    return big_text(&result);
}

ds_int dsx_math__bigCompare(ds_str* x, ds_str* y) {
    big a = big_parse(x), b = big_parse(y);
    int c = big_compare(&a, &b);
    big_free(&a);
    big_free(&b);
    return c;
}

/* Scientific notation: 213000000 with 2 digits -> "2.13E8" */
ds_str* dsx_math__bigSci(ds_str* x, ds_int digits) {
    big a = big_parse(x);
    ds_str* full = big_text(&a);
    const char* text = full->data;
    int64_t len = full->len;
    bool neg = len > 0 && text[0] == '-';
    if (neg) {
        text++;
        len--;
    }
    if (digits < 0) digits = 0;
    int64_t exponent = len - 1;
    int64_t keep = len < digits + 1 ? len : digits + 1;
    char* kept = (char*)malloc((size_t)keep + 2);
    memcpy(kept, text, (size_t)keep);
    kept[keep] = 0;
    if (len > keep && text[keep] >= '5') {
        int64_t k = keep - 1;
        while (k >= 0 && kept[k] == '9') kept[k--] = '0';
        if (k < 0) {
            memmove(kept + 1, kept, (size_t)keep);
            kept[0] = '1';
            kept[keep] = 0;   /* the same number of digits: the last one drops */
            ++exponent;
        } else {
            ++kept[k];
        }
    }
    ds_sb s;
    ds_sb_init(&s);
    if (neg) ds_sb_cstr(&s, "-");
    ds_sb_bytes(&s, kept, 1);
    if (keep > 1) {
        ds_sb_cstr(&s, ".");
        ds_sb_bytes(&s, kept + 1, keep - 1);
    }
    ds_sb_cstr(&s, "E");
    ds_sb_int(&s, exponent);
    free(kept);
    ds_release(full);
    return ds_sb_done(&s);
}

ds_int dsx_math__bigToInt(ds_str* x) {
    big a = big_parse(x);
    big most = big_of_int(INT64_MAX), least = big_of_int(INT64_MIN);
    bool tooBig = a.len > 3 || big_compare(&a, &most) > 0 || big_compare(&a, &least) < 0;
    big_free(&most);
    big_free(&least);
    if (tooBig) {
        big_free(&a);
        ds_str* sci = dsx_math__bigSci(x, 2);
        ds_fail("Cannot convert %s to Int: it's too big (Int goes up to about 9.22E18)", sci->data);
    }
    uint64_t magnitude = 0;
    for (int64_t i = a.len; i-- > 0;) magnitude = magnitude * BASE + a.limbs[i];
    bool neg = a.negative;
    big_free(&a);
    return neg ? (ds_int)(0ULL - magnitude) : (ds_int)magnitude;
}

ds_dbl dsx_math__bigToDbl(ds_str* x) {
    big a = big_parse(x);
    double result = 0;
    for (int64_t i = a.len; i-- > 0;) result = result * BASE + a.limbs[i];
    bool neg = a.negative;
    big_free(&a);
    return neg ? -result : result;
}
