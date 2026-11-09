// Checking a downloaded file against a release's SHA256SUMS, before anything
// in it is unpacked or run.
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace dessa::verify {

class Sha256 {
public:
    void add(const unsigned char* data, size_t length) {
        total += length;
        while (length > 0) {
            const size_t room = 64 - used;
            const size_t take = length < room ? length : room;
            for (size_t i = 0; i < take; ++i) block[used + i] = data[i];
            used += take;
            data += take;
            length -= take;
            if (used == 64) {
                compress();
                used = 0;
            }
        }
    }

    std::string hex() {
        const std::uint64_t bits = total * 8;
        const unsigned char one = 0x80, zero = 0;
        add(&one, 1);
        while (used != 56) add(&zero, 1);
        unsigned char length[8];
        for (int i = 0; i < 8; ++i) length[i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
        add(length, 8);
        static const char* digits = "0123456789abcdef";
        std::string out;
        for (std::uint32_t word : state) {
            for (int shift = 28; shift >= 0; shift -= 4) out += digits[(word >> shift) & 15];
        }
        return out;
    }

private:
    std::uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    unsigned char block[64] = {};
    size_t used = 0;
    std::uint64_t total = 0;

    static std::uint32_t rotate(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void compress() {
        static const std::uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (std::uint32_t(block[i * 4]) << 24) | (std::uint32_t(block[i * 4 + 1]) << 16) |
                   (std::uint32_t(block[i * 4 + 2]) << 8) | std::uint32_t(block[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotate(w[i - 15], 7) ^ rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotate(w[i - 2], 17) ^ rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t v[8];
        for (int i = 0; i < 8; ++i) v[i] = state[i];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotate(v[4], 6) ^ rotate(v[4], 11) ^ rotate(v[4], 25);
            const std::uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
            const std::uint32_t t1 = v[7] + s1 + ch + K[i] + w[i];
            const std::uint32_t s0 = rotate(v[0], 2) ^ rotate(v[0], 13) ^ rotate(v[0], 22);
            const std::uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
            const std::uint32_t t2 = s0 + maj;
            for (int j = 7; j > 0; --j) v[j] = v[j - 1];
            v[4] += t1;
            v[0] = t1 + t2;
        }
        for (int i = 0; i < 8; ++i) state[i] += v[i];
    }
};

inline std::string sha256File(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    Sha256 hash;
    char buffer[65536];
    while (in.read(buffer, sizeof buffer), in.gcount() > 0) {
        hash.add(reinterpret_cast<const unsigned char*>(buffer), static_cast<size_t>(in.gcount()));
    }
    return hash.hex();
}

// Empty when `file` is listed in `sums` (lines of "<sha256>  <name>", as
// sha256sum writes them) with its real hash; otherwise what is wrong. A file
// that isn't listed is refused too.
inline std::string check(const std::filesystem::path& file, const std::filesystem::path& sums) {
    const std::string name = file.filename().string();
    std::ifstream in(sums, std::ios::binary);
    if (!in) return "could not read the list of checksums";
    const std::string actual = sha256File(file);
    if (actual.empty()) return "could not read " + name;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        std::string hash, listed;
        if (!(words >> hash >> listed)) continue;
        if (!listed.empty() && listed[0] == '*') listed.erase(0, 1);   // sha256sum's mark for binary mode
        if (listed != name) continue;
        if (hash == actual) return "";
        return name + " is not the file that was released (its checksum is " + actual + ", expected " + hash + ")";
    }
    return name + " is not in the list of checksums";
}

} // namespace dessa::verify
