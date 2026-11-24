#include "nova2/io.h"

#include <cstring> // memcpy

#include "nova2/hash.h"

namespace nova2 {

void ByteWriter::put_u32(u32 v) {
    u8 b[4];
    for (int i = 0; i < 4; ++i)
        b[i] = static_cast<u8>(v >> (8 * i));
    buf_.append(b, 4);
    if (drain_ && buf_.size() >= drain_at_)
        spill();
}

void ByteWriter::put_u64(u64 v) {
    u8 b[8];
    for (int i = 0; i < 8; ++i)
        b[i] = static_cast<u8>(v >> (8 * i));
    buf_.append(b, 8);
    if (drain_ && buf_.size() >= drain_at_)
        spill();
}

void ByteWriter::spill() {
    const usize whole = buf_.size() & ~static_cast<usize>(7);
    if (!drain_ || whole == 0)
        return;
    drain_(drain_ctx_, buf_.data(), whole);
    const usize odd = buf_.size() - whole;
    u8 keep[8];
    memcpy(keep, buf_.data() + whole, odd);
    buf_.clear();
    buf_.append(keep, odd);
}

void ByteWriter::put_str(Str s) {
    NOVA2_ASSERT(s.n <= 0xFFFFFFFFull);
    put_u32(static_cast<u32>(s.n));
    put_bytes(s.p, s.n);
}

bool ByteReader::take(usize n) {
    if (!ok_ || n > n_ - pos_) {
        ok_ = false;
        return false;
    }
    return true;
}

bool ByteReader::get_u8(u8* v) {
    if (!take(1))
        return false;
    *v = p_[pos_++];
    return true;
}

bool ByteReader::get_u32(u32* v) {
    if (!take(4))
        return false;
    u32 r = 0;
    for (int i = 0; i < 4; ++i)
        r |= static_cast<u32>(p_[pos_ + i]) << (8 * i);
    pos_ += 4;
    *v = r;
    return true;
}

bool ByteReader::get_u64(u64* v) {
    if (!take(8))
        return false;
    u64 r = 0;
    for (int i = 0; i < 8; ++i)
        r |= static_cast<u64>(p_[pos_ + i]) << (8 * i);
    pos_ += 8;
    *v = r;
    return true;
}

bool ByteReader::get_bytes(void* out, usize n) {
    if (!take(n))
        return false;
    if (n)
        memcpy(out, p_ + pos_, n);
    pos_ += n;
    return true;
}

bool ByteReader::get_str(Str* s) {
    u32 len;
    if (!get_u32(&len) || !take(len))
        return false;
    *s = Str(reinterpret_cast<const char*>(p_ + pos_), len);
    pos_ += len;
    return true;
}

bool read_file(const char* path, Array<u8>* out) {
    FILE* f = fopen(path, "rb");
    if (!f)
        return false;
    out->clear();
    u8 chunk[65536];
    for (;;) {
        usize got = fread(chunk, 1, sizeof chunk, f);
        out->append(chunk, got);
        if (got < sizeof chunk)
            break;
    }
    bool ok = ferror(f) == 0;
    fclose(f);
    return ok;
}

bool write_file(const char* path, const void* data, usize n) {
    FILE* f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = fwrite(data, 1, n, f) == n;
    ok = (fclose(f) == 0) && ok;
    return ok;
}

// FileOut and FileIn hash block by block, each block's hash seeded with the
// hash so far: the reader asks for the blocks the writer put, so the two
// agree without the file being in memory at once.
FileOut::~FileOut() {
    if (f_)
        fclose(f_);
}

bool FileOut::open(const char* path) {
    f_ = fopen(path, "wb");
    ok_ = f_ != nullptr;
    h_ = n_ = 0;
    return ok_;
}

void FileOut::put(const void* p, usize n) {
    if (!ok_ || n == 0)
        return;
    ok_ = fwrite(p, 1, n, f_) == n;
    h_ = hash_bytes(p, n, h_);
    n_ += n;
}

bool FileOut::close() {
    if (!f_)
        return false;
    const u64 h = h_;
    ok_ = ok_ && fwrite(&h, 1, 8, f_) == 8;
    ok_ = fclose(f_) == 0 && ok_;
    f_ = nullptr;
    return ok_;
}

FileIn::~FileIn() {
    if (f_)
        fclose(f_);
}

bool FileIn::open(const char* path) {
    f_ = fopen(path, "rb");
    h_ = at_ = size_ = 0;
    ok_ = f_ != nullptr;
    if (!ok_)
        return false;
    _fseeki64(f_, 0, SEEK_END);
    const long long all = _ftelli64(f_);
    _fseeki64(f_, 0, SEEK_SET);
    ok_ = all >= 8;
    size_ = ok_ ? static_cast<u64>(all) - 8 : 0;
    return ok_;
}

bool FileIn::get(void* p, usize n) {
    if (!ok_ || n > left()) {
        ok_ = false;
        return false;
    }
    if (n == 0)
        return true;
    ok_ = fread(p, 1, n, f_) == n;
    h_ = hash_bytes(p, n, h_);
    at_ += n;
    return ok_;
}

bool FileIn::close() {
    if (!f_)
        return false;
    u64 h = 0;
    const bool whole = ok_ && at_ == size_ && fread(&h, 1, 8, f_) == 8 && h == h_;
    fclose(f_);
    f_ = nullptr;
    ok_ = whole;
    return whole;
}

bool TextFile::open(const char* path) {
    close();
    f_ = fopen(path, "wb");
    return f_ != nullptr;
}

void TextFile::write(Str s) {
    if (f_ && s.n)
        fwrite(s.p, 1, s.n, f_);
}

void TextFile::flush() {
    if (f_)
        fflush(f_);
}

void TextFile::close() {
    if (f_) {
        fclose(f_);
        f_ = nullptr;
    }
}

} // namespace nova2
