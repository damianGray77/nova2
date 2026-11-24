// A place's memory (nova2/memory.h; design.md 6.3, 9.3, 9.4;
// build-plan-gate1.md step 3): the layout, size classes and spans, access by
// page and ownership by allocation, PROTECT's whole-page rule, strict places,
// device choice, nothing zeroed, gifts, the written extent, and a million
// random calls with the invariant check holding throughout.
#include <cstring>

#include "check.h"
#include "nova2/memory.h"
#include "nova2/rng.h"

using namespace nova2;

namespace {

constexpr u32 P1 = kFirstProgramHolder; // three programs and an idle run
constexpr u32 P2 = kFirstProgramHolder + 1;
constexpr u32 P3 = kFirstProgramHolder + 2;
constexpr u32 RUN = 1;

// Two devices: 0 is slow and steady (lowest threshold: the reset region goes
// here, after page 0), 1 is fast.
const MemDeviceSpec kTwo[] = {{4096, 12 * 16, 900}, {8192, 4 * 16, 1000}};

bool make(Memory& m, u32 page, bool strict = false, u32 steps = 2, u32 max_alloc = 4096,
          const MemDeviceSpec* devs = kTwo, u32 ndev = 2, u32 region = 1024) {
    MemorySpec s;
    s.page_size = page;
    s.strict = strict;
    s.class_steps = steps;
    s.max_alloc = max_alloc;
    s.region_size = region;
    s.devices = devs;
    s.device_count = ndev;
    TextBuf why;
    const bool ok = m.init(s, &why);
    if (!ok)
        fprintf(stderr, "  init: %s\n", why.c_str());
    return ok;
}

bool checked(const Memory& m) {
    TextBuf why;
    const bool ok = m.check(&why);
    if (!ok)
        fprintf(stderr, "  %s\n", why.c_str());
    return ok;
}

void test_layout() {
    Memory m;
    CHECK(make(m, 256));
    CHECK(m.size() == 12288 && m.page_count() == 48);
    CHECK(m.device(0).base == 0 && m.device(1).base == 4096); // end to end
    CHECK(m.device_by_speed(0) == 1 && m.device_by_speed(1) == 0); // lowest latency first
    CHECK(m.page(0).state == PageState::reserved);
    CHECK(!m.allows(0, P1, kRead) && !m.allows(0, RUN, kRead)); // address 0 is never valid
    // The reset region: the lowest-threshold device, after page 0.
    CHECK(m.region_device() == 0 && m.region_base() == 256 && m.region_size() == 1024);
    for (u32 p = 1; p <= 4; ++p)
        CHECK(m.page(p).state == PageState::region);
    CHECK(m.page(5).state == PageState::free);
    CHECK(m.free_bytes() == 12288 - 256 - 1024);
    CHECK(checked(m));

    // Size classes: two per doubling up to the page, or powers of two.
    Memory a;
    CHECK(make(a, 64));
    const u32 two[] = {8, 16, 24, 32, 48, 64};
    CHECK(a.class_count() == 6);
    for (u32 c = 0; c < 6 && c < a.class_count(); ++c)
        CHECK(a.class_size(c) == two[c]);
    Memory b;
    CHECK(make(b, 256, false, 1));
    const u32 one[] = {8, 16, 32, 64, 128, 256};
    CHECK(b.class_count() == 6);
    for (u32 c = 0; c < 6 && c < b.class_count(); ++c)
        CHECK(b.class_size(c) == one[c]);

    // The region rounds up to whole pages, and goes on a device not at
    // address 0 from that device's start.
    const MemDeviceSpec flipped[] = {{4096, 4 * 16, 1000}, {4096, 12 * 16, 800}};
    Memory c;
    CHECK(make(c, 256, false, 2, 4096, flipped, 2, 300));
    CHECK(c.region_device() == 1 && c.region_base() == 4096 && c.region_size() == 512);
    CHECK(checked(c));
}

void test_constraints() {
    Memory m;
    MemorySpec s;
    s.devices = kTwo;
    s.device_count = 2;
    s.page_size = 256;
    s.region_size = 4096; // its device is 4096 bytes, less page 0
    TextBuf why;
    CHECK(!m.init(s, &why) && strstr(why.c_str(), "region.size") != nullptr);
    s.region_size = 3840; // exactly fits
    CHECK(m.init(s, nullptr));
    s.page_size = 100;
    CHECK(!m.init(s, nullptr));
    s.page_size = 8192;
    CHECK(!m.init(s, nullptr));
    s.page_size = 256;
    s.max_alloc = 128; // less than a page
    CHECK(!m.init(s, nullptr));
    s.max_alloc = 4096;
    const MemDeviceSpec odd[] = {{1000, 64, 900}};
    s.devices = odd;
    s.device_count = 1;
    CHECK(!m.init(s, nullptr)); // not whole pages
    s.region_size = 0;
    s.devices = kTwo;
    s.device_count = 2;
    CHECK(!m.init(s, nullptr)); // a place needs a reset region
}

void test_slabs() {
    Memory m;
    CHECK(make(m, 256));
    // Kin of one length pack into one page, side by side (class 24).
    const u32 a = m.alloc(P1, 18, 2, 0);
    const u32 b = m.alloc(P2, 18, 2, 0);
    CHECK(a != 0 && b == a + 24);
    CHECK(m.page_of(a) == m.page_of(b));
    CHECK(m.device_of(a) == 1); // "any" is the fastest with room
    // Another class, or another permission, takes another page.
    const u32 c = m.alloc(P1, 40, 2, 0);
    const u32 d = m.alloc(P1, 18, 1, 0);
    CHECK(m.page_of(c) != m.page_of(a) && m.page_of(d) != m.page_of(a) && m.page_of(c) != m.page_of(d));
    // First fit: a freed slot is the next one used.
    CHECK(m.free(P1, a));
    CHECK(m.alloc(P3, 20, 2, 0) == a);
    // The slot's whole extent maps to its allocation.
    CHECK(m.alloc_at(b) != 0 && m.alloc_at(b + 23) == m.alloc_at(b) && m.alloc_at(b + 24) == 0);
    // A lax place has no no-execute bit (9.3): data is "both", and packs with it.
    const u32 e = m.alloc(P2, 18, 0, 0);
    CHECK(e == b + 24 && m.page(m.page_of(e)).perm == Perm::both);
    CHECK(checked(m));
}

void test_access() {
    Memory m;
    CHECK(make(m, 256));
    const u32 a = m.alloc(P1, 18, 0, 0); // data
    const u32 b = m.alloc(P2, 18, 0, 0); // the same page
    CHECK(m.page_of(a) == m.page_of(b));
    // Access is by page: P2 reaches P1's bytes; P3, holding nothing here, doesn't.
    CHECK(m.allows(a, P2, kRead | kWrite) && m.allows(b, P1, kWrite));
    CHECK(!m.allows(a, P3, kRead));
    // The slab's free slots and its tail are reachable too: nothing within a page is protected.
    CHECK(m.allows(a + 200, P1, kWrite));
    // A lax place has no no-execute bit, so its data pages run too (9.3); a
    // strict place's don't.
    CHECK(m.allows(a, P1, kRun) && m.page(m.page_of(a)).perm == Perm::both);
    Memory st;
    CHECK(make(st, 256, true));
    const u32 sd = st.alloc(P1, 18, 0, 0);
    CHECK(sd != 0 && st.allows(sd, P1, kRead | kWrite) && !st.allows(sd, P1, kRun));
    const u32 x = m.alloc(P1, 18, 1, 0); // executable
    CHECK(m.allows(x, P1, kRead | kRun) && !m.allows(x, P1, kWrite));
    const u32 y = m.alloc(P1, 18, 2, 0); // both
    CHECK(m.allows(y, P1, kRead | kWrite | kRun));
    // Freeing your last allocation in a page loses the page.
    CHECK(m.free(P2, b));
    CHECK(!m.allows(a, P2, kRead));
    // Ranges: every page must be allowed; the prefix stops at the first that isn't.
    const u32 big = m.alloc(P1, 600, 0, 0); // three whole pages
    CHECK(m.allows_range(big, 768, P1, kWrite));
    CHECK(!m.allows_range(big, 769, P1, kWrite)); // the page after the span is free
    CHECK(m.usable_prefix(big + 700, 500, P1, kWrite) == 68);
    CHECK(!m.allows_range(m.size() - 4, 8, P1, kRead)); // past the end
    CHECK(m.usable_prefix(0xFFFFFFF0u, 64, P1, kRead) == 0);
    // The reset region: idle runs only.
    CHECK(m.allows(m.region_base(), RUN, kRead | kWrite | kRun));
    CHECK(!m.allows(m.region_base(), P1, kRead));
    CHECK(checked(m));
}

void test_spans() {
    Memory m;
    CHECK(make(m, 256));
    const u32 a = m.alloc(P1, 600, 2, 0);
    CHECK(a != 0 && a % 256 == 0);
    CHECK(m.record(m.alloc_at(a)).extent == 768);
    for (u32 p = m.page_of(a); p < m.page_of(a) + 3; ++p)
        CHECK(m.page(p).state == PageState::span);
    // Whole pages are never shared.
    const u32 b = m.alloc(P2, 8, 2, 0);
    CHECK(m.page_of(b) < m.page_of(a) || m.page_of(b) > m.page_of(a) + 2);
    CHECK(!m.allows(a + 700, P2, kRead));
    // Sizes: 0 and more than the largest allocation fail; the largest works.
    CHECK(m.alloc(P1, 0, 0, 0) == 0);
    CHECK(m.alloc(P1, 4097, 0, 0) == 0);
    CHECK(m.alloc(P1, 4096, 0, 0) != 0);
    CHECK(m.free(P1, a));
    CHECK(m.page(m.page_of(a)).state == PageState::free);
    CHECK(checked(m));
}

void test_devices() {
    Memory m;
    CHECK(make(m, 256));
    CHECK(m.device_of(m.alloc(P1, 64, 0, 1)) == 1);   // the fastest
    CHECK(m.device_of(m.alloc(P1, 64, 0, -1)) == 0);  // the slowest
    CHECK(m.device_of(m.alloc(P1, 64, 0, 99)) == 0);  // clamped: past the slowest
    CHECK(m.device_of(m.alloc(P1, 64, 0, -99)) == 1); // clamped: past the fastest
    CHECK(m.device_of(m.alloc(P1, 64, 0, static_cast<i32>(0x80000000u))) == 1);
    CHECK(m.device_of(m.alloc_on(P1, 64, Perm::data, 0)) == 0); // by index (stacks)
    // Fill the fast device; "any" falls back to the slow one, "fastest" fails.
    while (m.alloc(P2, 4096, 0, 1))
        ;
    while (m.alloc(P2, 256, 0, 1))
        ;
    while (m.alloc(P2, 8, 0, 1))
        ;
    CHECK(m.alloc(P3, 8, 0, 1) == 0);
    const u32 any = m.alloc(P3, 8, 0, 0);
    CHECK(any != 0 && m.device_of(any) == 0);
    CHECK(checked(m));
}

void test_protect() {
    Memory lax;
    CHECK(make(lax, 64));
    const u32 a = lax.alloc(P1, 64, 0, 0); // a whole page of data
    CHECK(lax.protect(P1, a + 10, 1));     // any address in the page
    CHECK(lax.allows(a, P1, kRun) && !lax.allows(a, P1, kWrite));
    CHECK(lax.protect(P1, a, 2)); // both: fine on a lax place
    CHECK(lax.protect(P1, a, 0) && lax.page(lax.page_of(a)).perm == Perm::both); // data is "both" here (9.3)
    // Not every allocation in the page is yours: PROTECT fails.
    const u32 b = lax.alloc(P1, 18, 0, 0);
    const u32 c = lax.alloc(P2, 18, 0, 0);
    CHECK(lax.page_of(b) == lax.page_of(c));
    CHECK(!lax.protect(P1, b, 1));
    CHECK(lax.free(P2, c));
    CHECK(lax.protect(P1, b, 1)); // now it is
    // The page joins its new permission's group: a new executable 18 lands in its free slot.
    CHECK(lax.page_of(lax.alloc(P3, 18, 1, 0)) == lax.page_of(b));
    CHECK(!lax.protect(P1, lax.region_base(), 0)); // the region is nobody's
    CHECK(!lax.protect(P1, 0, 0));
    CHECK(checked(lax));

    Memory strict;
    CHECK(make(strict, 64, true));
    CHECK(strict.alloc(P1, 18, 2, 0) == 0); // no "both" on a strict place
    const u32 s = strict.alloc(P1, 64, 0, 0);
    CHECK(s != 0 && !strict.protect(P1, s, 2));
    CHECK(strict.protect(P1, s, 1));
    CHECK(checked(strict));
}

void test_dirty_and_free() {
    Memory m;
    CHECK(make(m, 256));
    const u32 a = m.alloc(P1, 18, 2, 0);
    memcpy(m.bytes() + a, "eighteen bytes!!!", 18);
    CHECK(!m.free(P1, a + 1)); // not its start
    CHECK(!m.free(P2, a));     // not yours
    CHECK(m.free(P1, a));
    CHECK(!m.free(P1, a)); // already free
    // Nothing is zeroed: the same slot comes back with the corpse's bytes.
    const u32 b = m.alloc(P2, 18, 2, 0);
    CHECK(b == a && memcmp(m.bytes() + b, "eighteen bytes!!!", 18) == 0);
    CHECK(checked(m));
}

void test_gifts() {
    Memory m;
    CHECK(make(m, 256));
    const u32 a = m.alloc(P1, 18, 2, 0);
    const u32 id = m.alloc_at(a);
    const u32 v = m.page(m.page_of(a)).version;
    m.give(id, P2);
    CHECK(m.record(id).holder == P2 && m.page(m.page_of(a)).version != v);
    CHECK(!m.allows(a, P1, kRead) && m.allows(a, P2, kRead));
    CHECK(!m.free(P1, a) && m.free(P2, a));
    // free_all takes everything one holder has, and nothing else.
    for (int i = 0; i < 20; ++i) {
        m.alloc(P1, 8 + static_cast<u32>(i) * 30, static_cast<u32>(i % 3), 0);
        m.alloc(P3, 16, 0, 0);
    }
    m.free_all(P1);
    CHECK(m.first_of(P1) == 0 && m.first_of(P3) != 0);
    m.free_all(RUN); // a run holding nothing
    CHECK(checked(m));
}

void test_written_extent() {
    Memory m;
    CHECK(make(m, 256));
    const u32 a = m.alloc(P1, 18, 2, 0);
    const u32 id = m.alloc_at(a);
    CHECK(m.record(id).wlo >= m.record(id).whi); // nothing written yet
    m.note_write(a + 2, 5, P1);
    CHECK(m.record(id).wlo == 2 && m.record(id).whi == 7);
    m.note_write(a + 10, 1, P2); // someone else's write doesn't count
    CHECK(m.record(id).whi == 7);
    m.note_write(a, 40, P1); // runs past the allocation: clamped to its size
    CHECK(m.record(id).wlo == 0 && m.record(id).whi == 18);
    // A CPY across two neighbours widens each holder's own.
    const u32 b = m.alloc(P1, 18, 2, 0);
    m.note_write(a + 16, 12, P1);
    CHECK(m.record(m.alloc_at(b)).wlo == 0 && m.record(m.alloc_at(b)).whi == 4);
}

// A million random calls from programs and idle runs, with the invariant check
// every thousand (build-plan-gate1.md step 3's "done when").
void test_random_calls() {
    const MemDeviceSpec devs[] = {{16384, 8 * 16, 950}, {8192, 4 * 16, 1000}, {8192, 12 * 16, 900}};
    Memory m;
    CHECK(make(m, 64, false, 2, 1024, devs, 3, 512));
    Rng r(2026, 0, 1, 0);
    Array<u32> live;
    u32 checks_ok = 0, checks = 0, allocs = 0, frees = 0, protects = 0;
    for (u32 op = 0; op < 1000000; ++op) {
        const u32 kind = static_cast<u32>(r.below(100));
        const u32 holder = r.below(4) == 0 ? 1 + static_cast<u32>(r.below(4)) // an idle run
                                           : kFirstProgramHolder + static_cast<u32>(r.below(40));
        if (kind < 50) {
            const u32 size = r.below(8) == 0 ? 1 + static_cast<u32>(r.below(1100)) : 1 + static_cast<u32>(r.below(64));
            const i32 dev = static_cast<i32>(r.below(9)) - 4;
            const u32 a = m.alloc(holder, size, static_cast<u32>(r.below(3)), dev);
            if (a) {
                live.push(a);
                ++allocs;
            }
        } else if (kind < 85 && live.size()) {
            const usize i = static_cast<usize>(r.below(live.size()));
            const u32 a = live[i];
            const u32 id = m.alloc_at(a);
            if (id && m.record(id).base == a && m.free(m.record(id).holder, a))
                ++frees;
            live[i] = live.back();
            live.pop();
        } else if (kind < 93 && live.size()) {
            const u32 a = live[static_cast<usize>(r.below(live.size()))];
            const u32 id = m.alloc_at(a);
            if (id && m.protect(m.record(id).holder, a, static_cast<u32>(r.below(3))))
                ++protects;
        } else if (kind < 98 && live.size()) {
            const u32 a = live[static_cast<usize>(r.below(live.size()))];
            const u32 id = m.alloc_at(a);
            if (id && m.record(id).base == a)
                m.give(id, kFirstProgramHolder + static_cast<u32>(r.below(40)));
        } else {
            m.free_all(holder);
        }
        if (op % 1000 == 999) {
            ++checks;
            checks_ok += checked(m) ? 1 : 0;
        }
    }
    CHECK(checks == 1000 && checks_ok == checks);
    CHECK(allocs > 100000 && frees > 50000 && protects > 1000);
    printf("  memory: %u allocs, %u frees, %u protects; %u checks passed\n", allocs, frees, protects, checks_ok);
}

} // namespace

void suite_memory() {
    test_layout();
    test_constraints();
    test_slabs();
    test_access();
    test_spans();
    test_devices();
    test_protect();
    test_dirty_and_free();
    test_gifts();
    test_written_extent();
    test_random_calls();
}
