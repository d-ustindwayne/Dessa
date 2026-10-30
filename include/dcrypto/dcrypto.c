/* $("dcrypto") -- the native functions that C programs and WebAssembly call
 * (dsx_dcrypto_<name>, see src/codegen/cglue.hpp): hashes, passwords
 * (PBKDF2-SHA256), encryption (AES-256-GCM) and text encodings. The same
 * algorithms and formats as dcrypto.hpp, so either one reads what the other
 * wrote. */
#ifdef _WIN32
#define _CRT_RAND_S
#endif
#include "dessa_rt.h"
#ifndef _WIN32
#include <sys/random.h>
#endif

/* ---- bytes ---- */

typedef struct bytes {
    uint8_t* data;
    size_t len, cap;
} bytes;

static void bytes_add(bytes* b, const uint8_t* data, size_t len) {
    if (b->len + len > b->cap) {
        b->cap = (b->len + len) * 2 + 16;
        b->data = (uint8_t*)realloc(b->data, b->cap);
    }
    if (len) memcpy(b->data + b->len, data, len);
    b->len += len;
}
static void bytes_byte(bytes* b, uint8_t v) { bytes_add(b, &v, 1); }
static bytes bytes_copy(const uint8_t* data, size_t len) {
    bytes b = {NULL, 0, 0};
    bytes_add(&b, data, len);
    return b;
}
static void bytes_free(bytes* b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}
static ds_str* text_of(const uint8_t* data, size_t len) { return ds_str_new((const char*)data, (int64_t)len); }

static ds_str* hex_of(const uint8_t* data, size_t len) {
    static const char digits[] = "0123456789abcdef";
    char* out = (char*)malloc(len * 2 + 1);
    for (size_t i = 0; i < len; ++i) {
        out[i * 2] = digits[data[i] >> 4];
        out[i * 2 + 1] = digits[data[i] & 15];
    }
    ds_str* s = ds_str_new(out, (int64_t)len * 2);
    free(out);
    return s;
}

static uint32_t rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
static uint32_t rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
static uint64_t rotr64(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }

/* ============================================================
 * SHA-256 (also used by HMAC and PBKDF2)
 * ============================================================ */

typedef struct sha256 {
    uint32_t h[8];
    uint8_t block[64];
    size_t used;
    uint64_t total;
} sha256;

