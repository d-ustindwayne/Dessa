/* Text in other encodings: UTF-8 is what Str holds, and these convert to and
 * from UTF-16 and UTF-32. Used by the C runtime, the C++ runtime and the
 * compiler (which reads source files saved in them), so it is plain C with
 * nothing but the C library. */
#ifndef DESSA_ENCODING_H
#define DESSA_ENCODING_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    DS_ENC_UTF8,      /* "utf-8": a BOM is dropped when reading */
    DS_ENC_UTF8_BOM,  /* "utf-8-bom": written with a BOM */
    DS_ENC_UTF16,     /* "utf-16": little-endian with a BOM when written; reading follows the BOM */
    DS_ENC_UTF16LE,   /* "utf-16le", "utf-16be", "utf-32le", "utf-32be": no BOM written */
    DS_ENC_UTF16BE,
    DS_ENC_UTF32,     /* "utf-32": as utf-16 */
    DS_ENC_UTF32LE,
    DS_ENC_UTF32BE,
    DS_ENC_AUTO       /* "auto", reading only: by the BOM, UTF-8 without one */
};

/* The encoding named (any case; "utf8" and "utf_8" work too), or -1 */
static inline int ds_enc_named(const char* name) {
    char s[16];
    size_t n = 0;
    for (; name[n]; ++n) {
        if (n + 1 >= sizeof s) return -1;
        s[n] = (name[n] >= 'A' && name[n] <= 'Z') ? (char)(name[n] + 32) : name[n] == '_' ? '-' : name[n];
    }
    s[n] = 0;
    if (!strcmp(s, "utf-8") || !strcmp(s, "utf8")) return DS_ENC_UTF8;
    if (!strcmp(s, "utf-8-bom") || !strcmp(s, "utf8-bom")) return DS_ENC_UTF8_BOM;
    if (!strcmp(s, "utf-16") || !strcmp(s, "utf16")) return DS_ENC_UTF16;
    if (!strcmp(s, "utf-16le") || !strcmp(s, "utf16le")) return DS_ENC_UTF16LE;
    if (!strcmp(s, "utf-16be") || !strcmp(s, "utf16be")) return DS_ENC_UTF16BE;
    if (!strcmp(s, "utf-32") || !strcmp(s, "utf32")) return DS_ENC_UTF32;
    if (!strcmp(s, "utf-32le") || !strcmp(s, "utf32le")) return DS_ENC_UTF32LE;
    if (!strcmp(s, "utf-32be") || !strcmp(s, "utf32be")) return DS_ENC_UTF32BE;
    if (!strcmp(s, "auto")) return DS_ENC_AUTO;
    return -1;
}

/* Is there a BOM? Its encoding (or -1), and its size. UTF-32 LE's BOM starts
 * like UTF-16 LE's, so it is looked at first. */
static inline int ds_enc_bom(const unsigned char* in, size_t n, size_t* size) {
    *size = 0;
    if (n >= 4 && in[0] == 0xFF && in[1] == 0xFE && in[2] == 0 && in[3] == 0) { *size = 4; return DS_ENC_UTF32LE; }
    if (n >= 4 && in[0] == 0 && in[1] == 0 && in[2] == 0xFE && in[3] == 0xFF) { *size = 4; return DS_ENC_UTF32BE; }
    if (n >= 3 && in[0] == 0xEF && in[1] == 0xBB && in[2] == 0xBF) { *size = 3; return DS_ENC_UTF8; }
    if (n >= 2 && in[0] == 0xFF && in[1] == 0xFE) { *size = 2; return DS_ENC_UTF16LE; }
    if (n >= 2 && in[0] == 0xFE && in[1] == 0xFF) { *size = 2; return DS_ENC_UTF16BE; }
    return -1;
}

