param([string]$Out, [long]$From = 15000000, [long]$Bin = 500000, [string]$Slices = '20000000-21000000,23000000-24000000,24700000-',
      [long]$NewSince = 20000000, [string]$Dir = (Join-Path $PSScriptRoot '..\..\runs\open-2\records'))
# -Slices: three tick ranges A,B,C as "lo-hi" (an empty hi means up to now). Lineage shares and place leaders compare A with C.
# -NewSince: a "new" lineage is one first kept at or after this tick. -From/-Bin: the per-bin table.
# One pass over open-2's kept genomes (64-byte rows; shared read). Writes slices, top lineages, place dominance, new spreaders.
$src = @'
using System;
using System.IO;
using System.Collections.Generic;
using System.Linq;
using System.Text;
public class L { public long n; public ulong first = ulong.MaxValue, last; public uint maxGen;
  public long[] sl = new long[3]; public HashSet<uint>[] pl = { new HashSet<uint>(), new HashSet<uint>(), new HashSet<uint>() };
  public HashSet<uint> allPl = new HashSet<uint>(); public Dictionary<uint,int> lenC = new Dictionary<uint,int>();
  public Dictionary<uint,int> grpC = new Dictionary<uint,int>(); public long sampleRow = -1; public uint sampleLen, sampleGen, sampleGroup, samplePlace; public ulong sampleTick; }
