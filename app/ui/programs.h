#pragma once

// The programs of a place, and one program's code and threads
// (build-plan-viewer.md step 2; design.md 13.4). What the views show is
// worked out here, apart from its drawing, so the tests can read it:
//   - the place's living programs, each with its lineage, threads, bytes
//     held, birth and cause, sorted by any of them;
//   - a program's code: the genome it was born with (the observers', 13.2),
//     an instruction a line, each line with the threads standing at it and
//     whether its bytes in memory have changed since the birth. A program
//     the observer has no record of (one born before it began to watch)
//     shows the bytes of the allocation it was born in as they lie now;
//   - its threads: state, core, instruction pointer and registers.
// Nothing here changes the world.
#include "canvas.h"
#include "nova2/array.h"
#include "nova2/cpu.h"
#include "nova2/os.h"
#include "nova2/str.h"
#include "nova2/types.h"

namespace nova2 {

class Observer;
class World;
class Place;
struct UiInput;
struct ViewState;

enum class ProgramSort : u8 { age = 0, lineage, threads, bytes, born, count };

// One living program of a place.
struct ProgramRow {
    u64 uid = 0;
    u64 origin = 0; // its lineage's first program (its own uid when the observer has no record of it)
    u64 born_tick = 0;
    u32 holder = 0; // its holder number in the place's memory
    u32 threads = 0;
    u32 bytes = 0; // reserved by its allocations
    BirthCause cause = BirthCause::thread;
    bool known = false; // the observer has its record
};

// The place's living programs, sorted by `by` (ties by uid, the older
// first); `down` turns the order round. Not `whole`: the rows' bytes and
// lineage are left out unless they are what the rows are sorted by (a
// list of thousands needs them for the few rows shown), and finish_row
// adds them.
//
// The walks of a place's programs end once they have met all its living
// ones: the table is sized for the worst case (65,537 records in a place
// of 4 MB with 64-byte stacks) and a record freed is the first reused, so
// the living sit low in it (build-plan-viewer.md step 12).
void list_programs(const Place& p, const Observer* obs, ProgramSort by, bool down, Array<ProgramRow>* out,
                   bool whole = true);
void finish_row(const Place& p, const Observer* obs, ProgramSort by, ProgramRow* row);

// The living program `uid` of the place, or false. `holder`, if given, is
// where it was last found, looked at first.
bool find_program(const Place& p, const Observer* obs, u64 uid, ProgramRow* out, u32 holder = 0);

// One instruction of a program's code.
struct CodeLine {
    u32 at = 0;       // its offset in the code
    u16 threads = 0;  // the program's threads whose instruction pointer is in it
    u8 len = 0;       // 1 or 2 bytes
    bool changed = 0; // its bytes in memory are no longer the code's
};

struct ProgramCode {
    const u8* bytes = nullptr;
    u32 len = 0;
    u32 addr = kNowhere; // where its first byte lies in the place's memory, while that is still the program's
    u64 genome = 0;      // the genome's hash, when the code is the genome
    bool birth = false;  // the genome it was born with; false: bytes as they lie now
};

// The code of program `uid`: its genome, by the observer's record of it or,
// for a program that has died, by `genome` (the hash it had when last
// seen). `row` is the program if it lives (null: it doesn't), for the
// threads' marks and the changed bytes. False: there is nothing to show.
bool program_code(const Place& p, const Observer* obs, u64 uid, u64 genome, const ProgramRow* row,
                  ProgramCode* code, Array<CodeLine>* lines);

// A line's instruction as nova2 trace writes it ("MOV # 14").
void line_text(const ProgramCode& code, const CodeLine& line, TextBuf* out);

// The line holding the code's byte `off`, or ~0u.
u32 line_at(const Array<CodeLine>& lines, u32 off);

// The code as text, what the code pane's "copy" button puts on the
// clipboard: whose it is (`row`, at place `place`, tick `tick`), its bytes
// in hex as nova2 trace reads them, and a line an instruction (its offset,
// its bytes, the instruction; a * where its bytes have changed in memory
// since the birth). ASCII, lines ending in \n.
void code_text(const ProgramCode& code, const Array<CodeLine>& lines, const ProgramRow& row, u32 place, u64 tick,
               TextBuf* out);

// The text the copy button last made, for the window to put on the
// clipboard when the frame asks (ViewState::copy_asked).
const char* copied_text();

// One thread of a program.
struct ThreadRow {
    u32 record = 0; // its record in the place's OS
    u32 id = 0;
    u32 core = 0;   // 1 + the core it is on, or 0
    ThreadState state = ThreadState::free;
    Regs r;
};

// The program's threads, in its own order (the newest first).
void list_threads(const Place& p, const ProgramRow& row, Array<ThreadRow>* out);

// A program's uid as the views write it: the place it was born in, a dot,
// and its number among that place's programs ("3.10": the uid itself is
// (place + 1) * 2^40 + number, 13 digits for any place).
void uid_text(u64 uid, char* out, u32 size);

const char* cause_name(BirthCause c);
const char* sort_label(ProgramSort s); // an order's button
const char* state_name(ThreadState s);

// The inspector's programs page: the list, and for the program picked its
// code and its threads.
void draw_programs(Canvas& c, const Rect& r, ViewState& st, World& w, Place& p, const Observer* obs,
                   const UiInput& in);

// The pages of the place's memory that the program picked holds something
// in, a byte a page (0 or 1); empty when none is picked here or it has died.
void picked_pages(const Place& p, const Observer* obs, const ViewState& st, Array<u8>* out);

} // namespace nova2
