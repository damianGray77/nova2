#pragma once

// Every purpose a random stream is drawn for (rng.h). A stream is keyed by
// (seed, place, purpose, tick), so two jobs never share numbers, and adding a
// draw to one job never moves another's. Each purpose is listed once, here.
#include "nova2/types.h"

namespace nova2 {

inline constexpr u32 kPurposeStatic = 1;         // memory and disk's first bytes (10.1)
inline constexpr u32 kPurposeHardware = 2;       // a place's devices, drawn from the catalogue (7.3)
inline constexpr u32 kPurposeOs = 3;             // a place's OS values: page size, strict or lax (9.6)
inline constexpr u32 kPurposeSupply = 4;         // a place's supply: its day's phase (4.3)
// A swing's levels, keyed by the level's index in place of a tick (4.3).
inline constexpr u32 kPurposeSwing = 5;
inline constexpr u32 kPurposeDropout = 6;        // drop-outs, keyed by the stretch's index in place of a tick (4.3)
inline constexpr u32 kPurposeTear = 7;           // where a disk's cut-out cuts the I/O in flight (7.6)
inline constexpr u32 kPurposeNoise = 8;          // a map noise's permutation, keyed by its id in place of a tick (6.2)
// The map: darts, ids, bridges, keyed by the job in place of a tick (6.1).
inline constexpr u32 kPurposeMap = 9;
inline constexpr u32 kPurposeScatter = 10;       // a field's scatter, keyed by the field in place of a tick (7.3)
inline constexpr u32 kPurposeCorrupt = 11;       // messages' flipped bits, world-wide, at each barrier (11.3)
// Gate 4's sources on the map (4.2, 4.3; sources.h), each keyed by the source's number in place of a place:
inline constexpr u32 kPurposeSource = 12;        // what the seed fixes of it: type, landing, size, stiffness, life
inline constexpr u32 kPurposeSourceBirth = 13;   // the gap to the next birth, world-wide, keyed by the birth's number
inline constexpr u32 kPurposeSourceSwing = 14;   // its swings' levels, keyed by the level's index
inline constexpr u32 kPurposeSourceDropout = 15; // its drop-outs, keyed by the stretch's index
inline constexpr u32 kPurposeSourceSwell = 16;
inline constexpr u32 kPurposeBlocks = 18;    // a disk's blocks' endurances, by the disk's number and block (7.4)
inline constexpr u32 kPurposeWornBlock = 19; // the flips of a tick's writes to worn blocks (7.4)
inline constexpr u32 kPurposeVisit = 20;     // a place's visit gaps, keyed by its visit count (7.8)
// A link's draws: by its lower place, keyed by its higher place in place of a tick.
inline constexpr u32 kPurposeLinkLife = 21;  // its endurance, by which link there it is (7.4)
inline constexpr u32 kPurposeCut = 22;       // the gaps between its cuts, by the cut's number (11.6)
// A respawned place (7.8), by its incarnation in place of a tick: its new
// point's draws (and, as a stream's base, the seed its own draws take); the
// scatter of each field's value there, by the field; and a new link's, by
// its places and their incarnations.
inline constexpr u32 kPurposeRespawn = 23;
inline constexpr u32 kPurposeRespawnField = 24;
inline constexpr u32 kPurposeRespawnLink = 25;
// Gate 5's events (events.h): a source's drop-out's ramp and its swell's, by
// the source's number, keyed by the stretch's index; a strike, by the place
// and the tick; an outage patch's cut of a link, by its lower place and the
// tick, drawn by its higher place.
inline constexpr u32 kPurposeRampDropout = 26;
inline constexpr u32 kPurposeRampSwell = 27;
inline constexpr u32 kPurposeStrike = 28;
inline constexpr u32 kPurposePatchCut = 29;
// Rowhammer's flips from a page, keyed by the tick and the page (9.8).
inline constexpr u32 kPurposeHammer = 30;
// A strait: whether it parts, and its spells, by its two landmasses (11.6).
inline constexpr u32 kPurposeStrait = 31;
// The replication current (current.h; 12.6): a place's block at a tick
// (whether it copies one, from where, down which link, and where it
// lands); and, world-wide, a season's knot, keyed by its number.
inline constexpr u32 kPurposeCurrent = 32;
inline constexpr u32 kPurposeSeason = 33;
// The out-of-memory killer's draw at a place, by the tick (9.1).
inline constexpr u32 kPurposeKiller = 34;
// A place's limit on the threads one program may have, drawn from
// os.thread_limits (9.6; design-thread-limit-2026-11-25.md), apart from
// kPurposeOs's page size and strict or lax, so that adding it moves neither.
inline constexpr u32 kPurposeThreadLimit = 35;
// + a device's slot (wear.h): whether it is flaky, and its factor, keyed by
// how many times it has been replaced (7.5).
inline constexpr u32 kPurposeFlaky = 0xA00;
inline constexpr u32 kPurposeCooler = 17; // which places have a cooling device, with no field to say (7.7)
inline constexpr u32 kPurposeErrors = 0x100;     // + core: the error floor's countdown (7.5)
inline constexpr u32 kPurposeCosmic = 0x200;     // + memory device: the cosmic-ray floor (7.5)
inline constexpr u32 kPurposeMemoryEdge = 0x300; // + memory device: its edge errors, bits it fails to hold (7.5)
// + memory device: fading, its leak ranks' Feistel keys and the values its
// failed cells read back (appendix C)
inline constexpr u32 kPurposeFade = 0x400;
// Gate 5 (wear.h): + the device's slot at its place, its life as drawn, by how often it has been replaced (7.4)
inline constexpr u32 kPurposeWear = 0x500;
// + memory device: its weak cells' lost bits (7.4)
inline constexpr u32 kPurposeWeak = 0x600;
// + a device's slot (wear.h), keyed by its number at that slot: what a
// replaced device draws anew, and a new memory device's or disk's static (7.8).
inline constexpr u32 kPurposeFresh = 0x700;
// + a patch type (events.h), world-wide, keyed by the stretch's index: the
// patches that form in it; and each one's ramp, keyed by the stretch and
// its slot.
inline constexpr u32 kPurposePatch = 0x800;
inline constexpr u32 kPurposePatchRamp = 0x900;

} // namespace nova2
