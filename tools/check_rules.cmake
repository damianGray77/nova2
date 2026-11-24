# The code-rules guard (design.md 2), run as the `rules` CTest entry:
#   cmake -DROOT=<repo> -P tools/check_rules.cmake
#
# From Nova 1's tools/check_includes.cmake, extended. It fails if any source
# under core/, app/ or tests/:
#   - includes a system header that is not on the allowlist below;
#   - uses `std::` in code;
#   - writes a lambda with an empty, [&] or [=] capture (named captures slip
#     through; review catches those);
#   - declares a virtual function in core/ (the engine has none).
# Text after // is ignored. Project headers ("nova2/...", "check.h") are
# always fine. Exceptions and RTTI are refused by the compiler (types.h).
#
# Allowlist: thin C-runtime headers and platform headers only. Adding to it is a
# dependency decision: ask Damian first.
set(allowed
    cstddef   # size_t
    cstdint   # fixed-width integers
    cstdio    # FILE, fopen, fprintf, snprintf
    cstdlib   # malloc/realloc/free, strtod, _Exit
    cstring   # memcpy, memset, memcmp, strlen
    cmath     # floating-point maths, outside the tick
    windows.h # threads, SRWLOCK, CONDITION_VARIABLE
    intrin.h  # _umul128, _Interlocked*
    # The viewer (app/ui), approved for the UI (design.md 2): Direct2D,
    # DirectWrite and WIC, the Windows Imaging Component.
    d2d1.h
    dwrite.h
    wincodec.h
)

if(NOT ROOT)
    message(FATAL_ERROR "check_rules: pass -DROOT=<repo root>")
endif()

file(GLOB_RECURSE sources
    "${ROOT}/core/*.h" "${ROOT}/core/*.cpp" "${ROOT}/core/*.inc"
    "${ROOT}/app/*.h" "${ROOT}/app/*.cpp"
    "${ROOT}/tests/*.h" "${ROOT}/tests/*.cpp")

# A line's code part: anything with no `//` before the match.
set(code "^([^/]|/[^/])*")

set(problems "")
foreach(f IN LISTS sources)
    file(STRINGS "${f}" include_lines REGEX "^[ \t]*#[ \t]*include[ \t]*<")
    foreach(line IN LISTS include_lines)
        string(REGEX REPLACE "^[ \t]*#[ \t]*include[ \t]*<([^>]+)>.*$" "\\1" hdr "${line}")
        if(NOT hdr IN_LIST allowed)
            list(APPEND problems "${f}: #include <${hdr}> is not on the allowlist")
        endif()
    endforeach()
    file(STRINGS "${f}" std_lines REGEX "${code}std::")
    foreach(line IN LISTS std_lines)
        list(APPEND problems "${f}: uses std:: -> ${line}")
    endforeach()
    # The character before the [ must not be the r of `operator[]`.
    file(STRINGS "${f}" lambda_lines REGEX "${code}[^r]\\[[&=]?\\][ \t]*[({]")
    foreach(line IN LISTS lambda_lines)
        list(APPEND problems "${f}: looks like a lambda -> ${line}")
    endforeach()
    if(f MATCHES "^${ROOT}/core/")
        file(STRINGS "${f}" virtual_lines REGEX "${code}virtual[ \t]")
        foreach(line IN LISTS virtual_lines)
            list(APPEND problems "${f}: virtual in the engine -> ${line}")
        endforeach()
    endif()
endforeach()

list(LENGTH sources n)
if(problems)
    foreach(p IN LISTS problems)
        message("  ${p}")
    endforeach()
    message(FATAL_ERROR "check_rules: a code rule is broken (see above)")
endif()
message("check_rules: ${n} sources clean (allowed headers only; no std::, lambdas or engine virtuals)")
