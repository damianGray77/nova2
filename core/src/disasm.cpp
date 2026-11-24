// The disassembler (cpu.h): code bytes as the 22 instructions (8, A.1).
#include <cstdio> // snprintf

#include "nova2/cpu.h"

namespace nova2 {

namespace {

const char* const kOpNames[32] = {"MOV", "STO", "SWP", "PUSH", "POP", "ADD", "SUB", "MUL", "DIV", "AND", "OR",
                                  "XOR", "NOT", "SHL", "SHR", "JMP", "JEZ", "JNZ", "JGZ", "JLZ", "CPY", "SYS",
                                  "NOP", "NOP", "NOP", "NOP", "NOP", "NOP", "NOP", "NOP", "NOP", "NOP"};
const char* const kOperandNames[7] = {"A", "B", "C", "D", "IP", "[B]", "[D]"};

} // namespace

void disassemble(const u8* code, u32 len, TextBuf* out) {
    char buf[32];
    u32 i = 0;
    while (i < len) {
        const u8 op = code[i] >> 3, od = code[i] & 7;
        const char* name = kOpNames[op];
        if (i)
            out->append("; ");
        if (od == kImm) {
            if (i + 1 < len)
                snprintf(buf, sizeof buf, "%s # %d", name, static_cast<int>(static_cast<i8>(code[i + 1])));
            else
                snprintf(buf, sizeof buf, "%s # (cut)", name);
            i += 2;
        } else {
            snprintf(buf, sizeof buf, "%s %s", name, kOperandNames[od]);
            i += 1;
        }
        out->append(buf);
    }
}

} // namespace nova2
