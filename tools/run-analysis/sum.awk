# Summarise batch lines: tag len births faithful | own in out back hand end | fbeq tick gen | calls
{ split($0, p, "|"); split(p[1], a, " "); split(p[2], b, " "); split(p[3], c, " ");
  tick = c[2] + 0; if (tick < lo || tick >= hi) next
  n++; len = a[2]; births = a[3]; faith = a[4]; own = b[1]; inb = b[2]; outb = b[3]; back = b[4]; hand = b[5]; endk = b[6]; fbeq = c[1]
  if (faith > 0) cop++; if (faith >= 2) cop2++
  if (fbeq) fb++
  if (own > 0) ow++; if (own >= 2) ow2++
  # how the first thread goes on making children
  if (faith == 0) k = "no copy"
  else if (outb > 0 && inb + outb >= 2 && back == 0) k = "slide (runs on into the copies after it)"
  else if (hand > 0 && back > 0) k = "handler loop"
  else if (back > 0 && inb >= 2) k = "loop"
  else if (own > 0 && inb + outb <= 1) k = "relay (new thread of itself, one child, then dies)"
  else if (inb + outb <= 1) k = "one child, no relay"
  else k = "other"
  kind[k]++
  m = split(p[4], q, " "); delete seen
  for (i = 1; i <= m; i++) { split(q[i], r, ":"); nm = r[1]; if (nm ~ /^o:/ || nm == "handled" || nm == "HANDLERset" || nm == "SENDport") continue; if (!(nm in seen)) { seen[nm] = 1; calls[nm]++ } }
  if (p[4] ~ /SENDport/) sp++
}
END { printf "genomes %d (first kept %d to %d): copy faithfully alone %d (%.0f%%), first byte = length %d (%.0f%%), new thread of itself %d (%.0f%%), two or more %d\n", n, lo, hi, cop, 100*cop/n, fb, 100*fb/n, ow, 100*ow/n, ow2
  for (k in kind) printf "  %-55s %4d %5.1f%%\n", k, kind[k], 100*kind[k]/n
  printf "  calls made inside the genome (share of genomes):"; for (nm in calls) printf " %s %.1f%%", nm, 100*calls[nm]/n; printf "; SEND to a non-zero port: %d\n", sp }
