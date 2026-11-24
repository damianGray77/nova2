// The record stream (nova2/record.h; build-plan-observe.md step 2): rows
// in and read back, in memory and in a file; the flush on its clock, when
// the buffer is full and when asked; a file cut back when carried on; and
// the same file whatever the flush timing.
#include <cstring>

#include "check.h"
#include "nova2/io.h"
#include "nova2/record.h"

using namespace nova2;

namespace {

struct Row {
    u64 a;
    u32 b, c;
};

Row row_of(u64 i) { return Row{i * 0x9E3779B97F4A7C15ull, static_cast<u32>(i), static_cast<u32>(i * 7 + 1)}; }

bool same(const Row& x, const Row& y) { return x.a == y.a && x.b == y.b && x.c == y.c; }

// Every row 0..n-1 reads back as written.
bool reads_back(const RecordStream& s, u64 n) {
    if (s.rows() != n)
        return false;
    for (u64 i = 0; i < n; ++i) {
        Row r;
        if (!s.get_row(i, &r) || !same(r, row_of(i)))
            return false;
    }
    Row r;
    return !s.get_row(n, &r);
}

void test_memory() {
    RecordStream s;
    s.open_memory(sizeof(Row));
    for (u64 i = 0; i < 1000; ++i)
        s.add_row(row_of(i));
    CHECK(reads_back(s, 1000) && !s.in_file() && s.rows_written() == 0 && s.ok());
    CHECK(s.flush()); // nothing to write
    CHECK(reads_back(s, 1000));
    s.open_memory(sizeof(Row)); // starts afresh
    CHECK(s.rows() == 0 && s.close());
}

void test_file() {
    const char* path = "test_record_a.bin";
    RecordStream s;
    CHECK(s.open_file(path, sizeof(Row), 0, 1e9)); // the clock never flushes
    for (u64 i = 0; i < 700; ++i) {
        s.add_row(row_of(i));
        if (i == 299)
            CHECK(s.flush()); // a save, say
        s.tick();
    }
    // Rows 0..299 in the file, the rest in the buffer: all read back.
    CHECK(s.in_file() && s.rows_written() == 300 && reads_back(s, 700));
    CHECK(s.close());
    Array<u8> bytes;
    CHECK(read_file(path, &bytes) && bytes.size() == 700 * sizeof(Row));

    // Carried on from a save that counted 450 rows: the rest are cut off,
    // and what is added after follows on.
    RecordStream c;
    CHECK(c.open_file(path, sizeof(Row), 450, 1e9));
    CHECK(reads_back(c, 450) && c.rows_written() == 450);
    for (u64 i = 450; i < 900; ++i)
        c.add_row(row_of(i));
    CHECK(reads_back(c, 900) && c.close());

    // Written in one go, never stopped: the same bytes.
    const char* whole = "test_record_b.bin";
    RecordStream w;
    CHECK(w.open_file(whole, sizeof(Row), 0, 1e9));
    for (u64 i = 0; i < 900; ++i)
        w.add_row(row_of(i));
    CHECK(w.close());
    Array<u8> a, b;
    CHECK(read_file(path, &a) && read_file(whole, &b) && a.size() == b.size() &&
          memcmp(a.data(), b.data(), a.size()) == 0);

    // Flushed at every tick (a clock of 0 seconds): the same bytes again.
    RecordStream e;
    CHECK(e.open_file(whole, sizeof(Row), 0, 0));
    for (u64 i = 0; i < 900; ++i) {
        e.add_row(row_of(i));
        e.tick();
        CHECK(e.rows_written() == i + 1);
    }
    CHECK(e.close());
    CHECK(read_file(whole, &b) && a.size() == b.size() && memcmp(a.data(), b.data(), a.size()) == 0);

    // A save that counted more rows than the file holds can't be carried on.
    RecordStream x;
    CHECK(!x.open_file(path, sizeof(Row), 901, 1e9) && !x.in_file());
    CHECK(!x.open_file("no_such_folder/x.bin", sizeof(Row), 0, 1e9));
}

void test_full_buffer() {
    // A buffer that reaches kFlushBytes is written out without being asked.
    RecordStream s;
    CHECK(s.open_file("test_record_c.bin", sizeof(Row), 0, 1e9));
    const u64 n = RecordStream::kFlushBytes / sizeof(Row) + 10;
    for (u64 i = 0; i < n; ++i)
        s.add_row(row_of(i));
    CHECK(s.rows_written() > 0 && s.rows_written() < n);
    CHECK(s.bytes() <= RecordStream::kFlushBytes * 2); // the buffer stays bounded
    CHECK(reads_back(s, n) && s.close());
}

void test_typed() {
    // Records<T>, in memory and in a file: rows pushed come back whole (a
    // push once stored the row's address in place of the row).
    for (u32 in_file = 0; in_file < 2; ++in_file) {
        Records<Row> r;
        if (in_file)
            CHECK(r.stream().open_file("test_record_d.bin", sizeof(Row), 0, 1e9));
        for (u64 i = 0; i < 3000; ++i) {
            r.push(row_of(i));
            if (i == 1000)
                r.stream().flush();
        }
        bool all = r.size() == 3000 && same(r.back(), row_of(2999));
        for (u64 i = 0; i < 3000; ++i)
            all = all && same(r[i], row_of(i));
        CHECK(all);
        r.clear();
        CHECK(r.empty() && !r.stream().in_file());
    }
}

} // namespace

void suite_record() {
    test_memory();
    test_file();
    test_full_buffer();
    test_typed();
}