static inline size_t ds_enc_put_utf8(unsigned char* out, uint32_t c) {
    if (c < 0x80) { out[0] = (unsigned char)c; return 1; }
    if (c < 0x800) { out[0] = (unsigned char)(0xC0 | (c >> 6)); out[1] = (unsigned char)(0x80 | (c & 0x3F)); return 2; }
    if (c < 0x10000) {
        out[0] = (unsigned char)(0xE0 | (c >> 12));
        out[1] = (unsigned char)(0x80 | ((c >> 6) & 0x3F));
        out[2] = (unsigned char)(0x80 | (c & 0x3F));
        return 3;
    }
    out[0] = (unsigned char)(0xF0 | (c >> 18));
    out[1] = (unsigned char)(0x80 | ((c >> 12) & 0x3F));
    out[2] = (unsigned char)(0x80 | ((c >> 6) & 0x3F));
    out[3] = (unsigned char)(0x80 | (c & 0x3F));
    return 4;
}

/* One character of UTF-8; 0 when it isn't valid */
static inline size_t ds_enc_get_utf8(const unsigned char* in, size_t n, uint32_t* c) {
    if (n == 0) return 0;
    const unsigned char a = in[0];
    if (a < 0x80) { *c = a; return 1; }
    const size_t len = (a >= 0xC2 && a <= 0xDF) ? 2 : (a >= 0xE0 && a <= 0xEF) ? 3 : (a >= 0xF0 && a <= 0xF4) ? 4 : 0;
    if (len == 0 || n < len) return 0;
    uint32_t value = len == 2 ? (a & 0x1Fu) : len == 3 ? (a & 0x0Fu) : (a & 0x07u);
    for (size_t k = 1; k < len; ++k) {
        if ((in[k] & 0xC0) != 0x80) return 0;
        value = (value << 6) | (in[k] & 0x3Fu);
    }
    if ((len == 3 && value < 0x800) || (len == 4 && value < 0x10000) || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return 0;
    *c = value;
    return len;
}

/* Bytes in `encoding` to UTF-8. Returns malloc'ed text (size in *out_size, with
 * a 0 after it), or NULL with *error saying what's wrong with the data. */
static inline unsigned char* ds_enc_to_utf8(const unsigned char* in, size_t n, int encoding, size_t* out_size, const char** error) {
    size_t skip = 0;
    int bom = ds_enc_bom(in, n, &skip);
    int enc = encoding;
    if (enc == DS_ENC_AUTO) {
        enc = bom >= 0 ? bom : DS_ENC_UTF8;
    } else if (enc == DS_ENC_UTF16) {
        enc = bom == DS_ENC_UTF16BE ? DS_ENC_UTF16BE : DS_ENC_UTF16LE;
        if (bom != DS_ENC_UTF16BE && bom != DS_ENC_UTF16LE) skip = 0;
    } else if (enc == DS_ENC_UTF32) {
        enc = bom == DS_ENC_UTF32BE ? DS_ENC_UTF32BE : DS_ENC_UTF32LE;
        if (bom != DS_ENC_UTF32BE && bom != DS_ENC_UTF32LE) skip = 0;
    } else if (enc == DS_ENC_UTF8 || enc == DS_ENC_UTF8_BOM) {
        enc = DS_ENC_UTF8;
        if (bom != DS_ENC_UTF8) skip = 0;
    } else if (bom != enc) {
        skip = 0;   /* utf-16le etc.: only its own BOM is dropped */
    }
    in += skip;
    n -= skip;
    /* A 2-byte unit makes at most 3 bytes of UTF-8 (a surrogate pair: 4 from 4); 4 bytes at most 4 */
    const size_t room = (enc == DS_ENC_UTF16LE || enc == DS_ENC_UTF16BE) ? n / 2 * 3 + 1 : n + 1;
    unsigned char* out = (unsigned char*)malloc(room + 4);
    size_t at = 0;
    if (!out) { *error = "out of memory"; return NULL; }
    if (enc == DS_ENC_UTF8) {
        size_t i = 0;
        while (i < n) {
            uint32_t c = 0;
            size_t used = ds_enc_get_utf8(in + i, n - i, &c);
            if (!used) { free(out); *error = "this isn't valid UTF-8"; return NULL; }
            memcpy(out + at, in + i, used);
            at += used;
            i += used;
        }
    } else if (enc == DS_ENC_UTF16LE || enc == DS_ENC_UTF16BE) {
        if (n % 2) { free(out); *error = "UTF-16 comes in pairs of bytes, and this has one left over"; return NULL; }
        const int hi = enc == DS_ENC_UTF16BE ? 0 : 1, lo = 1 - hi;
        for (size_t i = 0; i + 1 < n; i += 2) {
            uint32_t c = ((uint32_t)in[i + (size_t)hi] << 8) | in[i + (size_t)lo];
            if (c >= 0xD800 && c <= 0xDBFF) {
                if (i + 3 >= n) { free(out); *error = "this UTF-16 ends in half a character"; return NULL; }
                const uint32_t d = ((uint32_t)in[i + 2 + (size_t)hi] << 8) | in[i + 2 + (size_t)lo];
                if (d < 0xDC00 || d > 0xDFFF) { free(out); *error = "this isn't valid UTF-16"; return NULL; }
                c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
                i += 2;
            } else if (c >= 0xDC00 && c <= 0xDFFF) {
                free(out);
                *error = "this isn't valid UTF-16";
                return NULL;
            }
            at += ds_enc_put_utf8(out + at, c);
        }
    } else {
        if (n % 4) { free(out); *error = "UTF-32 comes in groups of four bytes, and this has some left over"; return NULL; }
        for (size_t i = 0; i + 3 < n; i += 4) {
            const unsigned char* b = in + i;
            const uint32_t c = enc == DS_ENC_UTF32BE ? ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3]
                                                     : ((uint32_t)b[3] << 24) | ((uint32_t)b[2] << 16) | ((uint32_t)b[1] << 8) | b[0];
            if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) { free(out); *error = "this isn't valid UTF-32"; return NULL; }
            at += ds_enc_put_utf8(out + at, c);
        }
    }
    out[at] = 0;
    *out_size = at;
    return out;
}

