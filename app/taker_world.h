#pragma once

// 9.4's memory-taker (build-plan-gate2.md step 9): one program, placed by
// hand (R7: a test world), that takes all of its place's free memory and
// fills it with sleeping threads. `nova2 taker` puts one in every place of a
// world under the config's supply; the taker suite tests it.
//
// Its code, 36 bytes: its first thread SPAWNs threads of its own until its
// memory device has no room for another stack, then ALLOCs whatever is left
// on any device, from the largest allocation down a byte at a time, and
// every thread sleeps until an interrupt that never comes (SLEEP 0, as gate
// 1's frozen holds did).
//
// place_taker builds the state that code reaches, by hand at setup, through
// the OS's own SPAWN and ALLOC: the first thread, alone on its core while it
// fills, ends at a P core's first night-time cut-out in most places when it
// runs the fill itself, leaving them part full, and the question is what
// ends a full place, not whether a fill survives the night. The taker suite
// checks that the state is the one the code reaches when it runs on a
// steady supply, byte for byte.
#include "nova2/place.h"

namespace nova2 {

inline constexpr u32 kTakerBytes = 36;
extern const u8 kTaker[kTakerBytes];

struct TakerPlaced {
    u32 code = 0;   // where its code is
    u32 holder = 0; // its program's holder id
    u64 uid = 0;
    u32 device = 0; // the memory device its code and threads are on
    // Once filled: its threads, the bytes it holds, the bytes left free.
    u32 threads = 0, held = 0, free = 0;
};

// The taker's program alone, its first thread at the start of its code with
// A = 0, ready to do the fill itself. Its code goes in an allocation on the
// fastest memory device with room (ALLOC's "any"). False if it can't be
// placed (core 0 must be free).
bool place_taker_code(Place& pl, TakerPlaced* out);
// The taker, filled by hand: place_taker_code, then the SPAWNs and ALLOCs its
// code makes, and every thread asleep with the registers its code leaves.
bool place_taker(Place& pl, TakerPlaced* out);
// The bytes a holder holds: its allocations' reserved extents.
u32 held_by(const Memory& m, u32 holder);

} // namespace nova2
