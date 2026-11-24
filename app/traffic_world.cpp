// Gate 3's traffic (build-plan-gate3.md step 5): traffic_world.h.
#include "traffic_world.h"

#include <cstring>

namespace nova2 {

namespace {

// A.5's 14-byte replicator on a lax place: ALLOC 14, copy itself there,
// SPAWN it, again (tests/test_os.cpp traces it).
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};

} // namespace

HandCode& HandCode::bytes(const u8* p, u32 len) {
    memcpy(b + n, p, len);
    n += len;
    return *this;
}

HandCode colonist_sender(u32 ping) {
    HandCode c;
    c.bytes(kLax, 14);
    c.addr(kC, 0).set(kD, 14).set(kB, 0);
    const u32 loop = c.n;
    c.imm(kMov, kTick).sys(kSense).sys(kSend);
    c.imm(kMov, kNeighbourIdleCores).sys(kSense);
    c.imm(kMov, static_cast<i32>(ping & 127)).sys(kPing);
    c.back(loop);
    return c;
}

HandCode talker() {
    HandCode c;
    const u32 loop = c.n;
    c.addr(kC, 100).set(kD, 20).set(kB, 5).imm(kMov, kTick).sys(kSense).sys(kSend);
    c.addr(kB, 120).set(kC, 40).set(kD, 2).imm(kMov, 6).sys(kRecv);
    c.back(loop);
    return c;
}

// Its handler counts itself at [100], and RECV-polls port 5 into 104 until
// there's nothing, counting each at [101] and answering each; then HRET.
HandCode listener() {
    HandCode c;
    c.addr(kD, 100).op(kMov, kAtD).imm(kAdd, 1).op(kSto, kAtD);
    const u32 drain = c.n;
    c.addr(kB, 104).set(kC, 40).set(kD, 0).imm(kMov, 5).sys(kRecv);
    const u32 jez = c.n;
    c.imm(kJez, 0);
    c.addr(kD, 101).op(kMov, kAtD).imm(kAdd, 1).op(kSto, kAtD);
    c.addr(kC, 104).set(kD, 8).set(kB, 6).imm(kMov, kTick).sys(kSense).sys(kSend);
    c.back(drain);
    c.b[jez + 1] = static_cast<u8>(c.n - jez);
    c.sys(kHret);
    c.bad = c.bad || c.n != kListenerEntry;
    c.addr(kB, 0).set(kC, 5).imm(kMov, kOnData).sys(kSetHandler);
    const u32 wait = c.n;
    c.imm(kMov, 0).sys(kSleep);
    c.back(wait);
    return c;
}

// SENDs the 24 bytes from its spot (itself, and what follows), waits on its
// core while they go, and faults: code that only ever sends itself on.
HandCode traveller() {
    HandCode c;
    c.set(kD, 24).set(kB, 0).imm(kMov, kTick).sys(kSense).sys(kSend).op(kPush, kA);
    return c;
}

u32 place_hand_code(Place& p, const HandCode& c, u32 entry) {
    const u32 at = p.mem.alloc(1, 256, p.mem.strict() ? 1 : 2, 0);
    if (!at)
        return ~0u;
    memset(p.mem.bytes() + at, 0, 256);
    memcpy(p.mem.bytes() + at, c.b, c.n);
    for (u32 i = 0; i < 20; ++i)
        p.mem.bytes()[at + 100 + i] = static_cast<u8>(i * 13 + 5); // the talker's message
    const u32 id = p.os.place_by_hand(1, at, at + entry, 0);
    if (!id)
        return ~0u;
    for (u32 t = 0; t < p.os.thread_capacity(); ++t)
        if (p.os.thread(t).state != ThreadState::free && p.os.thread(t).id == id)
            return t;
    return ~0u;
}

HandCode migrant(MigrantKind k, bool any_device) {
    HandCode c;
    c.imm(kSub, 1); // a thread's A is 1: on to the loop
    const u32 to_loop = c.n;
    c.imm(kJez, 0);
    // A prompt run: a page of its own, less a byte, as data (B = 0), from the
    // device its landing spot names (C) or from any (C = 0).
    c.imm(kMov, kPageSize).sys(kSense).imm(kSub, 1).op(kSto, kD).set(kB, 0);
    if (any_device)
        c.op(kSto, kC);
    c.op(kMov, kD).sys(kAlloc);
    const u32 to_end = c.n;
    c.imm(kJez, 0); // no room: end
    if (any_device)
        c.addr(kB, 0); // from its start, found from IP
    else
        c.op(kMov, kC).op(kSto, kB); // from its landing spot
    const u32 copy = c.n;
    c.imm(kCpy, 0); // its length, filled in below
    c.set(kB, 1).op(kMov, kD).sys(kProtect).op(kMov, kD).sys(kSpawn); // executable; the starting value 1
    c.b[to_end + 1] = static_cast<u8>(c.n - to_end);
    c.op(kPush, kA); // an idle run has no stack: this ends it
    const u32 loop = c.n;
    c.b[to_loop + 1] = static_cast<u8>(loop - to_loop);
    if (k != MigrantKind::blind) {
        c.imm(kMov, kTick).sys(kSense).op(kSto, kB); // B: which neighbour
        if (k == MigrantKind::sense) {
            c.imm(kMov, kNeighbourIdleCores).sys(kSense);
            c.imm(kJez, static_cast<i32>(loop) - static_cast<i32>(c.n)); // none idle: another
        }
        c.imm(kMov, kNeighbourId).sys(kSense);
    }
    // SEND itself (C is its start) to port 0 of place A.
    c.op(kPush, kA).set(kB, 0);
    const u32 len = c.n;
    c.set(kD, 0); // its length, filled in below
    c.op(kPop, kA).sys(kSend).back(loop);
    c.b[copy + 1] = static_cast<u8>(c.n);
    c.b[len + 1] = static_cast<u8>(c.n);
    c.bad = c.bad || c.n > 63;
    return c;
}

u64 place_migrant(Place& p, const HandCode& c) {
    const u32 at = p.mem.alloc(1, 256, p.mem.strict() ? 1 : 2, 0);
    if (!at)
        return 0;
    memset(p.mem.bytes() + at, 0, 256);
    memcpy(p.mem.bytes() + at, c.b, c.n);
    const u32 id = p.os.place_by_hand(1, at, at, 1);
    if (!id)
        return 0;
    for (u32 t = 0; t < p.os.thread_capacity(); ++t)
        if (p.os.thread(t).state != ThreadState::free && p.os.thread(t).id == id)
            return p.os.program(p.os.thread(t).program).uid;
    return 0;
}

u32 place_traffic(Place& p, u32 place_count, u32 index) {
    u32 placed = 0;
    placed += place_hand_code(p, colonist_sender((index + place_count / 2) % place_count), kColonistEntry) != ~0u;
    placed += place_hand_code(p, talker()) != ~0u;
    placed += place_hand_code(p, listener(), kListenerEntry) != ~0u;
    const HandCode tv = traveller();
    memcpy(p.mem.bytes() + p.mem.region_base() + p.mem.region_size() / 2, tv.b, tv.n);
    return placed;
}

} // namespace nova2