/* UTF-8 to bytes in `encoding`: malloc'ed. NULL with *error if the text is invalid, or for "auto". */
static inline unsigned char* ds_enc_from_utf8(const unsigned char* in, size_t n, int encoding, size_t* out_size, const char** error) {
    if (encoding == DS_ENC_AUTO) { *error = "\"auto\" is only for reading: say which encoding to write"; return NULL; }
    unsigned char* out = (unsigned char*)malloc(n * 4 + 8);
    size_t at = 0, i = 0;
    if (!out) { *error = "out of memory"; return NULL; }
    if (encoding == DS_ENC_UTF8_BOM) { out[at++] = 0xEF; out[at++] = 0xBB; out[at++] = 0xBF; }
    if (encoding == DS_ENC_UTF16) { out[at++] = 0xFF; out[at++] = 0xFE; }
    if (encoding == DS_ENC_UTF32) { out[at++] = 0xFF; out[at++] = 0xFE; out[at++] = 0; out[at++] = 0; }
    const int little = encoding == DS_ENC_UTF16 || encoding == DS_ENC_UTF16LE || encoding == DS_ENC_UTF32 || encoding == DS_ENC_UTF32LE;
    while (i < n) {
        uint32_t c = 0;
        const size_t used = ds_enc_get_utf8(in + i, n - i, &c);
        if (!used) { free(out); *error = "this text isn't valid UTF-8"; return NULL; }
        i += used;
        if (encoding == DS_ENC_UTF8 || encoding == DS_ENC_UTF8_BOM) {
            at += ds_enc_put_utf8(out + at, c);
        } else if (encoding == DS_ENC_UTF32 || encoding == DS_ENC_UTF32LE || encoding == DS_ENC_UTF32BE) {
            for (int k = 0; k < 4; ++k) out[at++] = (unsigned char)(c >> (little ? 8 * k : 24 - 8 * k));
        } else {
            uint32_t units[2];
            int count = 1;
            units[0] = c;
            if (c >= 0x10000) {
                units[0] = 0xD800 + ((c - 0x10000) >> 10);
                units[1] = 0xDC00 + ((c - 0x10000) & 0x3FF);
                count = 2;
            }
            for (int u = 0; u < count; ++u) {
                out[at++] = (unsigned char)(little ? units[u] : units[u] >> 8);
                out[at++] = (unsigned char)(little ? units[u] >> 8 : units[u]);
            }
        }
    }
    *out_size = at;
    return out;
}

#endif
