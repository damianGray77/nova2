// The record stream (nova2/record.h; build-plan-observe.md step 2).
#include "nova2/record.h"

#include <cstring>
#include <windows.h> // CreateFileA, ReadFile, WriteFile, SetEndOfFile: files past 4 GB, read while written

#include "nova2/pool.h" // clock_seconds

namespace nova2 {

namespace {

// Reads or writes n bytes at byte `at` of the file, in pieces of at most
// 1 GB (a call takes a 32-bit length).
bool at_offset(HANDLE h, u64 at, void* p, usize n, bool write) {
    u8* b = static_cast<u8*>(p);
    while (n) {
        const DWORD part = n > (1u << 30) ? (1u << 30) : static_cast<DWORD>(n);
        OVERLAPPED o;
        memset(&o, 0, sizeof o);
        o.Offset = static_cast<DWORD>(at);
        o.OffsetHigh = static_cast<DWORD>(at >> 32);
        DWORD done = 0;
        const BOOL r = write ? WriteFile(h, b, part, &done, &o) : ReadFile(h, b, part, &done, &o);
        if (!r || done != part)
            return false;
        b += part;
        at += part;
        n -= part;
    }
    return true;
}

} // namespace

RecordStream::~RecordStream() { close(); }

void RecordStream::open_memory(u32 row_size) {
    close();
    row_ = row_size;
    rows_ = on_disk_ = 0;
    buf_.clear();
    ok_ = row_size > 0;
}

bool RecordStream::open_file(const char* path, u32 row_size, u64 keep, double flush_seconds) {
    close();
    row_ = row_size;
    rows_ = on_disk_ = 0;
    buf_.clear();
    every_ = flush_seconds;
    last_ = clock_seconds();
    ok_ = false;
    if (row_size == 0)
        return false;
    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           keep ? OPEN_EXISTING : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER size;
    LARGE_INTEGER cut;
    cut.QuadPart = static_cast<LONGLONG>(keep * row_size);
    if (!GetFileSizeEx(h, &size) || static_cast<u64>(size.QuadPart) < keep * row_size ||
        !SetFilePointerEx(h, cut, nullptr, FILE_BEGIN) || !SetEndOfFile(h)) {
        CloseHandle(h);
        return false;
    }
    h_ = h;
    rows_ = on_disk_ = keep;
    ok_ = true;
    return true;
}

bool RecordStream::close() {
    bool good = ok_;
    if (h_) {
        good = flush() && good;
        CloseHandle(static_cast<HANDLE>(h_));
        h_ = nullptr;
    }
    buf_.clear();
    cache_.clear();
    cache_first_ = cache_rows_ = 0;
    rows_ = on_disk_ = 0;
    ok_ = true;
    return good;
}

void RecordStream::add(const void* row) {
    buf_.append(static_cast<const u8*>(row), row_);
    ++rows_;
    if (h_ && buf_.size() >= kFlushBytes)
        flush();
}

void RecordStream::add_many(const void* rows, u64 n) {
    buf_.append(static_cast<const u8*>(rows), static_cast<usize>(n * row_));
    rows_ += n;
    if (h_ && buf_.size() >= kFlushBytes)
        flush();
}

bool RecordStream::get(u64 i, void* out) const {
    if (i >= rows_)
        return false;
    if (i >= on_disk_) {
        memcpy(out, buf_.data() + (i - on_disk_) * row_, row_);
        return true;
    }
    if (i < cache_first_ || i >= cache_first_ + cache_rows_) {
        // Read the block of rows i falls in (a row bigger than a block is
        // a block of its own).
        const u64 per = kCacheBytes / row_ ? kCacheBytes / row_ : 1;
        const u64 first = i / per * per;
        const u64 n = first + per <= on_disk_ ? per : on_disk_ - first;
        cache_.resize(static_cast<usize>(n * row_));
        cache_rows_ = 0;
        if (!at_offset(static_cast<HANDLE>(h_), first * row_, cache_.data(), cache_.size(), false))
            return false;
        cache_first_ = first;
        cache_rows_ = n;
    }
    memcpy(out, cache_.data() + (i - cache_first_) * row_, row_);
    return true;
}

bool RecordStream::get_many(u64 first, u64 n, void* out) const {
    if (first > rows_ || n > rows_ - first)
        return false;
    for (u64 i = 0; i < n; ++i)
        if (!get(first + i, static_cast<u8*>(out) + i * row_))
            return false;
    return true;
}

bool RecordStream::flush() {
    last_ = clock_seconds();
    if (!h_ || buf_.size() == 0)
        return ok_;
    if (!at_offset(static_cast<HANDLE>(h_), on_disk_ * row_, buf_.data(), buf_.size(), true))
        ok_ = false;
    on_disk_ = rows_;
    buf_.clear();
    return ok_;
}

void RecordStream::tick() {
    if (h_ && buf_.size() && clock_seconds() - last_ >= every_)
        flush();
}

} // namespace nova2
