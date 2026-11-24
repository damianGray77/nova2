// nova2 genomes <records folder> <seed> [--each] [--pack <folder>] [--key-every N]
//
// Reads a seed's kept genomes whole from its records (seed-N.genomes, a row
// a genome, and seed-N.genome_bytes, their bytes: genome_code.h), builds
// every genome and checks it against the hash in its row, and says how many
// there are, what they take and how long the read took. It builds them in
// the rows' order, each on the ones before it, which it keeps; --each
// builds every genome on its own instead, from its entry and the entries it
// rests on back to a whole copy, as a reader after one genome would.
//
// The bytes are differences since 2026-11-20 (build-plan-gate5.md 11); an
// earlier record's are whole genomes one after another, with no tag, and
// both are read. --pack writes an earlier record again as differences into
// another folder, as a run would have written it with everything kept in
// memory (no forgetting): each genome against the latest row before it with
// its parent's hash. That is how the saving was measured on gate 3's record
// without making the run again.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "commands.h"
#include "nova2/genome_code.h"
#include "nova2/hash.h"
#include "nova2/hashmap.h"
#include "nova2/io.h"
#include "nova2/observe.h"
#include "nova2/os.h"
#include "nova2/pool.h"

namespace nova2 {

namespace {

struct Bank {
    const GenebankRow* rows;
    u64 n;
    const u8* bytes;
    u64 size;
};

bool bank_row(void* ctx, u64 i, u64* bytes_at, u32* len) {
    const Bank& b = *static_cast<const Bank*>(ctx);
    if (i >= b.n)
        return false;
    *bytes_at = b.rows[i].bytes_at;
    *len = b.rows[i].len;
    return true;
}

bool bank_read(void* ctx, u64 at, u64 n, u8* out) {
    const Bank& b = *static_cast<const Bank*>(ctx);
    if (at > b.size || n > b.size - at)
        return false;
    memcpy(out, b.bytes + at, static_cast<usize>(n));
    return true;
}

bool write_whole(const char* path, const void* data, usize n) {
    FILE* f = fopen(path, "wb");
    if (!f)
        return false;
    const bool ok = n == 0 || fwrite(data, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

} // namespace

int cmd_genomes(int argc, char** argv) {
    const char* pack = nullptr;
    bool each = false;
    u32 key_every = 16;
    const char* plain[2] = {nullptr, nullptr};
    u32 plains = 0;
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--pack") == 0 && i + 1 < argc)
            pack = argv[++i];
        else if (strcmp(argv[i], "--each") == 0)
            each = true;
        else if (strcmp(argv[i], "--key-every") == 0 && i + 1 < argc)
            key_every = static_cast<u32>(strtoul(argv[++i], nullptr, 10));
        else if (plains < 2)
            plain[plains++] = argv[i];
    }
    if (plains < 2 || key_every < 1 || key_every > kGenomeMaxSteps) {
        fprintf(stderr, "nova2: genomes needs a records folder and a seed ([--each] [--pack <folder>] "
                        "[--key-every N], N from 1 to 255)\n");
        return 2;
    }
    const unsigned long long seed = strtoull(plain[1], nullptr, 10);
    char rows_path[512], bytes_path[512];
    snprintf(rows_path, sizeof rows_path, "%s/seed-%llu.genomes", plain[0], seed);
    snprintf(bytes_path, sizeof bytes_path, "%s/seed-%llu.genome_bytes", plain[0], seed);
    const double t0 = clock_seconds();
    Array<u8> row_bytes, bytes;
    if (!read_file(rows_path, &row_bytes) || !read_file(bytes_path, &bytes)) {
        fprintf(stderr, "nova2: can't read %s and %s\n", rows_path, bytes_path);
        return 2;
    }
    Bank b;
    b.rows = reinterpret_cast<const GenebankRow*>(row_bytes.data());
    b.n = row_bytes.size() / sizeof(GenebankRow);
    b.bytes = bytes.data();
    b.size = bytes.size();
    const bool tagged = bytes.size() >= kGenomeTagBytes && memcmp(bytes.data(), kGenomeFileTag, kGenomeTagBytes) == 0 &&
                        (b.n == 0 || b.rows[0].bytes_at == kGenomeTagBytes);
    const double t1 = clock_seconds();
    GenomeFile file;
    file.ctx = &b;
    file.rows = b.n;
    file.bytes = b.size;
    file.row = bank_row;
    file.read = bank_read;
    u64 whole = 0, bad = 0, total = 0, by_steps[kGenomeMaxSteps + 1] = {};
    u32 longest = 0;
    Array<u8> g;
    Array<u8> built;    // in order: every genome built so far
    Array<u64> built_at; // and where each begins
    const u8 nothing = 0;
    for (u64 i = 0; i < b.n; ++i) {
        const GenebankRow& r = b.rows[i];
        u32 steps = 0;
        bool ok;
        const u8* at = nullptr;
        if (tagged && each) {
            ok = genome_read(file, i, &g, &steps) && g.size() == r.len;
            at = g.data();
        } else if (tagged) {
            // Its entry, and the genome it is a difference from, built already.
            const u64 end = i + 1 < b.n ? b.rows[i + 1].bytes_at : b.size;
            u64 back = 0;
            usize body = 0;
            ok = r.bytes_at <= end && end <= b.size &&
                 genome_entry_head(b.bytes + r.bytes_at, static_cast<usize>(end - r.bytes_at), &steps, &back, &body);
            const usize n = ok ? static_cast<usize>(end - r.bytes_at) - body : 0;
            const u8* e = b.bytes + r.bytes_at + body;
            if (ok && steps == 0) {
                ok = n == r.len;
                g.clear();
                if (ok)
                    g.append(e, n);
            } else if (ok) {
                ok = back <= i && genome_patch(e, n, built.data() + built_at[static_cast<usize>(i - back)],
                                               b.rows[i - back].len, r.len, &g);
            }
            if (!ok) { // its place kept, so the rows after it still find theirs
                g.clear();
                g.resize(r.len);
            }
            built_at.push(built.size());
            built.append(g.data(), g.size());
            at = g.size() ? g.data() : &nothing;
        } else {
            ok = r.bytes_at <= b.size && r.len <= b.size - r.bytes_at;
            at = b.bytes + r.bytes_at;
        }
        ok = ok && hash_bytes(at, r.len, kGenomeDomain) == r.hash;
        bad += ok ? 0 : 1;
        total += r.len;
        whole += steps == 0 ? 1 : 0;
        ++by_steps[steps];
        longest = steps > longest ? steps : longest;
    }
    const double t2 = clock_seconds();
    printf("%s: %llu kept genomes, %llu bytes of them whole; the file is %llu bytes (%.1f%%), %s\n", bytes_path,
           static_cast<unsigned long long>(b.n), static_cast<unsigned long long>(total),
           static_cast<unsigned long long>(b.size), total ? 100.0 * static_cast<double>(b.size) / total : 0.0,
           tagged ? "as differences" : "every genome whole (a record from before 2026-11-20)");
    if (tagged)
        printf("  %llu whole copies, %llu differences; the longest line from a whole copy %u steps\n",
               static_cast<unsigned long long>(whole), static_cast<unsigned long long>(b.n - whole), longest);
    printf("  read whole and every genome built and checked against its row's hash in %.2f s (%.2f s reading the two "
           "files, %.2f s building): %llu wrong\n",
           t2 - t0, t1 - t0, t2 - t1, static_cast<unsigned long long>(bad));
    if (tagged)
        printf("  built %s\n", each ? "each on its own, from its entry and the entries it rests on"
                                 : "in the rows' order, each on the ones before it (--each: each on its own)");
    if (!pack)
        return bad ? 1 : 0;
    if (tagged) {
        fprintf(stderr, "nova2: --pack is for a record whose genomes are whole: this one is differences already\n");
        return 2;
    }
    // The record again as differences. Every genome kept is in memory in a
    // run that forgets nothing, so a genome's base is the latest row before
    // it with its parent's hash.
    const double t3 = clock_seconds();
    HashMap<u64> latest; // a genome's hash -> its latest row
    Array<u8> steps_of;
    Array<GenebankRow> rows;
    Array<u8> out;
    out.append(kGenomeFileTag, kGenomeTagBytes);
    u64 packed_whole = 0;
    for (u64 i = 0; i < b.n; ++i) {
        GenebankRow r = b.rows[i];
        const u8* gbytes = b.bytes + r.bytes_at;
        const u64* base = r.parent_genome ? latest.find(r.parent_genome) : nullptr;
        r.bytes_at = out.size();
        const u32 steps =
            genome_entry(gbytes, r.len, base ? b.bytes + b.rows[*base].bytes_at : nullptr,
                         base ? b.rows[*base].len : 0, base ? steps_of[static_cast<usize>(*base)] : 0,
                         base ? i - *base : 0, key_every, &out);
        steps_of.push(static_cast<u8>(steps));
        packed_whole += steps == 0 ? 1 : 0;
        rows.push(r);
        latest.get_or_insert(r.hash) = i;
    }
    char out_rows[512], out_bytes[512];
    snprintf(out_rows, sizeof out_rows, "%s/seed-%llu.genomes", pack, seed);
    snprintf(out_bytes, sizeof out_bytes, "%s/seed-%llu.genome_bytes", pack, seed);
    if (!write_whole(out_rows, rows.data(), rows.size() * sizeof(GenebankRow)) ||
        !write_whole(out_bytes, out.data(), out.size())) {
        fprintf(stderr, "nova2: can't write %s and %s\n", out_rows, out_bytes);
        return 2;
    }
    printf("  packed into %s: %llu bytes where it was %llu (%.1f%%), a whole copy at least every %u steps: %llu whole "
           "copies, %llu differences; %.2f s\n",
           out_bytes, static_cast<unsigned long long>(out.size()), static_cast<unsigned long long>(b.size),
           b.size ? 100.0 * static_cast<double>(out.size()) / static_cast<double>(b.size) : 0.0, key_every,
           static_cast<unsigned long long>(packed_whole), static_cast<unsigned long long>(b.n - packed_whole),
           clock_seconds() - t3);
    return bad ? 1 : 0;
}

} // namespace nova2
