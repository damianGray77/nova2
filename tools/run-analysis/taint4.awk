# Reads one `nova2 trace` and prints one line:
#   <tag> <births> <faithful> | NAME:count ... | S:<result>:<fate>[:detail] ...
# One S item per SENSE call. Its result is followed through registers, the stack and memory:
#   branch  it reached a conditional jump's test or a computed jump
#   arg     it was an argument of a later call (detail: the call), or a CPY length
#   addr    it was used as an address ([B], [D], or CPY's B or D)
#   held    never used, still somewhere at the end of the trace
#   dead    never used, overwritten
function uni(a, b) { return a == "" ? b : (b == "" ? a : a "," b) }
function arith(ids,   n, p, i) { n = split(ids, p, ","); for (i = 1; i <= n; i++) der[p[i]] = 1 }
function mark(ids, lvl, what,   n, p, i) {
    if (ids == "") return
    n = split(ids, p, ",")
    for (i = 1; i <= n; i++) if (lvl > lev[p[i]]) { lev[p[i]] = lvl; det[p[i]] = what; raw[p[i]] = !(p[i] in der) }
}
function rd(t, o) {
    if (o == "A" || o == "B" || o == "C" || o == "D") return ta[t, o]
    if (o == "[B]") { mark(ta[t, "B"], 2, "addr"); return mem[val[t, "B"]] }
    if (o == "[D]") { mark(ta[t, "D"], 2, "addr"); return mem[val[t, "D"]] }
    return ""
}
function wr(t, o, x) {
    if (o == "A" || o == "B" || o == "C" || o == "D") ta[t, o] = x
    else if (o == "IP") mark(x, 3, "jump")
    else if (o == "[B]") { mark(ta[t, "B"], 2, "addr"); mem[val[t, "B"]] = x }
    else if (o == "[D]") { mark(ta[t, "D"], 2, "addr"); mem[val[t, "D"]] = x }
}
/into its handler/ { n["handled"]++ }
match($0, /instructions; ([0-9]+) births \(([0-9]+) faithful\)/, s) { births = s[1]; faithful = s[2] }
$1 ~ /^t[0-9]+$/ && $4 ~ /^\+/ {
    t = $1; op = $5; o = $6; imm = (o == "#") ? $7 : ""
    if (op == "SYS") {
        if (!match($0, /-> ([A-Z]+)\(([^)]*)\) = (-?[0-9]+)/, m)) next
        name = m[1]; off = substr($4, 2) + 0; inside = (off >= 0 && off < len); if (inside) n[name]++; else { n["o:" name]++; ta[t, "A"] = ""; val[t, "A"] = m[3]; next }
        if (name == "SEND" && match(m[2], /B (-?[0-9]+)/, b) && b[1] % 16 != 0) n["SENDport"]++
        if (name == "SETHANDLER" && m[3] + 0 != 0) n["HANDLERset"]++
        mark(rd(t, o), 2, "callno")
        k = split(m[2], args, /, */)
        for (i = 1; i <= k; i++) { split(args[i], a2, " "); mark(ta[t, a2[1]], 2, name) }
        ta[t, "A"] = ""; val[t, "A"] = m[3]
        if (name == "RECV") ta[t, "B"] = ""
        if (name == "SENSE") { ++nid; res[nid] = m[3]; lev[nid] = 0; ta[t, "A"] = nid; match(m[2], /A (-?[0-9]+)/, sa); sv = sa[1] + 0; if (sv < 0) sv += 4294967296; rdg[nid] = sv % 21; st[nid] = $4 "/" o }
        next
    }
    if (op == "MOV") ta[t, "A"] = rd(t, o)
    else if (op == "STO") wr(t, o, ta[t, "A"])
    else if (op == "SWP") { x = rd(t, o); if (o != "#") wr(t, o, ta[t, "A"]); ta[t, "A"] = x }
    else if (op == "PUSH") { stk[t, ++sp[t]] = rd(t, o) }
    else if (op == "POP") { x = ""; if (sp[t] > 0) { x = stk[t, sp[t]]; delete stk[t, sp[t]]; sp[t]-- } wr(t, o, x) }
    else if (op == "ADD" || op == "OR" || op == "SHL" || op == "SHR") { ta[t, "A"] = uni(ta[t, "A"], rd(t, o)); arith(ta[t, "A"]) }
    else if (op == "SUB" || op == "XOR") { ta[t, "A"] = (o == "A") ? "" : uni(ta[t, "A"], rd(t, o)); arith(ta[t, "A"]) }
    else if (op == "MUL" || op == "AND") { ta[t, "A"] = (o == "#" && imm == 0) ? "" : uni(ta[t, "A"], rd(t, o)); arith(ta[t, "A"]) }
    else if (op == "DIV") { ta[t, "A"] = uni(ta[t, "A"], rd(t, o)); arith(ta[t, "A"]) }
    else if (op == "NOT") { ta[t, "A"] = rd(t, o); arith(ta[t, "A"]) }
    else if (op == "JMP") mark(rd(t, o), 3, "jump")
    else if (op == "JEZ" || op == "JNZ" || op == "JGZ" || op == "JLZ") { mark(ta[t, "A"], 3, "branch"); mark(rd(t, o), 3, "jump") }
    else if (op == "CPY") { mark(ta[t, "B"], 2, "addr"); mark(ta[t, "D"], 2, "addr"); mark(rd(t, o), 2, "CPYlen") }
    if (match($0, / A (-?[0-9]+) B (-?[0-9]+) C (-?[0-9]+) D (-?[0-9]+)[ \t\r]*$/, r)) {
        val[t, "A"] = r[1]; val[t, "B"] = r[2]; val[t, "C"] = r[3]; val[t, "D"] = r[4]
    }
}
END {
    for (k in ta) { c = split(ta[k], p, ","); for (i = 1; i <= c; i++) alive[p[i]] = 1 }
    for (k in mem) { c = split(mem[k], p, ","); for (i = 1; i <= c; i++) alive[p[i]] = 1 }
    for (k in stk) { c = split(stk[k], p, ","); for (i = 1; i <= c; i++) alive[p[i]] = 1 }
    printf "%s %d %d |", tag, births, faithful
    for (k in n) printf " %s:%d", k, n[k]
    printf " |"
    for (i = 1; i <= nid; i++) {
        if (lev[i] == 3) f = "branch:" det[i]; else if (lev[i] == 2) f = (det[i] == "addr" ? "addr" : "arg:" det[i])
        else f = (i in alive) ? "held" : "dead"
        printf " S:%d:%s:%s:%s:%s", rdg[i], res[i], st[i], f, (lev[i] >= 2 ? (raw[i] ? "raw" : "derived") : "-")
    }
    printf "\n"
}
