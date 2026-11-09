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

} // namespace dessa
