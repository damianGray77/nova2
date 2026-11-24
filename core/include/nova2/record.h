#pragma once

// A record stream (design.md 13.1; build-plan-observe.md 3.1, step 2): a
// table of fixed-size rows that is only added to, for the observers'
// records that are finished (samples, windows, kept genomes, finished
// lineages), so a long run doesn't keep its whole history in memory.
//
// - In memory: every row is kept, as the observers' tables were. For
//   tests, and for worlds run without a folder for records.
// - In a file: rows go into a buffer, and the buffer goes to the file
//   every `flush_seconds` on the clock (30, Damian's number), when it
//   reaches kFlushBytes, and whenever the owner asks (at a save). What is
//   written never depends on when: the file holds the rows in order, so
//   it is the same whatever the timing.
// - Rows are read back by number, from the buffer or the file.
// - Carrying on from a save opens the file keeping only the rows the save
//   counted, and cuts off the rest, so the file matches the run as it
//   carries on.
//
// Other programs may read the file while it is written (Windows' share
// mode allows reading). The file is outside the world's hash, and is read
// back by the build that wrote it: rows go as they lie in memory.
#include "nova2/array.h"
#include "nova2/types.h"

namespace nova2 {

class RecordStream {
  public:
    static constexpr usize kFlushBytes = 16u << 20; // the most the buffer holds before it is written

    RecordStream() = default;
    ~RecordStream();
    RecordStream(const RecordStream&) = delete;
    RecordStream& operator=(const RecordStream&) = delete;

    // In memory, rows of `row_size` bytes. Forgets any rows it had.
    void open_memory(u32 row_size);
    // In the file at `path`, rows of `row_size` bytes: a new file when
    // `keep` is 0, else one carried on, keeping its first `keep` rows and
    // cutting off the rest. False if it can't be opened or made, or holds
    // fewer than `keep` rows.
    bool open_file(const char* path, u32 row_size, u64 keep, double flush_seconds = 30);
    // Writes what is buffered and closes the file (in memory: forgets).
    // False if a write failed, now or before.
    bool close();

    void add(const void* row);
    void add_many(const void* rows, u64 n); // n rows at once
    template <class T> void add_row(const T& row) { add(static_cast<const void*>(&row)); }
    // In memory: every row, in order (null in a file).
    const u8* memory_rows() const { return h_ ? nullptr : buf_.data(); }
    u64 rows() const { return rows_; }
    // Row i, into `out` (row_size bytes). False if there is no row i, or
    // the file can't be read.
    bool get(u64 i, void* out) const;
    template <class T> bool get_row(u64 i, T* out) const { return get(i, static_cast<void*>(out)); }
    // Rows first to first + n, one after another, into `out`.
    bool get_many(u64 first, u64 n, void* out) const;

    // Writes the buffered rows to the file now (in memory: nothing to do).
    bool flush();
    // Flushes if `flush_seconds` have passed since the last flush. Its
    // owner calls it as it goes, a tick at a time: it reads the clock and,
    // most calls, nothing else.
    void tick();

    bool ok() const { return ok_; }
    bool in_file() const { return h_ != nullptr; }
    u32 row_size() const { return row_; }
    u64 rows_written() const { return on_disk_; }              // rows in the file (in memory: 0)
    usize bytes() const { return buf_.bytes() + cache_.bytes(); } // the memory it holds

  private:
    static constexpr usize kCacheBytes = 64u << 10; // rows read from the file come a block at a time

    void* h_ = nullptr; // the file's handle, or null in memory
    u32 row_ = 0;
    u64 rows_ = 0;    // rows in all
    u64 on_disk_ = 0; // of those, in the file
    Array<u8> buf_;   // in memory: every row; in a file: the rows not yet written
    double every_ = 30, last_ = 0;
    bool ok_ = true;
    // The block of the file read last: rows [cache_first_, cache_first_ +
    // cache_rows_). Rows in the file never change once written (a carried-on
    // file is cut back only when opened), so it never goes stale.
    mutable Array<u8> cache_;
    mutable u64 cache_first_ = 0, cache_rows_ = 0;
};

// A record stream of one kind of row, read like an array: what the
// observers' tables of finished records became (samples, voltages,
// windows). Rows come back by value, since a row in the file has no place
// in memory to point to.
template <class T> class Records {
  public:
    Records() { s_.open_memory(sizeof(T)); }
    RecordStream& stream() { return s_; }
    const RecordStream& stream() const { return s_; }
    void clear() { s_.open_memory(sizeof(T)); } // in memory, empty
    void push(const T& row) { s_.add_row(row); }
    usize size() const { return static_cast<usize>(s_.rows()); }
    bool empty() const { return s_.rows() == 0; }
    T operator[](u64 i) const {
        T r{};
        s_.get_row(i, &r);
        return r;
    }
    T back() const { return (*this)[s_.rows() - 1]; }
    usize bytes() const { return s_.bytes(); }

  private:
    RecordStream s_;
};

} // namespace nova2
