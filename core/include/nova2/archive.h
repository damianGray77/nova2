#pragma once

// Saving and loading state field by field (snapshots and the world hash,
// build-plan-gate1.md step 9). One function per structure walks its fields
// through an Archive, which either writes them or reads them back, so the
// saved form and the loaded form can't drift apart.
//
// Fields go one by one, never as raw structs, so padding never reaches a
// snapshot or the world hash. Arrays of plain numbers go whole. On load an
// array must already have the size it was saved with: a world is rebuilt
// from its settings first, then its state is read into it.
#include "nova2/array.h"
#include "nova2/io.h"
#include "nova2/types.h"

namespace nova2 {

// The form a save is written in, which is the snapshot's format version
// (snapshot.h): the world hash is taken over the same bytes.
inline constexpr u32 kArchiveVersion = 6;

// Where a save's bytes go, by section (`nova2 sizes`): each Archive::mark
// adds the bytes written since the mark before it to the section named.
struct ArchiveSizes {
    static constexpr u32 kMost = 64;
    const char* name[kMost] = {};
    u64 bytes[kMost] = {};
    u32 n = 0;
    u64 last = 0; // the writer's size at the last mark
    void add(const char* section, u64 b) {
        for (u32 i = 0; i < n; ++i)
            if (name[i] == section) {
                bytes[i] += b;
                return;
            }
        if (n < kMost) {
            name[n] = section;
            bytes[n++] = b;
        }
    }
};

class Archive {
  public:
    // `version`: the snapshot format the bytes are written in or were saved
    // in, for the parts whose form changed (snapshot.h). This build's unless
    // told: an older one is written only by tests of reading it.
    explicit Archive(ByteWriter* w, u32 version = kArchiveVersion) : w_(w), version_(version) {}
    explicit Archive(ByteReader* r, u32 version = kArchiveVersion) : r_(r), version_(version) {}

    bool loading() const { return r_ != nullptr; }
    bool ok() const { return ok_ && (!r_ || r_->ok()); }
    void fail() { ok_ = false; }
    // The form being read or written: kArchiveVersion when saving.
    u32 version() const { return version_; }
    // A place's counters carried over to the place built in its stead at a
    // respawn (7.8; World::respawn): what follows the old place's shape, its
    // cores and what its threads were doing, is left out on both sides.
    bool carry = false;

    // Whether the bytes are in the compact form (snapshot version 5): only
    // what is in use, as the state that decides the world's future.
    bool compact() const { return version_ >= 5; }

    // Counts the bytes written since the last mark as `section`'s, when
    // sizes are asked for (a pointer to a string literal: sections are told
    // apart by it).
    void sizes_to(ArchiveSizes* s) {
        sizes_ = s;
        if (s && w_)
            s->last = w_->size();
    }
    void mark(const char* section) {
        if (sizes_ && w_) {
            sizes_->add(section, w_->size() - sizes_->last);
            sizes_->last = w_->size();
        }
    }

    void u8_(u8& v) {
        if (r_)
            r_->get_u8(&v);
        else
            w_->put_u8(v);
    }
    void u16_(u16& v) {
        u32 t = v;
        u32_(t);
        v = static_cast<u16>(t);
    }
    void u32_(u32& v) {
        if (r_)
            r_->get_u32(&v);
        else
            w_->put_u32(v);
    }
    void u64_(u64& v) {
        if (r_)
            r_->get_u64(&v);
        else
            w_->put_u64(v);
    }
    void i32_(i32& v) {
        u32 t = static_cast<u32>(v);
        u32_(t);
        v = static_cast<i32>(t);
    }
    void i64_(i64& v) {
        u64 t = static_cast<u64>(v);
        u64_(t);
        v = static_cast<i64>(t);
    }
    void bool_(bool& v) {
        u8 t = v ? 1 : 0;
        u8_(t);
        v = t != 0;
    }
    // An enum stored as a byte.
    template <class E> void enum_(E& v) {
        u8 t = static_cast<u8>(v);
        u8_(t);
        v = static_cast<E>(t);
    }

    // A tag that must match on load: catches a section read out of step.
    void tag(u32 want) {
        u32 t = want;
        u32_(t);
        if (t != want)
            ok_ = false;
    }

    // Arrays of plain numbers, whole. The size is written, and on load must match.
    void bytes(Array<u8>& a) { raw(a.data(), a.size(), 1); }
    void words(Array<u32>& a) { raw(a.data(), a.size(), 4); }
    void quads(Array<u64>& a) { raw(a.data(), a.size(), 8); }
    void raw_u8(u8* p, usize n) { raw(p, n, 1); }
    // n bytes and nothing else: for a piece whose length both sides know.
    void plain_u8(u8* p, usize n) {
        if (r_)
            r_->get_bytes(p, n);
        else
            w_->put_bytes(p, n);
    }

  private:
    void raw(void* p, usize n, usize width) {
        u64 count = n;
        u64_(count);
        if (count != n) {
            ok_ = false;
            return;
        }
        // Little-endian targets only (MSVC on x64), so the bytes are the numbers.
        if (r_)
            r_->get_bytes(p, n * width);
        else
            w_->put_bytes(p, n * width);
    }

    ByteWriter* w_ = nullptr;
    ByteReader* r_ = nullptr;
    u32 version_ = kArchiveVersion;
    ArchiveSizes* sizes_ = nullptr;
    bool ok_ = true;
};

} // namespace nova2