static void sha256_init(sha256* s) {
    static const uint32_t start[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(s->h, start, sizeof start);
    s->used = 0;
    s->total = 0;
}

static void sha256_compress(sha256* s, const uint8_t* p) {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) | ((uint32_t)p[i * 4 + 2] << 8) | p[i * 4 + 3];
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], hh = s->h[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t t1 = hh + (rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
        uint32_t t2 = (rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += hh;
}

static void sha256_update(sha256* s, const uint8_t* data, size_t size) {
    s->total += size;
    while (size > 0) {
        size_t take = size < 64 - s->used ? size : 64 - s->used;
        memcpy(s->block + s->used, data, take);
        s->used += take; data += take; size -= take;
        if (s->used == 64) {
            sha256_compress(s, s->block);
            s->used = 0;
        }
    }
}

static void sha256_finish(sha256* s, uint8_t out[32]) {
    uint64_t bits = s->total * 8;
    uint8_t one = 0x80, zero = 0;
    sha256_update(s, &one, 1);
    while (s->used != 56) sha256_update(s, &zero, 1);
    uint8_t length[8];
    for (int i = 0; i < 8; ++i) length[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha256_update(s, length, 8);
    for (int i = 0; i < 8; ++i) for (int j = 0; j < 4; ++j) out[i * 4 + j] = (uint8_t)(s->h[i] >> (24 - 8 * j));
}

static void sha256_bytes(const uint8_t* data, size_t len, uint8_t out[32]) {
    sha256 s;
    sha256_init(&s);
    sha256_update(&s, data, len);
    sha256_finish(&s, out);
}

/* ============================================================
 * SHA-1 and MD5 (only for checksums: they are not safe for passwords)
 * ============================================================ */

/* The message padded to whole blocks of 64 bytes, its length in bits at the end */
static bytes padded(const uint8_t* input, size_t len, bool bigEndian) {
    bytes m = bytes_copy(input, len);
    uint64_t bits = (uint64_t)len * 8;
    bytes_byte(&m, 0x80);
    while (m.len % 64 != 56) bytes_byte(&m, 0);
    for (int i = 0; i < 8; ++i) bytes_byte(&m, (uint8_t)(bigEndian ? bits >> (56 - 8 * i) : bits >> (8 * i)));
    return m;
}

static void sha1_bytes(const uint8_t* input, size_t len, uint8_t out[20]) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    bytes m = padded(input, len, true);
    for (size_t off = 0; off < m.len; off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = ((uint32_t)m.data[off + i * 4] << 24) | ((uint32_t)m.data[off + i * 4 + 1] << 16) |
                   ((uint32_t)m.data[off + i * 4 + 2] << 8) | m.data[off + i * 4 + 3];
        }
        for (int i = 16; i < 80; ++i) w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t t = rotl32(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rotl32(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    bytes_free(&m);
    for (int i = 0; i < 5; ++i) for (int j = 0; j < 4; ++j) out[i * 4 + j] = (uint8_t)(h[i] >> (24 - 8 * j));
}

static void md5_bytes(const uint8_t* input, size_t len, uint8_t out[16]) {
    static const uint32_t k[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int r[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20,
                              5, 9, 14, 20, 5, 9, 14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                              6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    bytes m = padded(input, len, false);
    for (size_t off = 0; off < m.len; off += 64) {
        uint32_t w[16];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t)m.data[off + i * 4] | ((uint32_t)m.data[off + i * 4 + 1] << 8) |
                   ((uint32_t)m.data[off + i * 4 + 2] << 16) | ((uint32_t)m.data[off + i * 4 + 3] << 24);
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        for (int i = 0; i < 64; ++i) {
            uint32_t f;
            int g;
            if (i < 16) { f = (b & c) | (~b & d); g = i; }
            else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
            else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
            else { f = c ^ (b | ~d); g = (7 * i) % 16; }
            uint32_t t = d;
            d = c;
            c = b;
            b = b + rotl32(a + f + k[i] + w[g], r[i]);
            a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    }
    bytes_free(&m);
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) out[i * 4 + j] = (uint8_t)(h[i] >> (8 * j));
}

/* ============================================================
 * SHA-512
 * ============================================================ */

static void sha512_bytes(const uint8_t* input, size_t len, uint8_t out[64]) {
    static const uint64_t k[80] = {
        0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full, 0xe9b5dba58189dbbcull, 0x3956c25bf348b538ull,
        0x59f111f1b605d019ull, 0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull, 0xd807aa98a3030242ull, 0x12835b0145706fbeull,
        0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull, 0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull,
        0xc19bf174cf692694ull, 0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull, 0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull,
        0x2de92c6f592b0275ull, 0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull, 0x983e5152ee66dfabull,
        0xa831c66d2db43210ull, 0xb00327c898fb213full, 0xbf597fc7beef0ee4ull, 0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull,
        0x06ca6351e003826full, 0x142929670a0e6e70ull, 0x27b70a8546d22ffcull, 0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull,
        0x53380d139d95b3dfull, 0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull, 0x92722c851482353bull,
        0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull, 0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull, 0xd192e819d6ef5218ull,
        0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull, 0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull,
        0x2748774cdf8eeb99ull, 0x34b0bcb5e19b48a8ull, 0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull, 0x5b9cca4f7763e373ull,
        0x682e6ff3d6b2b8a3ull, 0x748f82ee5defb2fcull, 0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
        0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull, 0xc67178f2e372532bull, 0xca273eceea26619cull,
        0xd186b8c721c0c207ull, 0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull, 0x06f067aa72176fbaull, 0x0a637dc5a2c898a6ull,
        0x113f9804bef90daeull, 0x1b710b35131c471bull, 0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull,
        0x431d67c49c100d4cull, 0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull, 0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull};
    uint64_t h[8] = {0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,
                     0x510e527fade682d1ull, 0x9b05688c2b3e6c1full, 0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull};
    bytes m = bytes_copy(input, len);
    uint64_t bits = (uint64_t)len * 8;
    bytes_byte(&m, 0x80);
    while (m.len % 128 != 112) bytes_byte(&m, 0);
    for (int i = 0; i < 8; ++i) bytes_byte(&m, 0);   /* the length's top 64 bits */
    for (int i = 0; i < 8; ++i) bytes_byte(&m, (uint8_t)(bits >> (56 - 8 * i)));
    for (size_t off = 0; off < m.len; off += 128) {
        uint64_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = 0;
            for (int j = 0; j < 8; ++j) w[i] = (w[i] << 8) | m.data[off + i * 8 + j];
        }
        for (int i = 16; i < 80; ++i) {
            uint64_t s0 = rotr64(w[i - 15], 1) ^ rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
            uint64_t s1 = rotr64(w[i - 2], 19) ^ rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint64_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 80; ++i) {
            uint64_t t1 = hh + (rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            uint64_t t2 = (rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    bytes_free(&m);
    for (int i = 0; i < 8; ++i) for (int j = 0; j < 8; ++j) out[i * 8 + j] = (uint8_t)(h[i] >> (56 - 8 * j));
}

/* ============================================================
 * HMAC-SHA256 and PBKDF2
 * ============================================================ */

/* The two SHA-256 states after the key's inner and outer pads, so each HMAC
 * of a short message costs two compressions */
typedef struct hmac_key {
    sha256 inner, outer;
} hmac_key;

static void hmac_init(hmac_key* h, const uint8_t* raw, size_t len) {
    uint8_t key[64] = {0};
    if (len > 64) sha256_bytes(raw, len, key);
    else memcpy(key, raw, len);
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = key[i] ^ 0x36;
        opad[i] = key[i] ^ 0x5c;
    }
    sha256_init(&h->inner);
    sha256_init(&h->outer);
    sha256_update(&h->inner, ipad, 64);
    sha256_update(&h->outer, opad, 64);
}

static void hmac_mac(const hmac_key* h, const uint8_t* message, size_t len, uint8_t out[32]) {
    sha256 in = h->inner;
    sha256_update(&in, message, len);
    uint8_t innerHash[32];
    sha256_finish(&in, innerHash);
    sha256 o = h->outer;
    sha256_update(&o, innerHash, 32);
    sha256_finish(&o, out);
}

static void pbkdf2(const uint8_t* password, size_t passwordLen, const uint8_t* salt, size_t saltLen, int iterations, uint8_t* out,
                   size_t length) {
    hmac_key key;
    hmac_init(&key, password, passwordLen);
    size_t done = 0;
    for (uint32_t block = 1; done < length; ++block) {
        bytes first = bytes_copy(salt, saltLen);
        for (int i = 0; i < 4; ++i) bytes_byte(&first, (uint8_t)(block >> (24 - 8 * i)));
        uint8_t u[32], t[32];
        hmac_mac(&key, first.data, first.len, u);
        bytes_free(&first);
        memcpy(t, u, 32);
        for (int i = 1; i < iterations; ++i) {
            uint8_t next[32];
            hmac_mac(&key, u, 32, next);
            memcpy(u, next, 32);
            for (int j = 0; j < 32; ++j) t[j] ^= u[j];
        }
        size_t take = length - done < 32 ? length - done : 32;
        memcpy(out + done, t, take);
        done += take;
    }
}

/* ============================================================
 * AES-256 (encryption of blocks; GCM only needs that direction)
 * ============================================================ */

static const uint8_t sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76, 0xca, 0x82, 0xc9, 0x7d,
    0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc,
    0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15, 0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2,
    0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf, 0xd0, 0xef, 0xaa, 0xfb,
    0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8, 0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
    0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d,
    0x64, 0x5d, 0x19, 0x73, 0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d,
    0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08, 0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6,
    0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a, 0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9,
    0x86, 0xc1, 0x1d, 0x9e, 0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16};

static uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0)); }

typedef struct aes256 {
    uint8_t roundKeys[240];
} aes256;

static void aes_init(aes256* aes, const uint8_t key[32]) {
    memcpy(aes->roundKeys, key, 32);
    uint8_t rcon = 1;
    for (int i = 8; i < 60; ++i) {
        uint8_t t[4];
        memcpy(t, aes->roundKeys + (i - 1) * 4, 4);
        if (i % 8 == 0) {
            uint8_t first = t[0];
            t[0] = (uint8_t)(sbox[t[1]] ^ rcon);
            t[1] = sbox[t[2]];
            t[2] = sbox[t[3]];
            t[3] = sbox[first];
            rcon = xtime(rcon);
        } else if (i % 8 == 4) {
            for (int j = 0; j < 4; ++j) t[j] = sbox[t[j]];
        }
        for (int j = 0; j < 4; ++j) aes->roundKeys[i * 4 + j] = aes->roundKeys[(i - 8) * 4 + j] ^ t[j];
    }
}

static void aes_encrypt(const aes256* aes, uint8_t state[16]) {
    for (int i = 0; i < 16; ++i) state[i] ^= aes->roundKeys[i];
    for (int round = 1; round <= 14; ++round) {
        for (int i = 0; i < 16; ++i) state[i] = sbox[state[i]];
        /* shift rows (the state is column by column) */
        uint8_t t[16];
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) t[c * 4 + r] = state[((c + r) % 4) * 4 + r];
        memcpy(state, t, 16);
        if (round != 14) {
            for (int c = 0; c < 4; ++c) {
                uint8_t* col = state + c * 4;
                uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                uint8_t all = a0 ^ a1 ^ a2 ^ a3;
                col[0] ^= all ^ xtime(a0 ^ a1);
                col[1] ^= all ^ xtime(a1 ^ a2);
                col[2] ^= all ^ xtime(a2 ^ a3);
                col[3] ^= all ^ xtime(a3 ^ a0);
            }
        }
        for (int i = 0; i < 16; ++i) state[i] ^= aes->roundKeys[round * 16 + i];
    }
}

/* ============================================================
 * GCM: AES in counter mode plus an authentication tag
 * ============================================================ */

typedef struct block128 {
    uint64_t hi, lo;
} block128;

static block128 load128(const uint8_t* p) {
    block128 b = {0, 0};
    for (int i = 0; i < 8; ++i) b.hi = (b.hi << 8) | p[i];
    for (int i = 8; i < 16; ++i) b.lo = (b.lo << 8) | p[i];
    return b;
}

static void store128(block128 b, uint8_t* p) {
    for (int i = 0; i < 8; ++i) p[i] = (uint8_t)(b.hi >> (56 - 8 * i));
    for (int i = 0; i < 8; ++i) p[8 + i] = (uint8_t)(b.lo >> (56 - 8 * i));
}

/* Multiplication in GF(2^128) as GCM defines it */
static block128 gf_mul(block128 x, block128 y) {
    block128 z = {0, 0}, v = y;
    for (int i = 0; i < 128; ++i) {
        const bool bit = i < 64 ? (x.hi >> (63 - i)) & 1 : (x.lo >> (127 - i)) & 1;
        if (bit) {
            z.hi ^= v.hi;
            z.lo ^= v.lo;
        }
        const bool low = v.lo & 1;
        v.lo = (v.lo >> 1) | (v.hi << 63);
        v.hi >>= 1;
        if (low) v.hi ^= 0xe100000000000000ull;
    }
    return z;
}

static void gcm_tag(const aes256* aes, const uint8_t nonce[12], const uint8_t* cipher, size_t len, uint8_t tag[16]) {
    uint8_t zero[16] = {0};
    aes_encrypt(aes, zero);
    const block128 h = load128(zero);
    block128 y = {0, 0};
    for (size_t off = 0; off < len; off += 16) {
        uint8_t block[16] = {0};
        memcpy(block, cipher + off, len - off < 16 ? len - off : 16);
        block128 c = load128(block);
        y.hi ^= c.hi;
        y.lo ^= c.lo;
        y = gf_mul(y, h);
    }
    y.lo ^= (uint64_t)len * 8;   /* the lengths block: no extra authenticated data */
    y = gf_mul(y, h);
    uint8_t j0[16] = {0};
    memcpy(j0, nonce, 12);
    j0[15] = 1;
    aes_encrypt(aes, j0);
    store128(y, tag);
    for (int i = 0; i < 16; ++i) tag[i] ^= j0[i];
}

static void gcm_crypt(const aes256* aes, const uint8_t nonce[12], const uint8_t* data, size_t len, uint8_t* out) {
    uint32_t counter = 2;
    for (size_t off = 0; off < len; off += 16, ++counter) {
        uint8_t block[16] = {0};
        memcpy(block, nonce, 12);
        for (int i = 0; i < 4; ++i) block[12 + i] = (uint8_t)(counter >> (24 - 8 * i));
        aes_encrypt(aes, block);
        for (size_t i = 0; i < 16 && off + i < len; ++i) out[off + i] = data[off + i] ^ block[i];
    }
}

/* ============================================================
 * Text encodings and secure random bytes
 * ============================================================ */

static ds_str* base64_of(const uint8_t* data, size_t len) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    ds_sb b;
    ds_sb_init(&b);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (uint32_t)data[i] << 16;
        if (i + 1 < len) n |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) n |= data[i + 2];
        char four[4] = {table[(n >> 18) & 63], table[(n >> 12) & 63], i + 1 < len ? table[(n >> 6) & 63] : '=',
                        i + 2 < len ? table[n & 63] : '='};
        ds_sb_bytes(&b, four, 4);
    }
    return ds_sb_done(&b);
}

