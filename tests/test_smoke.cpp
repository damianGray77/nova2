// The skeleton's own check (build-plan-gate1.md step 1): the types are the
// widths the engine relies on, and the helpers behave.
#include "check.h"
#include "nova2/types.h"

using namespace nova2;

void suite_smoke() {
    CHECK(sizeof(u8) == 1);
    CHECK(sizeof(u16) == 2);
    CHECK(sizeof(u32) == 4);
    CHECK(sizeof(u64) == 8);
    CHECK(sizeof(i8) == 1);
    CHECK(sizeof(i16) == 2);
    CHECK(sizeof(i32) == 4);
    CHECK(sizeof(i64) == 8);

    // Byte reads are sign-extended (design.md A.1): the cast the interpreter
    // will use gives -128..127.
    const u8 b = 0xFF;
    CHECK(static_cast<i32>(static_cast<i8>(b)) == -1);
    const u8 c = 0x80;
    CHECK(static_cast<i32>(static_cast<i8>(c)) == -128);

    // "x mod n" reads x as unsigned 32-bit (design.md A.1): -1 mod 18 is 3.
    const i32 minus_one = -1;
    CHECK_EQ_U64(static_cast<u32>(minus_one) % 18u, 3);

    CHECK(min_of(3, 5) == 3);
    CHECK(max_of(3, 5) == 5);
    CHECK(min_of(-2, 1) == -2);
}
