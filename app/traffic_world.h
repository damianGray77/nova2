#pragma once

// Gate 3's traffic (build-plan-gate3.md step 5): hand-placed programs (R7: a
// test world) that keep a map's network busy, for the fuzz and determinism
// worlds and the benchmark. place_traffic gives a place:
//   a colonist sender: A.5's 14-byte replicator, then a thread that, again
//     and again, SENDs that replicator to the port 0 of place (the tick mod
//     the places), SENSEs its first neighbour's idle cores (a round trip)
//     and PINGs a place across the map;
//   a talker: SENDs 20 bytes to port 5 of place (the tick mod the places),
//     then waits up to 2 ticks for a message on port 6;
//   a listener: a data handler for port 5 that drains it, answering each
//     message with 8 bytes to port 6 of place (the tick mod the places);
//   a traveller in its reset region: an idle run of it SENDs its spot on and
//     faults (10.4).
#include "nova2/place.h"

namespace nova2 {

// A program's bytes, an instruction at a time.
struct HandCode {
    u8 b[256] = {};
    u32 n = 0;
    bool bad = false; // an addr() that a relative offset can't reach

    HandCode& op(u8 o, u8 od) {
        b[n++] = encode(o, od);
        return *this;
    }
    HandCode& imm(u8 o, i32 v) {
        b[n++] = encode(o, kImm);
        b[n++] = static_cast<u8>(v);
        return *this;
    }
    HandCode& sys(u8 call) { return imm(kSys, call); }
    // Register r (not A) = v, through A.
    HandCode& set(u8 r, i32 v) { return imm(kMov, v).op(kSto, r); }
    // Register r = the address `at` bytes into the program's allocation,
    // from the instruction's own address.
    HandCode& addr(u8 r, u32 at) {
        const i32 off = static_cast<i32>(at) - static_cast<i32>(n);
        bad = bad || off < -128 || off > 127;
        return op(kMov, kIp).imm(kAdd, off).op(kSto, r);
    }
    // A jump back to `to`.
    HandCode& back(u32 to) { return imm(kJmp, static_cast<i32>(to) - static_cast<i32>(n)); }
    HandCode& bytes(const u8* p, u32 len);
};

// The programs. A colonist sender's thread starts kColonistEntry bytes in, a
// listener's kListenerEntry; a colonist PINGs place `ping`.
inline constexpr u32 kColonistEntry = 14;
inline constexpr u32 kListenerEntry = 52;
HandCode colonist_sender(u32 ping);
HandCode talker();
HandCode listener();
HandCode traveller();

// Places code in a fresh 256-byte allocation, its thread starting `entry`
// bytes in, with the talker's 20-byte message at 100: the thread's record
// index, or ~0 if there's no room.
u32 place_hand_code(Place& p, const HandCode& c, u32 entry = 0);
// Place `index` of `place_count`'s share of the traffic. The number of
// programs placed, of three (the traveller is always placed).
u32 place_traffic(Place& p, u32 place_count, u32 index);

// Gate 3's migrants (build-plan-gate3.md step 7; R7: test worlds at full
// size). One program's code, run two ways:
//   - arriving at a port 0, as a prompt run, it copies itself into a page of
//     its own (an ALLOC of a page less a byte, as data; CPY; PROTECT it
//     executable, so strict and lax places both run it), SPAWNs the copy with
//     the starting value 1, and ends (an idle run can't PUSH): one copy an
//     arrival, nothing multiplied at the place;
//   - as a thread (A = 1 at its start), it SENDs itself to port 0s, again and
//     again:
//     - sense: SENSE a neighbour (the tick picks which), and if it has an
//       idle core, SENSE its id and SEND there;
//     - blind: SEND to whatever A holds after its last call: 0 at first, then
//       1 after a SEND that worked and 0 after one that failed. 11.1's
//       prediction for a SEND made by mutation;
//     - pushy: SENSE a neighbour's id and SEND there, idle cores or not (the
//       CPU-saturation world's, so arrivals reach busy places).
// Its ALLOC asks with C, its landing spot: an address, so the slowest device
// (A.5). With any_device (worlds.migrant_device = any; asked after step 7)
// it asks for any device (C = 0) and finds its start from IP instead, three
// bytes more. Each is under 64 bytes either way, so it fits the smallest
// page less a byte.
enum class MigrantKind : u8 { sense, blind, pushy };
HandCode migrant(MigrantKind k, bool any_device = false);
// A migrant placed by hand as a program in a fresh allocation, its thread at
// the start with A = 1, so it starts sending. Its program's uid, or 0.
u64 place_migrant(Place& p, const HandCode& c);

} // namespace nova2