static int base64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

static bytes bytes_of_base64(const char* text, size_t len, const char* what) {
    bytes out = {NULL, 0, 0};
    uint32_t buffer = 0;
    int bits = 0;
    for (size_t i = 0; i < len; ++i) {
        char c = text[i];
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        int v = base64_value(c);
        if (v < 0) {
            bytes_free(&out);
            ds_fail("%s: not valid base64 text", what);
        }
        buffer = (buffer << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes_byte(&out, (uint8_t)(buffer >> bits));
        }
    }
    return out;
}

static void random_bytes(uint8_t* out, size_t count) {
#ifdef _WIN32
    for (size_t i = 0; i < count; i += 4) {
        unsigned int r = 0;
        rand_s(&r);
        for (size_t j = 0; j < 4 && i + j < count; ++j) out[i + j] = (uint8_t)(r >> (8 * j));
    }
#else
    for (size_t at = 0; at < count; at += 256) {
        size_t n = count - at < 256 ? count - at : 256;
        if (getentropy(out + at, n) != 0) ds_fail("dcrypto: no secure random numbers on this system");
    }
#endif
}

static bool same_bytes(const uint8_t* a, size_t alen, const uint8_t* b, size_t blen) {
    if (alen != blen) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < alen; ++i) diff |= a[i] ^ b[i];   /* takes the same time either way */
    return diff == 0;
}

