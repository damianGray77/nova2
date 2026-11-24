#pragma once

// A compressibility estimate, for the region measures (R4, 13.1). From Nova
// 1's, made for bytes rather than its six-bit codons, and with no lambda.
//
// A hand-rolled greedy LZ77 cost estimate (no zlib). A greedy parse takes, at
// each position, the longest earlier match of at least 4 bytes (hash chains,
// bounded search) or else a literal. The estimated size in bits is:
//   - literals at their zero-order entropy (at most 8 bits each);
//   - one match/literal flag per token at the binary entropy of the match rate;
//   - per match, a 16-bit offset plus an Elias-gamma length.
// ratio = estimated bits / (8 x bytes): about 1 for random bytes, well below
// 1 for a region full of copies.
#include "nova2/array.h"
#include "nova2/types.h"

namespace nova2 {

struct Compressibility {
    bool measured = false;
    double ratio = 0.0; // estimated compressed size / raw size; lower = more repetitive
    u64 matches = 0;    // tokens that were matches
    u64 literals = 0;   // tokens that were literals
};

// The estimate's working tables, for a caller that estimates again and again
// (the observer, at every sample): kept from call to call, so nothing is
// allocated or cleared whole each time. Each call leaves `head` as it found
// it, every entry empty.
struct CompressScratch {
    Array<u32> head; // per hash: the last position with it, or none
    Array<u32> prev; // per position: the one before it with the same hash
};

// With no scratch it uses its own, for the one call. The result is the same
// either way.
Compressibility estimate_compressibility(const u8* data, usize n, CompressScratch* scratch = nullptr);

} // namespace nova2
