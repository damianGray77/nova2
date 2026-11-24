#include "nova2/watch.h"

namespace nova2 {

u32 size_bin(u32 bytes) {
    return bytes < 256 ? 0 : bytes < 1024 ? 1 : bytes < 4096 ? 2 : bytes < 16384 ? 3 : 4;
}

u32 age_bin(u64 ticks) {
    return ticks < 1000000 ? 0 : ticks < 4000000 ? 1 : ticks < 16000000 ? 2 : 3;
}

void CascadeWatch::init(u32 n) {
    places.resize(0);
    for (u32 i = 0; i < n; ++i)
        places.push(CascadePlace{}); // pushed, not resized: resize fills with zeros
    cascades.resize(0);
}

void CascadeWatch::respawned(u32 place) {
    if (place < places.size())
        places[place] = CascadePlace{};
}

bool CascadeWatch::passes(const CascadePlace& b, const CascadeInput& in, u64 t, u64 ta) const {
    const u64 K = kCascadeTicks;
    // The move must start within K ticks of ta, and at most K before t.
    const u64 from = ta > K && t > K ? (ta - K > t - K ? ta - K : t - K) : (t > K ? t - K : 0);
    const u64 to = ta + K < t ? ta + K : t;
    const u64 oldest = t + 1 - b.seen; // the first tick whose voltage it has
    if (in.trip) {
        // A rise past its maximum: above it at s, at or under it the tick before.
        for (u64 s = from; s <= to; ++s)
            if (s > oldest && mv_at(b, s) > in.maximum && mv_at(b, s - 1) <= in.maximum)
                return true;
    }
    if (in.cut) {
        // A fall: back from t over the ticks its voltage never rose, then
        // the first drop of that run.
        u64 u = t;
        while (u > oldest && mv_at(b, u) <= mv_at(b, u - 1))
            --u;
        u64 s = u + 1;
        while (s <= t && mv_at(b, s) >= mv_at(b, s - 1))
            ++s;
        if (s <= t && s >= from && s <= to)
            return true;
    }
    return false;
}

u32 CascadeWatch::step(u64 t, const CascadeInput* in, u32 n, const u32* begin, const u32* adj) {
    const u32 before = static_cast<u32>(cascades.size());
    const u64 K = kCascadeTicks;
    while (places.size() < n)
        places.push(CascadePlace{});
    for (u32 i = 0; i < n; ++i) {
        CascadePlace& p = places[i];
        p.mv[t % kCascadeRing] = in[i].mv;
        p.seen += p.seen < kCascadeRing ? 1 : 0;
        if (in[i].struck)
            p.strike = t;
        if (in[i].swelled)
            p.swell = t;
    }
    for (u32 i = 0; i < n; ++i) {
        if (!in[i].cut && !in[i].trip)
            continue;
        CascadePlace& b = places[i];
        // The neighbour that passed it on: the earliest to go off, then the
        // lowest numbered. One that went off this tick passes nothing, nor
        // one this place has gone off since (with or after it): that one
        // answered it, as a drop-out's places cut out tick after tick do.
        u32 best = ~0u;
        u64 best_t = ~0ull;
        for (u32 k = begin[i]; k < begin[i + 1]; ++k) {
            const u32 a = adj[k];
            const CascadePlace& A = places[a];
            if (A.off == ~0ull || A.off >= t || t - A.off > 2 * K || (b.off != ~0ull && b.off >= A.off) ||
                !passes(b, in[i], t, A.off))
                continue;
            if (A.off < best_t || (A.off == best_t && a < best)) {
                best = a;
                best_t = A.off;
            }
        }
        b.off = t;
        b.why = b.strike != ~0ull && t - b.strike <= K  ? CascadeCause::strike
                : b.swell != ~0ull && t - b.swell <= K ? CascadeCause::swell
                                                       : CascadeCause::load;
        if (best == ~0u) {
            b.cascade = ~0u; // it may begin one
            continue;
        }
        CascadePlace& A = places[best];
        if (A.cascade == ~0u) {
            CascadeRec c;
            c.start = c.last = A.off;
            c.root = best;
            c.size = 1;
            c.cause = A.why;
            A.cascade = static_cast<u32>(cascades.size());
            cascades.push(c);
        }
        CascadeRec& c = cascades[A.cascade];
        if (b.cascade != A.cascade)
            ++c.size;
        b.cascade = A.cascade;
        c.last = t;
    }
    return before;
}

} // namespace nova2