#define ITERATIONS 100000

/* ============================================================
 * The library
 * ============================================================ */

ds_str* dsx_dcrypto_md5(ds_str* text) {
    uint8_t out[16];
    md5_bytes((const uint8_t*)text->data, (size_t)text->len, out);
    return hex_of(out, 16);
}

ds_str* dsx_dcrypto_sha1(ds_str* text) {
    uint8_t out[20];
    sha1_bytes((const uint8_t*)text->data, (size_t)text->len, out);
    return hex_of(out, 20);
}

ds_str* dsx_dcrypto_sha256(ds_str* text) {
    uint8_t out[32];
    sha256_bytes((const uint8_t*)text->data, (size_t)text->len, out);
    return hex_of(out, 32);
}

ds_str* dsx_dcrypto_sha512(ds_str* text) {
    uint8_t out[64];
    sha512_bytes((const uint8_t*)text->data, (size_t)text->len, out);
    return hex_of(out, 64);
}

ds_str* dsx_dcrypto_hmac(ds_str* key, ds_str* text) {
    hmac_key k;
    hmac_init(&k, (const uint8_t*)key->data, (size_t)key->len);
    uint8_t out[32];
    hmac_mac(&k, (const uint8_t*)text->data, (size_t)text->len, out);
    return hex_of(out, 32);
}

