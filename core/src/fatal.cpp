#include <cstdio>  // fprintf, fflush
#include <cstdlib> // _Exit

#include "nova2/types.h"

namespace nova2 {

void fatal(const char* file, int line, const char* msg) {
    fprintf(stderr, "nova2: fatal: %s (%s:%d)\n", msg, file, line);
    fflush(stderr);
    // _Exit rather than abort(): the debug CRT's abort() pops a modal dialog,
    // which would hang an unattended test run instead of failing it.
    _Exit(70);
}

} // namespace nova2
