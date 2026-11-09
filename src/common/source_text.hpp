#pragma once

#include <string>

#include "../../runtime/dessa_encoding.h"

namespace dessa {

// A source file as UTF-8, whatever it was saved in: UTF-8 (with or without a
// BOM), or UTF-16 / UTF-32 with a BOM. The BOM is what says so: without one, the
// file is taken as UTF-8 and the loader says if it isn't. False: `message` says why.
inline bool sourceToUtf8(std::string& text, std::string& message) {
    size_t bomSize = 0;
    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    const int encoding = ds_enc_bom(bytes, text.size(), &bomSize);
    if (encoding < 0) return true;
    if (encoding == DS_ENC_UTF8) {
        text.erase(0, bomSize);
        return true;
    }
    size_t size = 0;
    const char* error = nullptr;
    unsigned char* converted = ds_enc_to_utf8(bytes, text.size(), DS_ENC_AUTO, &size, &error);
    if (!converted) {
        message = error;
        return false;
    }
    text.assign(reinterpret_cast<const char*>(converted), size);
    free(converted);
    return true;
}

// Where a file stops being valid UTF-8, as its line number; 0 when it's all valid
inline int invalidUtf8Line(const std::string& text) {
    int line = 1;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '\n') ++line;
        const int extra = c < 0x80 ? 0 : (c >= 0xC2 && c <= 0xDF) ? 1 : (c >= 0xE0 && c <= 0xEF) ? 2 : (c >= 0xF0 && c <= 0xF4) ? 3 : -1;
        if (extra < 0 || i + static_cast<size_t>(extra) >= text.size() + (extra == 0 ? 1 : 0)) return line;
        for (int k = 1; k <= extra; ++k) {
            const unsigned char more = static_cast<unsigned char>(text[i + static_cast<size_t>(k)]);
            unsigned char low = 0x80, high = 0xBF;
            if (k == 1 && c == 0xE0) low = 0xA0;      // too long a form
            if (k == 1 && c == 0xED) high = 0x9F;     // surrogates aren't characters
            if (k == 1 && c == 0xF0) low = 0x90;      // too long a form
            if (k == 1 && c == 0xF4) high = 0x8F;     // past the last character
            if (more < low || more > high) return line;
        }
        i += static_cast<size_t>(extra) + 1;
    }
    return 0;
}

} // namespace dessa
