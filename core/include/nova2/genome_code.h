#pragma once

// The kept genomes' bytes as the genebank's second file holds them
// (design.md 13.2; build-plan-gate5.md 11; Damian, 2026-11-20): a genome is
// stored as its difference from the genome it came from, with a whole copy
// at least every `key_every` steps along a line (16), and a whole copy
// whenever its parent's bytes aren't to hand or the difference is no
// shorter.
//
// The file, seed-N.genome_bytes, begins with the 8 bytes "NOV2GEN1". Then
// one entry for each row of seed-N.genomes, in the rows' order. A row's
// bytes_at is where its entry begins, so any entry is found from its row
// alone, and it ends where the next row's begins (the last, at the file's
// end). Numbers are varints: 7 bits a byte, low bits first, the top bit set
// on every byte but a number's last.
//
//   steps    0: a whole copy. The genome's bytes follow (the row's len).
//            1 to 255: a difference, this many differences on from a whole
//            copy along its line.
//   back     (a difference only) how many rows back its base is: the row
//            whose genome this one differs from. 1 or more.
//   pieces   (a difference only) until the genome has the row's len bytes:
//              copy    bytes taken from the base, from where the last piece
//                      left off in it (its start, for the first piece);
//            and then, if the genome isn't whole yet,
//              skip    bytes of the base passed over;
//              fresh   how many bytes follow here, and those bytes.
//
// To read a genome, read its entry; if it is a difference, read its base
// first, the same way. `steps` says how long the way is, and it is always
// under the key_every the file was written with, so a reader reads 16
// entries at most. Damage to an entry spoils that genome and the ones built
// on it up to the next whole copy on each line, and no others: every other
// entry is found from its own row. To find the whole copy a line starts
// from, follow `back` row by row until an entry whose steps is 0. Every row
// has the hash of its genome's bytes (hash_bytes with the genome's domain),
// so a reader can tell a genome it has built wrongly.
//
// A record made before this (gate 3's) has no tag: its file is the genomes'
// bytes one after another, each whole, at its row's bytes_at.
#include "nova2/array.h"
#include "nova2/types.h"

namespace nova2 {

inline constexpr u32 kGenomeTagBytes = 8;
inline constexpr u8 kGenomeFileTag[kGenomeTagBytes] = {'N', 'O', 'V', '2', 'G', 'E', 'N', '1'};
inline constexpr u32 kGenomeMaxSteps = 255;

void put_varint(Array<u8>* out, u64 v);
// The number at p[*at], moving *at past it. False if it runs past n bytes.
bool get_varint(const u8* p, usize n, usize* at, u64* v);

// Appends genome g's entry to `out`: a difference from `base` (its nb bytes;
// itself `base_steps` differences from a whole copy; `back` rows back), if
// there is a base, the line stays under key_every steps and the difference
// is shorter than the genome; else a whole copy. Returns the steps written.
u32 genome_entry(const u8* g, u32 n, const u8* base, u32 nb, u32 base_steps, u64 back, u32 key_every,
                 Array<u8>* out);
// An entry's head: its steps and, for a difference, how far back its base
// is (0 for a whole copy), and where its body begins.
bool genome_entry_head(const u8* e, usize ne, u32* steps, u64* back, usize* body);
// A difference's pieces applied to its base: the genome's `len` bytes, into
// `out` (cleared first). False if the pieces aren't well formed for it.
bool genome_patch(const u8* pieces, usize np, const u8* base, u32 nb, u32 len, Array<u8>* out);

// The genebank's two files, however they are held (the observer's streams,
// or both read into memory), for genome_read.
struct GenomeFile {
    void* ctx;
    u64 rows;  // rows of the genebank
    u64 bytes; // bytes of the second file
    // Row i: where its entry begins and its genome's length.
    bool (*row)(void* ctx, u64 i, u64* bytes_at, u32* len);
    // n bytes of the second file from `at`.
    bool (*read)(void* ctx, u64 at, u64 n, u8* out);
};
// Row `row`'s genome, built from its entry and the entries it rests on.
// False if a row or an entry can't be read or isn't well formed. `steps`,
// if given: how many differences it was from a whole copy.
bool genome_read(const GenomeFile& f, u64 row, Array<u8>* out, u32* steps = nullptr);

} // namespace nova2
