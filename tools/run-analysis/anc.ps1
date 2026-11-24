# -Action pick: -N random kept genomes (8-600 bytes) first kept in [-Since, -Until), as "row hash first_tick len gen hex" (batch.sh input).
# -Action chain: walk each row in -In up its parents, -Steps at most. -Seed fixes the random pick so it can be repeated.
param([string]$Action, [string]$In = "", [string]$Out = "", [int]$N = 6000, [long]$Since = 15000000, [long]$Until = [long]::MaxValue,
      [int]$Steps = 100, [int]$Seed = 20261120,
      [string]$Dir = (Join-Path $PSScriptRoot '..\..\runs\open-2\records'))
$src = @'
using System;
using System.IO;
using System.Collections.Generic;
using System.Text;
public static class Anc {
    static FileStream Open(string p) {
        return new FileStream(p, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 1 << 22);
    }
    static string Hex(FileStream f, long at, int len) {
        f.Seek(at, SeekOrigin.Begin);
        var b = new byte[len]; int got = 0; while (got < len) { int k = f.Read(b, got, len - got); if (k <= 0) break; got += k; }
        var sb = new StringBuilder(); for (int i = 0; i < got; ++i) sb.Append(b[i].ToString("x2"));
        return sb.ToString();
    }
    // Random rows first seen at or after `since`, 8 to 600 bytes long: "row hash first_tick len gen hex".
    public static void Pick(string dir, string outPath, int want, long since, long until, int seed) {
        var rnd = new Random(seed);
        var keep = new long[want]; long seen = 0;
        using (var f = Open(Path.ChangeExtension(Directory.GetFiles(dir, "*.genomes")[0], "genomes"))) {
            long n = f.Length / 64; var buf = new byte[64 * 16384];
            for (long i = 0; i < n; ) {
                int rows = (int)Math.Min(16384, n - i); int need = rows * 64, got = 0;
                while (got < need) { int k = f.Read(buf, got, need - got); if (k <= 0) break; got += k; }
                for (int j = 0; j < rows; ++j) {
                    ulong ft = BitConverter.ToUInt64(buf, j * 64 + 24); uint len = BitConverter.ToUInt32(buf, j * 64 + 48);
                    if ((long)ft < since || (long)ft >= until || len < 8 || len > 600) continue;
                    if (seen < want) keep[seen] = i + j; else { long r = (long)(rnd.NextDouble() * (seen + 1)); if (r < want) keep[r] = i + j; }
                    ++seen;
                }
                i += rows;
            }
            int m = (int)Math.Min(seen, want); Array.Sort(keep, 0, m);
            using (var g = Open(Path.ChangeExtension(Directory.GetFiles(dir, "*.genomes")[0], "genome_bytes")))
            using (var w = new StreamWriter(outPath, false, new UTF8Encoding(false))) {
                w.NewLine = "\n"; var b = new byte[64];
                for (int q = 0; q < m; ++q) {
                    f.Seek(keep[q] * 64, SeekOrigin.Begin); f.Read(b, 0, 64);
                    int len = (int)BitConverter.ToUInt32(b, 48);
                    w.WriteLine("{0} {1} {2} {3} {4} {5}", keep[q], BitConverter.ToUInt64(b, 0), BitConverter.ToUInt64(b, 24), len,
                                BitConverter.ToUInt32(b, 60), Hex(g, (long)BitConverter.ToUInt64(b, 40), len));
                }
            }
        }
    }
    // For each row in `inPath` (first field), walk up its parents: "start step row hash first_tick len gen hex".
    public static string Chain(string dir, string inPath, string outPath, int steps) {
        ulong[] hash; int[] idx; long n;
        using (var f = Open(Path.ChangeExtension(Directory.GetFiles(dir, "*.genomes")[0], "genomes"))) {
            n = f.Length / 64; hash = new ulong[n]; idx = new int[n]; var buf = new byte[64 * 16384];
            for (long i = 0; i < n; ) {
                int rows = (int)Math.Min(16384, n - i); int need = rows * 64, got = 0;
                while (got < need) { int k = f.Read(buf, got, need - got); if (k <= 0) break; got += k; }
                for (int j = 0; j < rows; ++j) { hash[i + j] = BitConverter.ToUInt64(buf, j * 64); idx[i + j] = (int)(i + j); }
                i += rows;
            }
            Array.Sort(hash, idx);
            long walked = 0, lost = 0, starts = 0; var done = new HashSet<long>();
            using (var g = Open(Path.ChangeExtension(Directory.GetFiles(dir, "*.genomes")[0], "genome_bytes")))
            using (var w = new StreamWriter(outPath, false, new UTF8Encoding(false))) {
                w.NewLine = "\n"; var b = new byte[64];
                foreach (var line in File.ReadAllLines(inPath)) {
                    if (line.Length == 0) continue;
                    long start = long.Parse(line.Split(' ')[0]); long row = start; ++starts;
                    for (int s = 0; s <= steps; ++s) {
                        f.Seek(row * 64, SeekOrigin.Begin); f.Read(b, 0, 64);
                        ulong h = BitConverter.ToUInt64(b, 0), par = BitConverter.ToUInt64(b, 8);
                        int len = (int)BitConverter.ToUInt32(b, 48);
                        // The bytes are written once per row; later starts that reach it name the row only.
                        string hex = done.Add(row) ? Hex(g, (long)BitConverter.ToUInt64(b, 40), len) : "-";
                        w.WriteLine("{0} {1} {2} {3} {4} {5} {6} {7}", start, s, row, h, BitConverter.ToUInt64(b, 24), len,
                                    BitConverter.ToUInt32(b, 60), hex);
                        ++walked;
                        if (par == 0 || par == h) break;
                        int at = Array.BinarySearch(hash, par);
                        if (at < 0) { ++lost; break; }
                        while (at > 0 && hash[at - 1] == par) --at;
                        row = idx[at];
                    }
                }
            }
            return string.Format("rows {0}, starts {1}, walked {2}, distinct {3}, chains ending at a parent not kept {4}", n, starts, walked, done.Count, lost);
        }
    }
}
'@
Add-Type -TypeDefinition $src -Language CSharp
if ($Action -eq 'pick') { [Anc]::Pick($Dir, $Out, $N, $Since, $Until, $Seed); "picked" }
elseif ($Action -eq 'chain') { [Anc]::Chain($Dir, $In, $Out, $Steps) }
