#include "check.h"
#include "nova2/hash.h"
#include "nova2/rng.h"

using namespace nova2;

namespace {

// The published SplitMix64 reference sequence (Vigna's splitmix64.c, seed
// 1234567). Pins mix64 and the stream step to the real algorithm.
void test_splitmix_known_answers() {
    Rng r = Rng::from_base(1234567);
    CHECK_EQ_U64(r.next(), 6457827717110365317ull);
    CHECK_EQ_U64(r.next(), 3203168211198807973ull);
    CHECK_EQ_U64(r.next(), 9817491932198370423ull);
    CHECK_EQ_U64(r.next(), 4593380528125082431ull);
    CHECK_EQ_U64(r.next(), 16408922859458223821ull);
}

// Golden values for Nova 2's own stream keying (seed, place, purpose, tick),
// computed independently with BigInteger arithmetic in PowerShell (2026-11-11).
// If these change, every seed produces a different world: that is a format
// break, never a refactor.
void test_nova2_stream_golden() {
    CHECK_EQ_U64(Rng::stream_base(42, 0, 0, 0), 0xe5799e85bf3e42daull);
    Rng r(42, 0, 0, 0);
    CHECK_EQ_U64(r.next(), 0x45d6108a98c4fa4dull);
    CHECK_EQ_U64(r.next(), 0x05258403b8934b61ull);
    CHECK_EQ_U64(r.next(), 0xe4b2f330e4b3f8b4ull);
    CHECK_EQ_U64(Rng(42, 7, 3, 1000).next(), 0x7751d09727aab113ull);
    CHECK_EQ_U64(Rng(42, Rng::kWorldPlace, 0, 0).next(), 0x3e198376f9ed7e1full);
}

void test_counter_access() {
    Rng a(9, 3, 5, 77);
    u64 draws[10];
    for (u64& d : draws)
        d = a.next();
    CHECK(a.counter() == 10);
    Rng b(9, 3, 5, 77);
    bool same = true;
    for (u64 k = 0; k < 10; ++k)
        same = same && b.at(k) == draws[k];
    CHECK(same);
    CHECK(b.counter() == 0); // at() does not advance

    Rng c(9, 3, 5, 77);
    c.set_counter(6); // continue a stream mid-way
    CHECK(c.next() == draws[6]);
}

// The order streams are used in changes nothing (build-plan-gate1.md step 2):
// two places' streams drawn interleaved, or one after the other, or in
// reverse place order, give the same numbers. This is what lets host threads
// run places in any order.
void test_order_free() {
    u64 alone_a[8], alone_b[8];
    Rng a1(7, 1, 2, 30), b1(7, 2, 2, 30);
    for (u64& v : alone_a)
        v = a1.next();
    for (u64& v : alone_b)
        v = b1.next();
    Rng b2(7, 2, 2, 30), a2(7, 1, 2, 30); // made in the other order
    bool same = true;
    for (int i = 0; i < 8; ++i) { // drawn interleaved
        same = same && b2.next() == alone_b[i];
        same = same && a2.next() == alone_a[i];
    }
    CHECK(same);
}

void test_streams_differ() {
    u64 base = Rng(42, 0, 0, 0).next();
    CHECK(Rng(43, 0, 0, 0).next() != base);
    CHECK(Rng(42, 1, 0, 0).next() != base);
    CHECK(Rng(42, 0, 1, 0).next() != base);
    CHECK(Rng(42, 0, 0, 1).next() != base);
    CHECK(Rng(42, 0, 0, Rng::kInitTick).next() != base);
    // Swapping place, purpose and tick must not alias.
    CHECK(Rng(42, 5, 9, 0).next() != Rng(42, 9, 5, 0).next());
    CHECK(Rng(42, 5, 0, 9).next() != Rng(42, 9, 0, 5).next());
    CHECK(Rng(42, 0, 5, 9).next() != Rng(42, 0, 9, 5).next());
}

void test_bounded() {
    Rng r(1, 1, 1, 1);
    CHECK(r.below(1) == 0);
    bool in_range = true;
    for (int i = 0; i < 10000; ++i) {
        in_range = in_range && r.below(1000) < 1000;
        in_range = in_range && r.below(0x8000000000000001ull) < 0x8000000000000001ull;
    }
    CHECK(in_range);

    // Coarse uniformity: 60,000 draws over 6 buckets, expect 10,000 each
    // (sd ~91). The draws are seeded, so this can never flake.
    u32 bucket[6] = {};
    for (int i = 0; i < 60000; ++i)
        ++bucket[r.below(6)];
    bool uniform = true;
    for (u32 b : bucket)
        uniform = uniform && b > 9500 && b < 10500;
    CHECK(uniform);

    double lo = 1, hi = 0, sum = 0;
    for (int i = 0; i < 100000; ++i) {
        double u = r.unit();
        lo = u < lo ? u : lo;
        hi = u > hi ? u : hi;
        sum += u;
    }
    CHECK(lo >= 0.0 && hi < 1.0);
    CHECK(sum / 100000 > 0.49 && sum / 100000 < 0.51);

    u32 yes = 0;
    for (int i = 0; i < 100000; ++i)
        yes += r.chance(0.25) ? 1 : 0;
    CHECK(yes > 24000 && yes < 26000);
    CHECK(!r.chance(0.0));
}

void test_hasher() {
    Hasher a, b;
    a.word(1);
    a.word(2);
    b.word(2);
    b.word(1);
    CHECK(a.finish() != b.finish()); // order-sensitive

    CHECK(hash_bytes("ab", 2) != hash_bytes("ab\0", 3)); // tail length counts
    CHECK(hash_bytes("abcdefgh", 8) != hash_bytes("abcdefg", 7));
    CHECK(hash_bytes("abc", 3, 1) != hash_bytes("abc", 3, 2)); // domains separate
    CHECK(hash_bytes("abc", 3) == hash_bytes("abc", 3));
    CHECK(Hasher().finish() != Hasher(1).finish());
    CHECK(mix64(0) == 0); // known fixed point of the finalizer -- why the Hasher adds golden
}

} // namespace

void suite_rng() {
    test_splitmix_known_answers();
    test_nova2_stream_golden();
    test_counter_access();
    test_order_free();
    test_streams_differ();
    test_bounded();
    test_hasher();
}