ds_str* dsx_dcrypto_hashPassword(ds_str* password) {
    uint8_t salt[16], hash[32];
    random_bytes(salt, 16);
    pbkdf2((const uint8_t*)password->data, (size_t)password->len, salt, 16, ITERATIONS, hash, 32);
    ds_sb b;
    ds_sb_init(&b);
    ds_sb_cstr(&b, "pbkdf2-sha256$");
    ds_sb_int(&b, ITERATIONS);
    ds_sb_cstr(&b, "$");
    ds_sb_take(&b, base64_of(salt, 16));
    ds_sb_cstr(&b, "$");
    ds_sb_take(&b, base64_of(hash, 32));
    return ds_sb_done(&b);
}

DS_NORETURN static void not_a_hash(void) { ds_fail("verifyPassword: not a hash made by hashPassword"); }

ds_bul dsx_dcrypto_verifyPassword(ds_str* password, ds_str* stored) {
    /* pbkdf2-sha256$<iterations>$<salt>$<hash> */
    const char* parts[4];
    size_t lens[4];
    int count = 0;
    const char* start = stored->data;
    const char* end = stored->data + stored->len;
    for (const char* p = start;; ++p) {
        if (p == end || *p == '$') {
            if (count == 4) not_a_hash();
            parts[count] = start;
            lens[count++] = (size_t)(p - start);
            if (p == end) break;
            start = p + 1;
        }
    }
    if (count != 4 || lens[0] != 13 || memcmp(parts[0], "pbkdf2-sha256", 13) != 0) not_a_hash();
    ds_str* number = ds_str_new(parts[1], (int64_t)lens[1]);
    ds_int iterations = ds_str_to_int(number);
    ds_release(number);
    if (iterations < 1 || iterations > 10000000) not_a_hash();
    bytes salt = bytes_of_base64(parts[2], lens[2], "verifyPassword");
    bytes expected = bytes_of_base64(parts[3], lens[3], "verifyPassword");
    uint8_t* actual = (uint8_t*)malloc(expected.len + 1);
    pbkdf2((const uint8_t*)password->data, (size_t)password->len, salt.data, salt.len, (int)iterations, actual, expected.len);
    bool same = same_bytes(actual, expected.len, expected.data, expected.len);
    free(actual);
    bytes_free(&salt);
    bytes_free(&expected);
    return same;
}

