// The noise floors (nova2/noise.h; design.md 7.5, appendix C;
// build-plan-gate1.md step 8): the fixed-point log2 and geometric draw,
// the draw's divide by a reciprocal giving the divide's quotient (pass 9),
// error rates through a core's countdown at stock and other clock levels,
// the interpreter's results going wrong, the cosmic-ray floor's rate and
// spread, rates of 0 giving nothing, and the same seed giving the same flips.
#include <cmath>
#include <cstring>

#include "check.h"
#include "nova2/cpu.h"
#include "nova2/hash.h"
#include "nova2/noise.h"
#include "nova2/rng.h"

using namespace nova2;

namespace {

void test_log2_and_gaps() {
    CHECK(log2_q56(1) == 0);
    CHECK(log2_q56(2) == 1ull << 56);
    CHECK(log2_q56(1ull << 40) == 40ull << 56);
    const double want3 = 1.5849625007211562 * 72057594037927936.0; // log2(3) * 2^56
    const double got3 = static_cast<double>(log2_q56(3));
    CHECK(fabs(got3 - want3) < 1e5); // about 1e-12 of the value
    const u64 top = log2_q56(~0ull);
    CHECK(top <= (64ull << 56) && top >= (64ull << 56) - 4);
    CHECK(to_p64(0) == 0 && to_p64(0.25) == 1ull << 62 && to_p64(0.7) == 1ull << 63);

    CHECK(geometric_gap(0, 12345) == ~0ull); // a rate of 0: never
    Rng r(1, 0, 0, 0);
    const u32 n = 200000;
    double sum = 0;
    for (u32 i = 0; i < n; ++i)
        sum += static_cast<double>(geometric_gap(to_p64(1e-3), r.next()));
    CHECK(sum / n > 990 && sum / n < 1010); // the mean of a geometric: 1 / p
    u32 ones = 0;
    for (u32 i = 0; i < n; ++i)
        ones += geometric_gap(to_p64(0.1), r.next()) == 1 ? 1 : 0;
    CHECK(ones > 0.097 * n && ones < 0.103 * n); // P(gap = 1) = p
    double half = 0;
    for (u32 i = 0; i < n; ++i)
        half += static_cast<double>(geometric_gap(to_p64(0.5), r.next()));
    CHECK(half / n > 1.98 && half / n < 2.02);
    // A tiny rate is still told apart from never.
    CHECK(geometric_gap(to_p64(1e-9), r.next()) != ~0ull);
}

// How far the fast log strays from the exact one at v.
u64 near_off(u64 v) {
    const u64 a = log2_q56_near(v), b = log2_q56(v);
    return a > b ? a - b : b - a;
}

// The fast log (geometric_gap's) stays well inside its slack of 2^12: on
// every exponent, at both edges of every span of its table, and on random
// values of every size; and geometric_gap gives what the exact log gives,
// at rates from the largest to ones so small that most draws fall near a
// multiple of the log and go to the exact log.
void test_fast_log() {
    u64 worst = 0;
    for (u32 e = 0; e < 64; ++e) {
        const u64 one = 1ull << e;
        const u64 vs[] = {one, one - 1, one + 1, one | (one - 1), one | (one >> 1)};
        for (u32 k = 0; k < 5; ++k)
            if (vs[k] && near_off(vs[k]) > worst)
                worst = near_off(vs[k]);
        if (e < 8)
            continue;
        for (u64 i = 0; i < 256; ++i) { // either side of each span's edges
            const u64 edge = one | (i << (e - 8));
            const u64 ws[] = {edge, edge - 1, edge + 1};
            for (u32 k = 0; k < 3; ++k)
                if (near_off(ws[k]) > worst)
                    worst = near_off(ws[k]);
        }
    }
    Rng r(3, 0, 0, 0);
    for (u32 i = 0; i < 2000000; ++i) {
        const u64 v = r.next() >> (i & 63);
        if (v && near_off(v) > worst)
            worst = near_off(v);
    }
    printf("  the fast log's worst: %llu 2^-56ths (slack 4096)\n", worst);
    CHECK(worst <= 256);

    const u64 top = 64ull << 56;
    const u64 ps[] = {1ull << 63, to_p64(0.1), to_p64(1e-3), to_p64(1e-6), to_p64(1e-9), to_p64(1e-12), 1ull << 20,
                      1000, 1};
    u32 wrong = 0;
    for (u32 k = 0; k < sizeof ps / sizeof ps[0]; ++k) {
        const GapLog g = keep_gap_log(ps[k]);
        const u64 b = g.log;
        CHECK(b == gap_log(ps[k]));
        for (u32 i = 0; i < 200000; ++i) {
            const u64 d = r.next();
            const u64 want = b ? (top - log2_q56(d ? d : 1)) / b + 1 : ~0ull;
            wrong += geometric_gap(ps[k], g, d) != want ? 1 : 0;
        }
        wrong += geometric_gap(ps[k], g, 0) != (b ? top / b + 1 : ~0ull) ? 1 : 0;
        wrong += geometric_gap(ps[k], g, ~0ull) != (b ? (top - log2_q56(~0ull)) / b + 1 : ~0ull) ? 1 : 0;
    }
    CHECK(wrong == 0);
}

// a / b by b's reciprocal against the divide.
u64 quotient_off(u64 a, const GapLog& g) { return gap_quotient(a, g) != a / g.log ? 1 : 0; }

// gap_quotient gives the divide's quotient (pass 9): by 1, by every power
// of two and either side of it, by divisors near 2^63 and 2^64 - 1, by the
// logs of rates from the largest a gap is drawn at to the smallest, and by
// divisors at random; each of a dividend at its edges (0, 1, either side
// of the divisor, of a multiple of it and of the largest), and at random.
void test_gap_quotient() {
    Array<u64> bs;
    bs.push(1);
    bs.push(3);
    for (u32 e = 1; e < 64; ++e) {
        const u64 one = 1ull << e;
        bs.push(one - 1);
        bs.push(one);
        bs.push(one + 1);
    }
    const u64 far[] = {(1ull << 63) - 3, (1ull << 63) + 3, ~0ull - 2, ~0ull - 1, ~0ull, ~0ull / 3, ~0ull / 3 * 2};
    for (u32 k = 0; k < sizeof far / sizeof far[0]; ++k)
        bs.push(far[k]);
    const double ps[] = {0.5, 0.1, 1e-3, 1e-6, 1e-9, 1e-12, 1e-15};
    for (u32 k = 0; k < sizeof ps / sizeof ps[0]; ++k)
        bs.push(gap_log(to_p64(ps[k])));
    bs.push(gap_log(~0ull)); // rowhammer at its full strength
    bs.push(gap_log(1ull << 63));
    Rng r(9, 0, 0, 0);
    for (u32 i = 0; i < 3000; ++i) {
        const u64 b = r.next() >> (i & 63);
        bs.push(b ? b : 1);
    }
    u64 tried = 0, wrong = 0;
    for (usize k = 0; k < bs.size(); ++k) {
        const u64 b = bs[k];
        const GapLog g = gap_divisor(b);
        CHECK(g.log == b && g.inv == ~0ull / b);
        const u64 most = ~0ull / b; // the largest quotient
        const u64 edges[] = {0, 1, b - 1, b, b + 1, most * b - 1, most * b, ~0ull - 1, ~0ull, 64ull << 56};
        for (u32 e = 0; e < sizeof edges / sizeof edges[0]; ++e)
            wrong += quotient_off(edges[e], g);
        tried += sizeof edges / sizeof edges[0];
        for (u32 i = 0; i < 300; ++i) {
            const u64 a = r.next() >> (i & 63);
            const u64 q = most == ~0ull ? r.next() : r.next() % (most + 1); // a multiple of b, and either side
            wrong += quotient_off(a, g) + quotient_off(q * b, g);
            wrong += (q ? quotient_off(q * b - 1, g) : 0) + (q * b < ~0ull ? quotient_off(q * b + 1, g) : 0);
            tried += 4;
        }
    }
    CHECK(gap_divisor(0).log == 0 && gap_divisor(0).inv == 0);
    const u64 divisors = bs.size();
    printf("  gap_quotient: %llu quotients by %llu divisors, %llu unlike the divide's\n", tried, divisors, wrong);
    CHECK(wrong == 0);
}

struct Rig {
    Memory m;
    Core cores[2];
    PlaceEnv env;
    Noise noise;

