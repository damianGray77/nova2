param([string]$Out, [long]$Since = 20000000, [int]$RefEvery = 1000, [string]$Dir = (Join-Path $PSScriptRoot '..\..\runs\open-2\records'))
# For lineages first kept at or after $Since: the founder (earliest kept genome), its generation, where; whether its exact
# bytes were kept before under another lineage; and its best 3-byte-run similarity to a sample of older kept genomes.
$src = @'
using System;
using System.IO;
using System.Collections.Generic;
using System.Linq;
using System.Text;
public class F { public ulong origin, hash, tick, at; public uint len, gen, place; public long n; public HashSet<uint> places = new HashSet<uint>(); }
public class R { public ulong origin, tick; public int[] sh; public uint len; }
public static class Fd {
  static string Id(ulong u) { return (u >> 40).ToString() + "." + (u & ((1UL << 40) - 1)).ToString(); }
  static FileStream Open(string p) { return new FileStream(p, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 1 << 22); }
  static byte[] Bytes(FileStream g, ulong at, uint len) { var b = new byte[len]; g.Seek((long)at, SeekOrigin.Begin); int got = 0; while (got < len) { int k = g.Read(b, got, (int)len - got); if (k <= 0) break; got += k; } return b; }
  static int[] Sh(byte[] b) { var s = new HashSet<int>(); for (int i = 0; i + 2 < b.Length; ++i) s.Add(b[i] << 16 | b[i + 1] << 8 | b[i + 2]); var a = s.ToArray(); Array.Sort(a); return a; }
  static double Jac(int[] x, int[] y) { int i = 0, j = 0, c = 0; while (i < x.Length && j < y.Length) { if (x[i] == y[j]) { ++c; ++i; ++j; } else if (x[i] < y[j]) ++i; else ++j; } int u = x.Length + y.Length - c; return u == 0 ? 0 : (double)c / u; }
  static string Hex(byte[] b) { var sb = new StringBuilder(); foreach (var x in b) sb.Append(x.ToString("x2")); return sb.ToString(); }
  public static void Run(string dir, string outPath, long since, int refEvery) {
    var first = new Dictionary<ulong, F>(); var refs = new List<R>(); long gbLen = new FileInfo(Path.ChangeExtension(Directory.GetFiles(dir, "*.genomes")[0], "genome_bytes")).Length;
    var refRows = new List<Tuple<ulong, ulong, ulong, uint>>(); long cnt = 0;
    using (var f = Open(Path.ChangeExtension(Directory.GetFiles(dir, "*.genomes")[0], "genomes"))) {
      long n = f.Length / 64; var buf = new byte[64 * 16384];
      for (long i = 0; i < n; ) {
        int r = (int)Math.Min(16384, n - i); int need = r * 64, got = 0;
        while (got < need) { int k = f.Read(buf, got, need - got); if (k <= 0) break; got += k; }
        for (int j = 0; j < r; ++j) { int o = j * 64;
          ulong hash = BitConverter.ToUInt64(buf, o), origin = BitConverter.ToUInt64(buf, o + 16), ft = BitConverter.ToUInt64(buf, o + 24), at = BitConverter.ToUInt64(buf, o + 40);
          uint len = BitConverter.ToUInt32(buf, o + 48), place = BitConverter.ToUInt32(buf, o + 56), gen = BitConverter.ToUInt32(buf, o + 60);
          F a; if (!first.TryGetValue(origin, out a)) { a = new F(); a.origin = origin; a.tick = ulong.MaxValue; first[origin] = a; }
          a.n++; a.places.Add(place);
          if (ft < a.tick && (long)at + len <= gbLen) { a.tick = ft; a.hash = hash; a.at = at; a.len = len; a.gen = gen; a.place = place; }
          if ((long)ft >= since - 3000000 && len >= 8 && len <= 300 && (long)at + len <= gbLen && (cnt++ % refEvery) == 0) refRows.Add(Tuple.Create(origin, ft, at, len));
        }
        i += r;
      }
    }
    var targets = first.Values.Where(x => (long)x.tick >= since && x.tick != ulong.MaxValue).ToList();
    var thash = new Dictionary<ulong, F>(); foreach (var t in targets) thash[t.hash] = t;
    var older = new Dictionary<ulong, Tuple<ulong, ulong>>(); // founder hash -> (older lineage, tick)
    using (var f = Open(Path.ChangeExtension(Directory.GetFiles(dir, "*.genomes")[0], "genomes"))) {
      long n = f.Length / 64; var buf = new byte[64 * 16384];
      for (long i = 0; i < n; ) {
        int r = (int)Math.Min(16384, n - i); int need = r * 64, got = 0;
        while (got < need) { int k = f.Read(buf, got, need - got); if (k <= 0) break; got += k; }
        for (int j = 0; j < r; ++j) { int o = j * 64; ulong hash = BitConverter.ToUInt64(buf, o); F t;
          if (!thash.TryGetValue(hash, out t)) continue;
          ulong origin = BitConverter.ToUInt64(buf, o + 16), ft = BitConverter.ToUInt64(buf, o + 24);
          if (origin != t.origin && ft <= t.tick && !older.ContainsKey(hash)) older[hash] = Tuple.Create(origin, ft); }
        i += r;
      }
    }
    using (var g = Open(Path.ChangeExtension(Directory.GetFiles(dir, "*.genomes")[0], "genome_bytes"))) {
      foreach (var q in refRows) refs.Add(new R { origin = q.Item1, tick = q.Item2, len = q.Item4, sh = Sh(Bytes(g, q.Item3, q.Item4)) });
      var w = new StreamWriter(outPath, false, new UTF8Encoding(false)); w.NewLine = "\n";
      w.WriteLine("lineages first kept since {0}: {1}; reference sample {2} kept genomes (1 in {3}, from {4})", since, targets.Count, refs.Count, refEvery, since - 3000000);
      w.WriteLine("lineage kept places founder_tick founder_gen founder_len founder_place home exact_older best_jaccard best_lineage best_tick | founder hex");
      int exact = 0, sim90 = 0, sim50 = 0, low = 0, home = 0, gen1 = 0; var rows = new List<string>();
      foreach (var t in targets.OrderByDescending(x => x.n)) {
        var b = Bytes(g, t.at, t.len); var sh = Sh(b); double best = 0; R br = null;
        foreach (var rf in refs) { if (rf.origin == t.origin || rf.tick >= t.tick) continue; double j = Jac(sh, rf.sh); if (j > best) { best = j; br = rf; } }
        Tuple<ulong, ulong> ex; bool isEx = older.TryGetValue(t.hash, out ex);
        bool isHome = (t.origin >> 40) == t.place + 1; if (isHome) home++; if (t.gen <= 1) gen1++;
        if (isEx) exact++; else if (best >= 0.9) sim90++; else if (best >= 0.5) sim50++; else low++;
        w.WriteLine("{0,-16} {1,7} {2,3} {3,9} {4,4} {5,4} {6,3} {7} {8} {9:F2} {10} {11} | {12}", Id(t.origin), t.n, t.places.Count, t.tick, t.gen, t.len, t.place, isHome ? "home" : "away",
          isEx ? Id(ex.Item1) + "@" + ex.Item2 : "-", best, br == null ? "-" : Id(br.origin), br == null ? 0 : br.tick, t.len <= 160 ? Hex(b) : Hex(b).Substring(0, 320) + "...");
      }
      w.WriteLine("SUMMARY: {0} lineages; founder kept before under another lineage (exact) {1}; else best similarity >=0.9 {2}, 0.5-0.9 {3}, <0.5 {4}; founder in its home place {5}; founder generation <=1 {6}",
        targets.Count, exact, sim90, sim50, low, home, gen1);
      w.Close();
    }
  }
}
'@
Add-Type -TypeDefinition $src -Language CSharp -ReferencedAssemblies System.Core
[Fd]::Run($Dir, $Out, $Since, $RefEvery)
