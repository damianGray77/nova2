#pragma once

// Byte-level encoding (explicit little-endian, independent of host layout) and
// whole-file helpers. Snapshots use ByteWriter/ByteReader; CSV and the replay
// log use TextFile.
#include <cstdio> // FILE

#include "nova2/array.h"
#include "nova2/str.h"
#include "nova2/types.h"

namespace nova2 {

class ByteWriter {
  public:
    void put_u8(u8 v) {
        buf_.push(v);
        if (drain_ && buf_.size() >= drain_at_)
            spill();
    }
    void put_u32(u32 v);
    void put_u64(u64 v);
    void put_bytes(const void* p, usize n) {
        buf_.append(static_cast<const u8*>(p), n);
        if (drain_ && buf_.size() >= drain_at_)
            spill();
    }
    void put_str(Str s); // u32 length + bytes

    const u8* data() const { return buf_.data(); }
    usize size() const { return buf_.size(); }

    // For what is too large to build in memory first (a world of big places):
    // whenever it holds `at_least` bytes or more, `drain` is given them in
    // whole 8-byte words and they are gone from the writer, so data() and
    // size() are only what it still holds. spill() hands over what it holds
    // now, but for the last 0 to 7 bytes.
    typedef void (*Drain)(void* ctx, const u8* p, usize n);
    void drain_to(Drain drain, void* ctx, usize at_least) {
        drain_ = drain;
        drain_ctx_ = ctx;
        drain_at_ = at_least;
    }
    void spill();

  private:
    Array<u8> buf_;
    Drain drain_ = nullptr;
    void* drain_ctx_ = nullptr;
    usize drain_at_ = 0;
};

// Bounds-checked reader. Any overrun makes every later get_* fail too (sticky),
// so callers can read a whole section and check ok() once.
class ByteReader {
  public:
    ByteReader(const u8* data, usize n) : p_(data), n_(n) {}

    bool get_u8(u8* v);
    bool get_u32(u32* v);
    bool get_u64(u64* v);
    bool get_bytes(void* out, usize n);
    bool get_str(Str* s); // view into the underlying buffer

    bool ok() const { return ok_; }
    usize pos() const { return pos_; }
    usize remaining() const { return n_ - pos_; }

  private:
    bool take(usize n);

    const u8* p_;
    usize n_;
    usize pos_ = 0;
    bool ok_ = true;
};

bool read_file(const char* path, Array<u8>* out);
bool write_file(const char* path, const void* data, usize n);

// Append-only text output (binary mode, so "\n" stays LF on Windows).
class TextFile {
  public:
    TextFile() = default;
    ~TextFile() { close(); }
    TextFile(const TextFile&) = delete;
    TextFile& operator=(const TextFile&) = delete;

    bool open(const char* path);
    bool is_open() const { return f_ != nullptr; }
    void write(Str s);
    void flush();
    void close();

  private:
    FILE* f_ = nullptr;
};

// A file written in order, a block or a whole table at a time, with a hash
// of everything put so far at its end: for saves too large to build in
// memory first (the observers' memory, design.md 13.1). Bytes go as they
// lie in memory (little-endian targets only), and a table of records goes
// with its padding: such a file is outside the world's hash, and is read
// back by the build that wrote it (a table's record size is checked).
class FileOut {
  public:
    FileOut() = default;
    ~FileOut();
    FileOut(const FileOut&) = delete;
    FileOut& operator=(const FileOut&) = delete;

    bool open(const char* path);
    void put(const void* p, usize n);
    void put_u64(u64 v) { put(&v, 8); }
    // A table: its length, its records' size, then the records.
    template <class T> void put_table(const Array<T>& a) {
        put_u64(a.size());
        put_u64(sizeof(T));
        put(a.data(), a.size() * sizeof(T));
    }
    // Ends the file with its hash and closes it. False if any write failed.
    bool close();
    bool ok() const { return ok_; }
    u64 bytes() const { return n_; }

  private:
    FILE* f_ = nullptr;
    u64 h_ = 0, n_ = 0;
    bool ok_ = true;
};

// Such a file read back, in the order it was written. Any failure makes
// every later get fail too.
class FileIn {
  public:
    FileIn() = default;
    ~FileIn();
    FileIn(const FileIn&) = delete;
    FileIn& operator=(const FileIn&) = delete;

    bool open(const char* path);
    bool get(void* p, usize n);
    bool get_u64(u64* v) { return get(v, 8); }
    template <class T> bool get_table(Array<T>* a) {
        u64 n = 0, width = 0;
        if (!get_u64(&n) || !get_u64(&width) || width != sizeof(T) || n > left() / sizeof(T)) {
            ok_ = false;
            return false;
        }
        a->clear();
        a->resize_unfilled(static_cast<usize>(n));
        return get(a->data(), static_cast<usize>(n) * sizeof(T));
    }
    // The file's end: true if everything was read and its hash is the one
    // stored. Closes it.
    bool close();
    bool ok() const { return ok_; }
    u64 left() const { return size_ > at_ ? size_ - at_ : 0; } // bytes not yet read, the hash aside

  private:
    FILE* f_ = nullptr;
    u64 h_ = 0, at_ = 0, size_ = 0;
    bool ok_ = true;
};

} // namespace nova2
