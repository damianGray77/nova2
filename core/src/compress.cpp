#include "nova2/compress.h"

#include <cmath>    // log2: observers only, never in a tick
#include <cstring>  // memcpy
#include <intrin.h> // _BitScanForward64

#include "nova2/array.h"

namespace nova2 {

namespace {

constexpr u32 kMinMatch = 4; // shorter matches cost more than their literals
constexpr u32 kMaxMatch = 4096;
constexpr u32 kOffsetBits = 16; // matches reach back up to 64 KB
constexpr u32 kWindow = 1u << kOffsetBits;
constexpr u32 kChain = 32; // candidates tried per position (bounded search)
constexpr u32 kHashBits = 16;
constexpr u32 kNoPos = ~0u; // no position: an empty chain, or its end

u32 hash4(const u8* p) {
    const u32 v = static_cast<u32>(p[0]) | static_cast<u32>(p[1]) << 8 | static_cast<u32>(p[2]) << 16 |
                  static_cast<u32>(p[3]) << 24;
    return (v * 2654435761u) >> (32 - kHashBits);
}

// Elias-gamma code length for x >= 1.
u32 gamma_bits(u32 x) {
    u32 b = 0;
    while ((x >> b) > 1)
        ++b;
    return 2 * b + 1;
}

struct Chains {
    const u8* data;
    usize n;
    u32* head;
    u32* prev;

    void insert(usize pos) {
        if (pos + kMinMatch <= n) {
            const u32 h = hash4(data + pos);
            prev[pos] = head[h];
            head[h] = static_cast<u32>(pos);
        }
    }
};

} // namespace

Compressibility estimate_compressibility(const u8* data, usize n, CompressScratch* scratch) {
    Compressibility c;
    c.measured = true;
    if (n == 0) {
        c.ratio = 1.0;
        return c;
    }
    if (n >= kNoPos)
        NOVA2_FATAL("estimate_compressibility: more bytes than a position can number");
    CompressScratch own;
    CompressScratch& sc = scratch ? *scratch : own;
    if (sc.head.size() != static_cast<usize>(1) << kHashBits) { // its first use: every chain empty
        sc.head.resize(static_cast<usize>(1) << kHashBits);
        sc.head.fill(kNoPos);
    }
    if (sc.prev.size() < n)
        sc.prev.resize_unfilled(n);
    Chains ch;
    ch.data = data;
    ch.n = n;
    ch.head = sc.head.data();
    ch.prev = sc.prev.data();

    u64 freq[256] = {};
    double match_bits = 0;
    usize i = 0;
    while (i < n) {
        u32 best = 0;
        if (i + kMinMatch <= n) {
            const u32 maxlen = static_cast<u32>(min_of<usize>(kMaxMatch, n - i));
            u32 cand = ch.head[hash4(data + i)];
            for (u32 tries = 0; cand != kNoPos && tries < kChain; ++tries) {
                const usize at = cand;
                if (i - at > kWindow)
                    break;
                // Only a longer match than the best counts, and one must
                // agree where the best ended (best < maxlen here, so both
                // bytes are the data's): most candidates stop at this.
                if (best && data[at + best] != data[i + best]) {
                    cand = ch.prev[at];
                    continue;
                }
                u32 l = 0; // the match may overlap position i
                bool differs = false;
                while (!differs && l + 8 <= maxlen) { // eight bytes at a time
                    u64 a, b;
                    memcpy(&a, data + at + l, 8);
                    memcpy(&b, data + i + l, 8);
                    unsigned long first;
                    if (_BitScanForward64(&first, a ^ b)) {
                        l += first >> 3; // the lowest byte that differs
                        differs = true;
                    } else {
                        l += 8;
                    }
                }
                while (!differs && l < maxlen && data[at + l] == data[i + l])
                    ++l;
                if (l > best) {
                    best = l;
                    if (l == maxlen)
                        break;
                }
                cand = ch.prev[at];
            }
        }
        if (best >= kMinMatch) {
            ++c.matches;
            match_bits += kOffsetBits + gamma_bits(best - kMinMatch + 1);
            for (u32 j = 0; j < best; ++j)
                ch.insert(i + j);
            i += best;
        } else {
            ++c.literals;
            ++freq[data[i]];
            ch.insert(i);
            ++i;
        }
    }

    // Every chain it started is emptied again, for the next call: far fewer
    // than the table holds.
    for (usize pos = 0; pos + kMinMatch <= n; ++pos)
        ch.head[hash4(data + pos)] = kNoPos;

    double bits = match_bits;
    const double lit = static_cast<double>(c.literals);
    for (u32 s = 0; s < 256; ++s)
        if (freq[s])
            bits += static_cast<double>(freq[s]) * log2(lit / static_cast<double>(freq[s]));
    const double tokens = static_cast<double>(c.matches + c.literals);
    if (c.matches && c.literals) {
        bits += static_cast<double>(c.matches) * log2(tokens / static_cast<double>(c.matches));
        bits += lit * log2(tokens / lit);
    }
    c.ratio = bits / (8.0 * static_cast<double>(n));
    return c;
}

} // namespace nova2