    explicit Rig(NoiseSettings s, u64 seed = 7) {
        static const MemDeviceSpec devs[] = {{16384, 64, 900}, {8192, 64, 950}};
        MemorySpec ms;
        ms.page_size = 256;
        ms.region_size = 1024;
        ms.devices = devs;
        ms.device_count = 2;
        CHECK(m.init(ms, nullptr));
        cores[0].kind = DeviceKind::p_core;
        cores[1].kind = DeviceKind::e_core;
        env.place_id = 3;
        CHECK(noise.init(&m, cores, 2, &env, seed, s, PowerSettings{}, nullptr));
    }

    // Counts n results through core 0's countdown, as the interpreter does.
    u64 results(u64 n) {
        ErrorClock& c = cores[0].clock;
        u64 wrong = 0;
        for (u64 i = 0; i < n; ++i)
            if (c.left != ~0ull && --c.left == 0) { // as the interpreter counts
                c.next(&c);
                ++wrong;
            }
        return wrong;
    }
};

void test_error_rates() {
    NoiseSettings s;
    s.p_floor = 1e-3;
    Rig g(s);
    const u64 wrong = g.results(1000000); // expect 1,000 (sd about 32)
    CHECK(wrong > 900 && wrong < 1100 && g.noise.result_errors == wrong);
    // The clock: each tenth above stock doubles the rate, each below halves it.
    const double r10 = static_cast<double>(g.noise.rate(0, 10));
    CHECK(fabs(static_cast<double>(g.noise.rate(0, 15)) / r10 - 32) < 1e-6);
    CHECK(fabs(static_cast<double>(g.noise.rate(0, 5)) / r10 - 1.0 / 32) < 1e-6);
    // Moving to a new level redraws the countdown, and the rate follows.
    set_level(g.cores[0].clock, 15);
    const u64 fast = g.results(100000); // expect 3,200
    CHECK(fast > 2950 && fast < 3450);
    // A floor of 0 makes no errors, at any level.
    NoiseSettings zero;
    zero.p_floor = zero.e_floor = 0;
    Rig z(zero);
    CHECK(z.results(1000000) == 0 && z.cores[0].clock.left == ~0ull);
    set_level(z.cores[0].clock, 15);
    CHECK(z.results(1000) == 0);
}

// Through the interpreter: at a rate of one half, about half of MOV # 0's
// results come out wrong, each by one bit.
void test_wrong_results() {
    NoiseSettings s;
    s.p_floor = 0.5;
    Rig g(s);
    const u32 code = g.m.alloc(kFirstProgramHolder, 64, 2, 0);
    g.m.bytes()[code] = encode(kMov, kImm);
    g.m.bytes()[code + 1] = 0;
    ExecContext x;
    x.holder = kFirstProgramHolder;
    x.clock = &g.cores[0].clock;
    const CostTable t = default_p_costs();
    u32 wrong = 0, one_bit = 0;
    for (u32 i = 0; i < 2000; ++i) {
        x.r.ip = code;
        step(g.m, x, t);
        if (x.r.a != 0) {
            ++wrong;
            one_bit += (x.r.a & (x.r.a - 1)) == 0 ? 1 : 0;
        }
    }
    CHECK(wrong > 900 && wrong < 1100 && one_bit == wrong);
}

void test_cosmic() {
    NoiseSettings s;
    s.cosmic = 1e-4; // 24 KB: about 19.7 flips a tick
    Rig g(s);
    const u32 writes = g.m.region_writes();
    for (u32 t = 0; t < 100; ++t) {
        g.noise.cosmic_flips();
        ++g.env.tick;
    }
    const double expect = 24576.0 * 8 * 1e-4 * 100; // 1,966
    CHECK(g.noise.cosmic > expect - 150 && g.noise.cosmic < expect + 150);
    // Spread by size: the 16 KB device takes about two thirds.
    u32 set_bits[2] = {};
    for (u32 d = 0; d < 2; ++d) {
        const MemDevice& dv = g.m.device(d);
        for (u32 i = 0; i < dv.size; ++i) {
            const u8 b = g.m.bytes()[dv.base + i];
            for (u32 k = 0; k < 8; ++k)
                set_bits[d] += (b >> k) & 1;
        }
    }
    const double share = static_cast<double>(set_bits[0]) / (set_bits[0] + set_bits[1]);
    CHECK(share > 0.6 && share < 0.73);
    CHECK(g.m.region_writes() > writes); // flips in the region count as writes to it
    // The same seed gives the same flips; another seed doesn't.
    Rig a(s, 11), b(s, 11), c(s, 12);
    for (u32 t = 0; t < 20; ++t) {
        a.noise.cosmic_flips();
        b.noise.cosmic_flips();
        c.noise.cosmic_flips();
        ++a.env.tick;
        ++b.env.tick;
        ++c.env.tick;
    }
    const u64 ha = hash_bytes(a.m.bytes(), a.m.size()), hb = hash_bytes(b.m.bytes(), b.m.size());
    CHECK(ha == hb && ha != hash_bytes(c.m.bytes(), c.m.size()));
    // A rate of 0 flips nothing.
    NoiseSettings off;
    off.cosmic = 0;
    Rig z(off);
    for (u32 t = 0; t < 100; ++t)
        z.noise.cosmic_flips();
    CHECK(z.noise.cosmic == 0);
}

// Many flips a tick: at first more than are drawn ahead of being made
// (kFlipsAhead, noise.cpp), so the oldest are made as new ones are drawn,
// and later fewer, all made at the tick's end; with a falling rate
// redrawing the countdowns now and then. The bytes, the failed bytes' list
// in order, the count and the reset region's writes are the loop's before
// (pass 11's, a few flips drawn and then made), held here by their values
// from that loop.
void test_cosmic_ahead() {
    NoiseSettings s;
    s.cosmic = 2e-3;       // 24 KB: about 390 flips a tick
    s.cosmic_start = 2e-2; // about 3,900 at first
    s.cosmic_half_life = 24;
    Rig g(s, 5);
    Array<u32> failed;
    u64 list = 0, listed = 0;
    for (u32 t = 0; t < 120; ++t) {
        failed.resize(0);
        g.noise.cosmic_flips(t % 3 ? &failed : nullptr);
        for (u32 i = 0; i < failed.size(); ++i)
            list = mix64(list ^ failed[i]);
        listed += failed.size();
        ++g.env.tick;
    }
    const u64 bytes = hash_bytes(g.m.bytes(), g.m.size());
    printf("  noise: %llu cosmic flips in 120 ticks (%llu listed), %u reset-region writes; bytes %016llx, "
           "list %016llx\n",
           static_cast<unsigned long long>(g.noise.cosmic), static_cast<unsigned long long>(listed),
           g.m.region_writes(), static_cast<unsigned long long>(bytes), static_cast<unsigned long long>(list));
    CHECK(g.noise.cosmic == 170753 && listed == 113770 && g.m.region_writes() == 7099);
    CHECK(bytes == 0x508b6aae14d262bfull && list == 0x06076bcbde4b8b3cull);
}

// A falling cosmic rate (7.5): from 1e-4 to a floor of 1e-6, its excess
// halving every 80 ticks, so a step every 10. The flips follow the curve
// tick by tick; a start equal to the floor is no fall at all; and a start
// below the floor is refused.
void test_cosmic_fall() {
    NoiseSettings s;
    s.cosmic = 1e-6;
    s.cosmic_start = 1e-4;
    s.cosmic_half_life = 80;
    Rig g(s);
    const double bits = 24576.0 * 8;
    u64 first = 0, last = 0;
    double want = 0;
    for (u32 t = 0; t < 800; ++t) {
        const u64 c0 = g.noise.cosmic;
        g.noise.cosmic_flips();
        if (t < 10)
            first += g.noise.cosmic - c0;
        if (t >= 700)
            last += g.noise.cosmic - c0;
        want += bits * (1e-6 + 99e-6 * pow(2.0, -static_cast<double>(t / 10) / 8));
        ++g.env.tick;
    }
    CHECK(first > 150 && first < 245); // about 197 in the first step
    CHECK(last > 8 && last < 45);      // about 23 in the last 100 ticks, near the floor
    CHECK(g.noise.cosmic > want - 200 && g.noise.cosmic < want + 200); // about 2,500 in all
    NoiseSettings flat;
    flat.cosmic = 1e-5;
    NoiseSettings level = flat;
    level.cosmic_start = 1e-5;
    level.cosmic_half_life = 3;
    Rig a(flat), b(level);
    for (u32 t = 0; t < 200; ++t) {
        a.noise.cosmic_flips();
        b.noise.cosmic_flips();
        ++a.env.tick;
        ++b.env.tick;
    }
    CHECK(a.noise.cosmic > 0 && a.noise.cosmic == b.noise.cosmic);
    CHECK(hash_bytes(a.m.bytes(), a.m.size()) == hash_bytes(b.m.bytes(), b.m.size()));
    NoiseSettings below = flat;
    below.cosmic_start = 1e-6;
    Noise refused;
    CHECK(!refused.init(&a.m, a.cores, 2, &a.env, 7, below, PowerSettings{}, nullptr));
    printf("  noise: a cosmic rate falling from 1e-4 to 1e-6 with a half-life of 80 ticks flipped %llu bits in its "
           "first 10 ticks and %llu in ticks 700 to 800 (%llu in 800, %.0f expected)\n",
           static_cast<unsigned long long>(first), static_cast<unsigned long long>(last),
           static_cast<unsigned long long>(g.noise.cosmic), want);
}

} // namespace

void suite_noise() {
    test_log2_and_gaps();
    test_fast_log();
    test_gap_quotient();
    test_error_rates();
    test_wrong_results();
    test_cosmic();
    test_cosmic_ahead();
    test_cosmic_fall();
}