/* base64 of: version (1) | salt (16) | nonce (12) | encrypted text | tag (16) */
ds_str* dsx_dcrypto_encrypt(ds_str* text, ds_str* password) {
    uint8_t salt[16], nonce[12], key[32], tag[16];
    random_bytes(salt, 16);
    random_bytes(nonce, 12);
    pbkdf2((const uint8_t*)password->data, (size_t)password->len, salt, 16, ITERATIONS, key, 32);
    aes256 aes;
    aes_init(&aes, key);
    const size_t len = (size_t)text->len;
    uint8_t* cipher = (uint8_t*)malloc(len + 1);
    gcm_crypt(&aes, nonce, (const uint8_t*)text->data, len, cipher);
    gcm_tag(&aes, nonce, cipher, len, tag);
    bytes out = {NULL, 0, 0};
    bytes_byte(&out, 1);
    bytes_add(&out, salt, 16);
    bytes_add(&out, nonce, 12);
    bytes_add(&out, cipher, len);
    bytes_add(&out, tag, 16);
    free(cipher);
    ds_str* result = base64_of(out.data, out.len);
    bytes_free(&out);
    return result;
}

ds_str* dsx_dcrypto_decrypt(ds_str* data, ds_str* password) {
    bytes all = bytes_of_base64(data->data, (size_t)data->len, "decrypt");
    if (all.len < 1 + 16 + 12 + 16 || all.data[0] != 1) {
        bytes_free(&all);
        ds_fail("decrypt: this isn't text made by encrypt");
    }
    const uint8_t* salt = all.data + 1;
    const uint8_t* nonce = all.data + 17;
    const uint8_t* cipher = all.data + 29;
    const size_t len = all.len - 29 - 16;
    const uint8_t* tag = all.data + all.len - 16;
    uint8_t key[32], actual[16];
    pbkdf2((const uint8_t*)password->data, (size_t)password->len, salt, 16, ITERATIONS, key, 32);
    aes256 aes;
    aes_init(&aes, key);
    gcm_tag(&aes, nonce, cipher, len, actual);
    if (!same_bytes(actual, 16, tag, 16)) {
        bytes_free(&all);
        ds_fail("decrypt: wrong password, or the text was changed");
    }
    uint8_t* plain = (uint8_t*)malloc(len + 1);
    gcm_crypt(&aes, nonce, cipher, len, plain);
    ds_str* result = text_of(plain, len);
    free(plain);
    bytes_free(&all);
    return result;
}

