#pragma once

// Versioned binary snapshots (principle 20; build-plan-gate1.md step 9). The
// container is Nova 1's, made to carry any payload.
//
// Format v2, all integers little-endian:
//   "NOVA2SNP"   8-byte magic
//   u32          format version (kSnapshotVersion)
//   u32          flags: 0, or kSnapshotPlace for a place snapshot
//   u64          seed       } so a file can be identified without parsing the rest
//   u64          tick       }
//   str          config text: the settings the world was built from
//   ...          world section (World::save: the places, then the network),
//                or for a place snapshot the place's index (u32) and its
//                section (World::save_place)
//   u64          checksum: hash of every preceding byte
//
// Snapshots are taken at the barrier. Random streams are counter-based and
// keyed by the tick, so no RNG state is stored.
//
// Version 4 is gate 4's (build-plan-gate4.md step 8): with supply.model =
// sources a world's section ends with the solve's voltages, and with that
// or heat on each place's ends with its temperatures, its devices' trips
// and its cooling device (place.h). A world with neither saves version 3's
// bytes, so a version 3 file is still read: it was saved before those two
// settings were, when every place had its own supply, nothing had a
// temperature and nothing raised the low-voltage interrupt, and
// snapshot_config fills those in so. Nothing older is read (design.md 2:
// no backward compatibility in beta).
//
// Version 5 (2026-11-20) holds only what is in use (serialize.cpp): of a
// place's memory, the allocation records in use and the order of the free
// ones, with the granule map, the holders' lists and the bitmaps worked out
// again on loading; of its OS, the programs alive, the threads not free and
// the ready queues' entries; and of its disk, the pieces that are no longer
// the static it began as. Versions 3 and 4, which hold every table whole,
// are still read. The world hash is taken over the same bytes as a save
// (Place::hash), so it too covers what is in use: the state that decides
// what the world does next.
//
// Version 6 is gate 5's (build-plan-gate5.md): with wear on, each place's
// section ends with its devices' wear, lives and failed states and its weak
// cells' countdowns (place.h). A world with every gate 5 strength off saves
// version 5's bytes, so a version 5 file is still read: it was saved before
// gate 5's settings were, and snapshot_config turns each of them off for it
// (gate5_off).
#include "nova2/archive.h"
#include "nova2/config.h"
#include "nova2/hash.h"
#include "nova2/io.h"
#include "nova2/str.h"
#include "nova2/types.h"

namespace nova2 {

// 2: gate 2 adds each place's voltage and last draw; 3: gate 3's network;
// 4: gate 4's power and heat; 5: only what is in use; 6: gate 5's wear.
inline constexpr u32 kSnapshotVersion = kArchiveVersion; // archive.h
inline constexpr u32 kSnapshotOldest = 3; // the oldest still read
// A place snapshot: one place of a world, for tracing a lineage where it
// lived (nova2 trace --snapshot; build-plan-gate1.md 11). Not resumable.
inline constexpr u32 kSnapshotPlace = 1;

// Writes the header and config; the caller writes the world section next
// (for a place snapshot, the place's index then its section), then
// snapshot_finish adds the checksum.
void snapshot_begin(u64 seed, u64 tick, Str config_text, ByteWriter& out, u32 flags = 0);
void snapshot_finish(ByteWriter& out);

// A snapshot written to a file as it is made, for a world too large to build
// in memory first (200 places of 4 MB each save as about 8 GB): open, then
// snapshot_begin on writer(), the sections as ever, then close, which adds
// the checksum. The file is byte for byte the one the same snapshot built
// whole and written would be. Not snapshot_finish: close does its work.
class SnapshotFile {
  public:
    SnapshotFile() = default;
    ~SnapshotFile();
    SnapshotFile(const SnapshotFile&) = delete;
    SnapshotFile& operator=(const SnapshotFile&) = delete;

    // `chunk`: the bytes it holds before writing them out.
    bool open(const char* path, usize chunk = 8u << 20);
    ByteWriter& writer() { return out_; }
    // False if the file could not be written whole.
    bool close();
    u64 bytes() const { return bytes_; } // the file's length, once closed

  private:
    static void drain(void* ctx, const u8* p, usize n);
    ByteWriter out_;
    FILE* f_ = nullptr;
    Hasher h_;
    u64 bytes_ = 0;
    bool ok_ = true;
};

struct SnapshotHeader {
    u32 version = 0;
    u32 flags = 0;          // 0, or kSnapshotPlace
    u32 place = 0;          // a place snapshot's place
    u64 seed = 0;
    u64 tick = 0;
    Str config_text;        // a view into the snapshot's bytes
    usize world_offset = 0; // the world (or place) section: [world_offset, world_offset + world_size)
    usize world_size = 0;
};

// Checks the magic, version, structure and checksum. Builds nothing.
bool snapshot_parse(const u8* data, usize n, SnapshotHeader* h, TextBuf* err);

// The settings a snapshot's world was built from, into *cfg: its text, and
// where it doesn't say, what its build took them to be. For a version 3
// file supply.model = place, heat.on = false and power.low_voltage_margin
// = 0; for versions 3 and 4, gate 4's builds, the seven numbers agreed for
// gate 4 at the placeholders those builds had (the heatsinks, the cores'
// heat capacities, the sources' sizes and the link resistance); and for
// one older than version 6 every gate 5 strength off. False, with the line
// and the reason, if the text can't be read.
bool snapshot_config(const SnapshotHeader& h, Config* cfg, Config::ParseError* pe);

// Turns every gate 5 strength off in *cfg (build-plan-gate5.md 3): the base
// rate of wear, and the rest as their steps add them. With all of them off
// the engine gives gate 4's world exactly. `whatever`: even where *cfg sets
// one; otherwise only where it doesn't say.
void gate5_off(Config* cfg, bool whatever);

} // namespace nova2
