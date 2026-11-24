# Reads one `nova2 trace` of a genome placed at 2097152 and prints:
#   tag len births faithful | own in out back hand end | firstbyte_is_len
# For p0's first thread only:
#   own   SPAWNs whose start lies inside p0's own bytes (a new thread of itself)
#   in    children made by a SPAWN at an offset inside the genome
#   out   children made by a SPAWN at an offset past its end (it ran on into the memory after it)
#   back  backward moves to an offset inside the genome (a loop, or a handler sending it back)
#   hand  times it entered its handler
#   end   fault (no handler), handler-fault (a fault inside its handler), steps (still running at the trace's end)
BEGIN { S = 2097152; own = 0; inb = 0; outb = 0; back = 0; endk = "steps"; prev = -1; last = -999 }
match($0, /instructions; ([0-9]+) births \(([0-9]+) faithful\)/, s) { births = s[1]; faithful = s[2] }
$1 == "t1" && $2 == "p0" && !done {
    off = substr($4, 2) + 0
    if (prev >= 0 && off < len && off >= 0 && off <= prev) back++
    prev = off
    if (match($0, /-> SPAWN\(A ([0-9]+)/, m)) { a = m[1] + 0; if (a >= S && a < S + len) own++; last = off }
    if ($0 ~ /the thread ends/) { endk = ($0 ~ /handler/) ? "handler-fault" : "fault"; done = 1 }
    if ($0 ~ /into its handler/) hand++
}
/^\[[0-9]+\] BIRTH p[0-9]+ from p0/ && !done2 { if (last >= 0 && last < len) inb++; else outb++ }
$1 == "--" && $2 == "thread" && $3 != "1" { done2 = 1 }
END { printf "%s %d %d %d | %d %d %d %d %d %s | %d\n", tag, len, births, faithful, own, inb, outb, back, hand + 0, endk, fb == len }
