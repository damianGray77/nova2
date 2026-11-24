#include "nova2/genome_code.h"

namespace nova2 {

void put_varint(Array<u8>* out, u64 v) {
    while (v >= 0x80) {
        out->push(static_cast<u8>(v | 0x80));
        v >>= 7;
    }
    out->push(static_cast<u8>(v));
}

bool get_varint(const u8* p, usize n, usize* at, u64* v) {
    u64 r = 0;
    for (u32 shift = 0; shift < 64; shift += 7) {
        if (*at >= n)
            return false;
        const u8 b = p[(*at)++];
        r |= static_cast<u64>(b & 0x7F) << shift;
        if (!(b & 0x80)) {
            *v = r;
            return true;
        }
    }
    return false;
}

namespace {

constexpr u32 kSync = 4;   // bytes that must agree for the two to be in step again
constexpr u32 kReach = 32; // the furthest a way back into step is looked for: bytes skipped and fresh together

// How many bytes a and b begin alike.
u32 alike(const u8* a, u32 na, const u8* b, u32 nb) {
    const u32 most = na < nb ? na : nb;
    u32 k = 0;
    while (k < most && a[k] == b[k])
        ++k;
    return k;
}

// The pieces that make g from base. Greedy: copy while the two agree; where
// they part, the cheapest way back into step within kReach (bytes of the
// base skipped and fresh bytes together, the fewest first; in step means the
// next kSync bytes agree, or all that is left of g); and if there is none,
// everything fresh up to the tail the two share, or to the end. Whatever it
// finds, the pieces make g exactly: only their length depends on it.
void pieces_of(const u8* g, u32 n, const u8* base, u32 nb, Array<u8>* out) {
    u32 i = 0, j = 0;
    for (;;) {
        const u32 c = alike(g + i, n - i, base + j, nb - j);
        put_varint(out, c);
        i += c;
        j += c;
        if (i == n)
            return;
        u32 fresh = n - i, skip = 0;
        bool found = false;
        for (u32 total = 1; total <= kReach && !found; ++total)
            for (u32 f = 0; f <= total; ++f) {
                const u32 s = total - f;
                if (f >= n - i || s > nb - j)
                    continue;
                const u32 left = n - i - f;
                const u32 need = left < kSync ? left : kSync;
                if (alike(g + i + f, left, base + j + s, nb - j - s) >= need) {
                    fresh = f;
                    skip = s;
                    found = true;
                    break;
                }
            }
        if (!found) {
            u32 t = 0; // the tail the two share
            while (t < n - i && t < nb - j && g[n - 1 - t] == base[nb - 1 - t])
                ++t;
            if (t >= kSync) {
                fresh = n - i - t;
                skip = nb - j - t;
            }
        }
        put_varint(out, skip);
        put_varint(out, fresh);
        out->append(g + i, fresh);
        i += fresh;
        j += skip;
        if (i == n)
            return;
    }
}

} // namespace

u32 genome_entry(const u8* g, u32 n, const u8* base, u32 nb, u32 base_steps, u64 back, u32 key_every,
                 Array<u8>* out) {
    if (base && back > 0 && base_steps + 1 < key_every && base_steps + 1 <= kGenomeMaxSteps) {
        const usize at = out->size();
        put_varint(out, base_steps + 1);
        put_varint(out, back);
        pieces_of(g, n, base, nb, out);
        if (out->size() - at < 1 + static_cast<usize>(n)) // shorter than the whole copy would be
            return base_steps + 1;
        out->resize(at);
    }
    put_varint(out, 0);
    out->append(g, n);
    return 0;
}

bool genome_entry_head(const u8* e, usize ne, u32* steps, u64* back, usize* body) {
    usize at = 0;
    u64 s = 0, b = 0;
    if (!get_varint(e, ne, &at, &s) || s > kGenomeMaxSteps)
        return false;
    if (s && (!get_varint(e, ne, &at, &b) || b == 0))
        return false;
    *steps = static_cast<u32>(s);
    *back = b;
    *body = at;
    return true;
}

bool genome_patch(const u8* pieces, usize np, const u8* base, u32 nb, u32 len, Array<u8>* out) {
    out->clear();
    out->reserve(len);
    usize at = 0;
    u32 j = 0;
    while (out->size() < len) {
        u64 copy = 0, skip = 0, fresh = 0;
        if (!get_varint(pieces, np, &at, &copy) || copy > nb - j || copy > len - out->size())
            return false;
        out->append(base + j, static_cast<usize>(copy));
        j += static_cast<u32>(copy);
        if (out->size() == len)
            break;
        if (!get_varint(pieces, np, &at, &skip) || !get_varint(pieces, np, &at, &fresh) || skip > nb - j ||
            fresh > len - out->size() || fresh > np - at || copy + skip + fresh == 0)
            return false;
        out->append(pieces + at, static_cast<usize>(fresh));
        at += static_cast<usize>(fresh);
        j += static_cast<u32>(skip);
    }
    return at == np; // nothing left over
}

bool genome_read(const GenomeFile& f, u64 row, Array<u8>* out, u32* steps_out) {
    // The line back from the row to a whole copy.
    u64 line[kGenomeMaxSteps + 1];
    u32 n = 0;
    Array<u8> entry, base;
    u8 head[24];
    for (u64 r = row;;) {
        u64 at = 0;
        u32 len = 0;
        if (n > kGenomeMaxSteps || r >= f.rows || !f.row(f.ctx, r, &at, &len) || at >= f.bytes)
            return false;
        const u64 have = f.bytes - at < sizeof head ? f.bytes - at : sizeof head;
        u32 steps = 0;
        u64 back = 0;
        usize body = 0;
        if (!f.read(f.ctx, at, have, head) || !genome_entry_head(head, static_cast<usize>(have), &steps, &back, &body))
            return false;
        // Each step back is one nearer a whole copy: a line that isn't is damaged.
        if (n == 0 && steps_out)
            *steps_out = steps;
        line[n++] = r;
        if (steps == 0)
            break;
        if (back > r)
            return false;
        r -= back;
    }
    out->clear();
    for (u32 k = n; k-- > 0;) {
        u64 at = 0, end = f.bytes;
        u32 len = 0, next_len = 0;
        if (!f.row(f.ctx, line[k], &at, &len))
            return false;
        if (line[k] + 1 < f.rows && !f.row(f.ctx, line[k] + 1, &end, &next_len))
            return false;
        if (end < at || end > f.bytes || end - at > static_cast<u64>(len) + 64)
            return false;
        entry.resize_unfilled(static_cast<usize>(end - at));
        u32 steps = 0;
        u64 back = 0;
        usize body = 0;
        if (!f.read(f.ctx, at, end - at, entry.data()) ||
            !genome_entry_head(entry.data(), entry.size(), &steps, &back, &body))
            return false;
        if (steps == 0) {
            if (k != n - 1 || entry.size() - body != len)
                return false;
            out->clear();
            out->append(entry.data() + body, len);
            continue;
        }
        if (k == n - 1) // the line's far end must be a whole copy
            return false;
        base.clear();
        base.append(out->data(), out->size());
        if (!genome_patch(entry.data() + body, entry.size() - body, base.data(), static_cast<u32>(base.size()), len,
                          out))
            return false;
    }
    return true;
}

} // namespace nova2