public static class G {
  static string Id(ulong u) { return (u >> 40).ToString() + "." + (u & ((1UL << 40) - 1)).ToString(); }
  static FileStream Open(string p) { return new FileStream(p, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 1 << 22); }
  public static void Run(string dir, string outPath, long T0, long BIN, string slices, long newSince) {
    var sp = slices.Split(','); if (sp.Length != 3) throw new ArgumentException("-Slices needs three ranges");
    long[] s0 = new long[3], s1 = new long[3]; string[] nm = new string[3];
    for (int s = 0; s < 3; ++s) { var lh = sp[s].Split('-'); s0[s] = long.Parse(lh[0]); s1[s] = lh[1].Length > 0 ? long.Parse(lh[1]) : long.MaxValue;
      nm[s] = "ABC"[s] + " " + (s0[s] / 1e6).ToString("0.###") + "M-" + (lh[1].Length > 0 ? (s1[s] / 1e6).ToString("0.###") + "M" : "now"); }
    const int NB = 400;
    var lin = new Dictionary<ulong, L>();
    var binN = new long[NB]; var binLen = new long[NB, 1100]; var binLin = new HashSet<ulong>[NB]; var binPl = new HashSet<uint>[NB]; var binMaxGen = new uint[NB];
    for (int i = 0; i < NB; ++i) { binLin[i] = new HashSet<ulong>(); binPl[i] = new HashSet<uint>(); }
    var dom = new Dictionary<uint, Dictionary<ulong, int>>[3]; for (int i = 0; i < 3; ++i) dom[i] = new Dictionary<uint, Dictionary<ulong, int>>();
    long gbLen = new FileInfo(Path.ChangeExtension(Directory.GetFiles(dir, "*.genomes")[0], "genome_bytes")).Length; long rowsAll;
    using (var f = Open(Path.ChangeExtension(Directory.GetFiles(dir, "*.genomes")[0], "genomes"))) {
      long n = f.Length / 64; rowsAll = n; var buf = new byte[64 * 16384];
      for (long i = 0; i < n; ) {
        int r = (int)Math.Min(16384, n - i); int need = r * 64, got = 0;
        while (got < need) { int k = f.Read(buf, got, need - got); if (k <= 0) break; got += k; }
        for (int j = 0; j < r; ++j) {
          int o = j * 64; ulong origin = BitConverter.ToUInt64(buf, o + 16), ft = BitConverter.ToUInt64(buf, o + 24);
          ulong at = BitConverter.ToUInt64(buf, o + 40); uint len = BitConverter.ToUInt32(buf, o + 48), grp = BitConverter.ToUInt32(buf, o + 52);
          uint place = BitConverter.ToUInt32(buf, o + 56), gen = BitConverter.ToUInt32(buf, o + 60);
          L a; if (!lin.TryGetValue(origin, out a)) { a = new L(); lin[origin] = a; }
          a.n++; if (ft < a.first) a.first = ft; if (ft > a.last) a.last = ft; if (gen > a.maxGen) a.maxGen = gen; a.allPl.Add(place);
          if ((long)ft >= T0) { int b = (int)(((long)ft - T0) / BIN); if (b > NB - 1) b = NB - 1; binN[b]++; binLen[b, Math.Min(len, 1099u)]++; binLin[b].Add(origin); binPl[b].Add(place); if (gen > binMaxGen[b]) binMaxGen[b] = gen; }
          for (int s = 0; s < 3; ++s) if ((long)ft >= s0[s] && (long)ft < s1[s]) {
            a.sl[s]++; a.pl[s].Add(place);
            Dictionary<ulong, int> d; if (!dom[s].TryGetValue(place, out d)) { d = new Dictionary<ulong, int>(); dom[s][place] = d; }
            int v; d.TryGetValue(origin, out v); d[origin] = v + 1;
            if (s == 2) { a.lenC.TryGetValue(len, out v); a.lenC[len] = v + 1; a.grpC.TryGetValue(grp, out v); a.grpC[grp] = v + 1;
              if ((long)at + len <= gbLen && len >= 8) { a.sampleRow = i + j; a.sampleLen = len; a.sampleGen = gen; a.sampleGroup = grp; a.samplePlace = place; a.sampleTick = ft; } }
          }
        }
        i += r;
      }
    }
    var w = new StreamWriter(outPath, false, new UTF8Encoding(false)); w.NewLine = "\n";
    w.WriteLine("rows {0}; genome_bytes {1}; lineages with a kept genome {2}", rowsAll, gbLen, lin.Count);
    w.WriteLine("== per " + BIN + " ticks from " + T0 + ": kept, lineages, new lineages (first kept in bin), places, p10/p50/p90 len, top lengths, max gen");
    var seenFirst = new Dictionary<int, int>(); foreach (var a in lin.Values) if ((long)a.first >= T0) { int b = (int)(((long)a.first - T0) / BIN); if (b > NB - 1) b = NB - 1; int v; seenFirst.TryGetValue(b, out v); seenFirst[b] = v + 1; }
    for (int b = 0; b < NB; ++b) { if (binN[b] == 0) continue;
      long acc = 0; int p10 = -1, p50 = -1, p90 = -1; var tops = new List<Tuple<long,int>>();
      for (int len = 0; len < 1100; ++len) { acc += binLen[b, len]; if (p10 < 0 && acc * 10 >= binN[b]) p10 = len; if (p50 < 0 && acc * 2 >= binN[b]) p50 = len; if (p90 < 0 && acc * 10 >= binN[b] * 9) p90 = len; if (binLen[b, len] > 0) tops.Add(Tuple.Create(binLen[b, len], len)); }
      tops.Sort((x, y) => y.Item1.CompareTo(x.Item1)); int nf; seenFirst.TryGetValue(b, out nf);
      w.WriteLine("{0,9} kept {1,8} lineages {2,5} new {3,5} places {4,3} len p10 {5} p50 {6} p90 {7} top {8} maxgen {9}", T0 + b * BIN, binN[b], binLin[b].Count, nf, binPl[b].Count, p10, p50, p90,
        string.Join(",", tops.Take(6).Select(t => t.Item2 + ":" + (100.0 * t.Item1 / binN[b]).ToString("F1") + "%")), binMaxGen[b]);
    }
    for (int s = 0; s < 3; ++s) {
      long tot = lin.Values.Sum(x => x.sl[s]);
      w.WriteLine("== top lineages in slice {0} (kept {1})", nm[s], tot);
      foreach (var kv in lin.Where(x => x.Value.sl[s] > 0).OrderByDescending(x => x.Value.sl[s]).Take(25)) { var a = kv.Value;
        w.WriteLine("  {0,-16} kept {1,7} ({2,4:F1}%) places {3,3}  A/B/C places {4}/{5}/{6}  all-time kept {7} places {8} first {9} last {10} maxgen {11}", Id(kv.Key), a.sl[s], 100.0 * a.sl[s] / tot, a.pl[s].Count,
          a.pl[0].Count, a.pl[1].Count, a.pl[2].Count, a.n, a.allPl.Count, a.first, a.last, a.maxGen); }
    }
    w.WriteLine("== slice C top lineages: modal lengths, top groups, a sample row (newest with bytes)");
    foreach (var kv in lin.Where(x => x.Value.sl[2] > 0).OrderByDescending(x => x.Value.sl[2]).Take(25)) { var a = kv.Value;
      w.WriteLine("  {0,-16} lens {1} groups {2} sample row {3} len {4} gen {5} group {6} place {7} tick {8}", Id(kv.Key),
        string.Join(",", a.lenC.OrderByDescending(x => x.Value).Take(5).Select(x => x.Key + ":" + x.Value)),
        string.Join(",", a.grpC.OrderByDescending(x => x.Value).Take(3).Select(x => x.Key + ":" + x.Value)), a.sampleRow, a.sampleLen, a.sampleGen, a.sampleGroup, a.samplePlace, a.sampleTick); }
    w.WriteLine("== place dominance: place, kept A, top lineage A (share), kept C, top lineage C (share)");
    int changed = 0, both = 0; var domC = new Dictionary<ulong, int>();
    var places = new SortedSet<uint>(dom[0].Keys.Concat(dom[2].Keys));
    foreach (uint p in places) {
      Dictionary<ulong, int> da, dc; dom[0].TryGetValue(p, out da); dom[2].TryGetValue(p, out dc);
      string ta = "-", tc = "-"; ulong la = 0, lc = 0; long na = 0, nc = 0;
      if (da != null) { var t = da.OrderByDescending(x => x.Value).First(); la = t.Key; na = da.Values.Sum(); ta = Id(la) + " (" + (100.0 * t.Value / na).ToString("F0") + "%)"; }
      if (dc != null) { var t = dc.OrderByDescending(x => x.Value).First(); lc = t.Key; nc = dc.Values.Sum(); tc = Id(lc) + " (" + (100.0 * t.Value / nc).ToString("F0") + "%)"; int v; domC.TryGetValue(lc, out v); domC[lc] = v + 1; }
      if (da != null && dc != null) { both++; if (la != lc) changed++; }
      w.WriteLine("  {0,3} {1,6} {2,-24} {3,6} {4}", p, na, ta, nc, tc);
    }
    w.WriteLine("places with kept genomes in A {0}, in C {1}, in both {2}, top lineage changed in {3}", dom[0].Count, dom[2].Count, both, changed);
    w.WriteLine("lineages on top in C by places: " + string.Join(", ", domC.OrderByDescending(x => x.Value).Take(15).Select(x => Id(x.Key) + ":" + x.Value)));
    w.WriteLine("== lineages first kept at or after " + newSince + ", by places reached (top 30)");
    foreach (var kv in lin.Where(x => (long)x.Value.first >= newSince).OrderByDescending(x => x.Value.allPl.Count).ThenByDescending(x => x.Value.n).Take(30)) { var a = kv.Value;
      w.WriteLine("  {0,-16} kept {1,7} places {2,3} first {3} last {4} maxgen {5} C places {6}", Id(kv.Key), a.n, a.allPl.Count, a.first, a.last, a.maxGen, a.pl[2].Count); }
    var nl = lin.Values.Where(x => (long)x.first >= newSince).ToList();
    w.WriteLine("new lineages since " + newSince + ": {0}; reaching 2+ places {1}, 5+ {2}, 10+ {3}; their share of slice C kept {4:F1}%", nl.Count, nl.Count(x => x.allPl.Count >= 2), nl.Count(x => x.allPl.Count >= 5), nl.Count(x => x.allPl.Count >= 10),
      100.0 * nl.Sum(x => x.sl[2]) / Math.Max(1, lin.Values.Sum(x => x.sl[2])));
    w.Close();
  }
}
'@
Add-Type -TypeDefinition $src -Language CSharp -ReferencedAssemblies System.Core
[G]::Run($Dir, $Out, $From, $Bin, $Slices, $NewSince)