ds_str* dsx_dcrypto_toBase64(ds_str* text) { return base64_of((const uint8_t*)text->data, (size_t)text->len); }

ds_str* dsx_dcrypto_fromBase64(ds_str* text) {
    bytes b = bytes_of_base64(text->data, (size_t)text->len, "fromBase64");
    ds_str* result = text_of(b.data, b.len);
    bytes_free(&b);
    return result;
}

ds_str* dsx_dcrypto_toHex(ds_str* text) { return hex_of((const uint8_t*)text->data, (size_t)text->len); }

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

ds_str* dsx_dcrypto_fromHex(ds_str* text) {
    if (text->len % 2 != 0) ds_fail("fromHex: the text must have an even number of digits");
    uint8_t* out = (uint8_t*)malloc((size_t)text->len / 2 + 1);
    for (int64_t i = 0; i < text->len; i += 2) {
        int hi = hex_digit(text->data[i]), lo = hex_digit(text->data[i + 1]);
        if (hi < 0 || lo < 0) {
            free(out);
            ds_fail("fromHex: not a hex digit in \"%s\"", text->data);
        }
        out[i / 2] = (uint8_t)(hi * 16 + lo);
    }
    ds_str* result = text_of(out, (size_t)text->len / 2);
    free(out);
    return result;
}

/* Random hex text from `bytes` secure random bytes (token(16) gives 32 digits) */
ds_str* dsx_dcrypto_token(ds_int count) {
    if (count < 1 || count > 1024) ds_fail("token: bytes must be from 1 to 1024");
    uint8_t out[1024];
    random_bytes(out, (size_t)count);
    return hex_of(out, (size_t)count);
}
