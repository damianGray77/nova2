param([string]$Out, [long]$Since = 20000000, [long]$RefFrom = 17000000, [int]$RefEvery = 20, [string]$Dir = (Join-Path $PSScriptRoot '..\..\runs\open-2\records'))
# For each lineage first kept since $Since: the share of its founder genome's 3-byte runs found in the best single older kept
# genome of ANOTHER lineage (a sample of 1 in $RefEvery kept genomes from $RefFrom). Control: the same for random byte strings.
$src = @'
using System;
using System.IO;
using System.Collections.Generic;
using System.Linq;
using System.Text;
public static class Ct {
  static string Id(ulong u) { return (u >> 40).ToString() + "." + (u & ((1UL << 40) - 1)).ToString(); }
  static FileStream Open(string p) { return new FileStream(p, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 1 << 22); }
  static byte[] Bytes(FileStream g, ulong at, uint len) { var b = new byte[len]; g.Seek((long)at, SeekOrigin.Begin); int got = 0; while (got < len) { int k = g.Read(b, got, (int)len - got); if (k <= 0) break; got += k; } return b; }
  static int[] Sh(byte[] b) { var s = new HashSet<int>(); for (int i = 0; i + 2 < b.Length; ++i) s.Add(b[i] << 16 | b[i + 1] << 8 | b[i + 2]); return s.ToArray(); }
  public static void Run(string dir, string outPath, long since, long refFrom, int every) {
    long gbLen = new FileInfo(Path.ChangeExtension(Directory.GetFiles(dir, "*.genomes")[0], "genome_bytes")).Length;
    var fT = new Dictionary<ulong, ulong>(); var fAt = new Dictionary<ulong, ulong>(); var fLen = new Dictionary<ulong, uint>(); var fN = new Dictionary<ulong, long>();
    var rO = new List<ulong>(); var rT = new List<ulong>(); var rAt = new List<ulong>(); var rL = new List<uint>(); long cnt = 0;
    using (var f = Open(Path.ChangeExtension(Directory.GetFiles(dir, "*.genomes")[0], "genomes"))) {
      long n = f.Length / 64; var buf = new byte[64 * 16384];
      for (long i = 0; i < n; ) {
        int r = (int)Math.Min(16384, n - i); int need = r * 64, got = 0;
        while (got < need) { int k = f.Read(buf, got, need - got); if (k <= 0) break; got += k; }
        for (int j = 0; j < r; ++j) { int o = j * 64;
          ulong origin = BitConverter.ToUInt64(buf, o + 16), ft = BitConverter.ToUInt64(buf, o + 24), at = BitConverter.ToUInt64(buf, o + 40); uint len = BitConverter.ToUInt32(buf, o + 48);
          long v; fN.TryGetValue(origin, out v); fN[origin] = v + 1;
          ulong t0; if ((!fT.TryGetValue(origin, out t0) || ft < t0) && (long)at + len <= gbLen && len >= 8) { fT[origin] = ft; fAt[origin] = at; fLen[origin] = len; }
          if ((long)ft >= refFrom && len >= 8 && len <= 300 && (long)at + len <= gbLen && (cnt++ % every) == 0) { rO.Add(origin); rT.Add(ft); rAt.Add(at); rL.Add(len); } }
        i += r;
      }
    }
    var idx = new Dictionary<int, List<int>>();
    using (var g = Open(Path.ChangeExtension(Directory.GetFiles(dir, "*.genomes")[0], "genome_bytes"))) {
      for (int q = 0; q < rO.Count; ++q) foreach (int s in Sh(Bytes(g, rAt[q], rL[q]))) { List<int> l; if (!idx.TryGetValue(s, out l)) { l = new List<int>(); idx[s] = l; } l.Add(q); }
      var score = new int[rO.Count]; var touched = new List<int>();
      Func<int[], ulong, ulong, Tuple<double, int>> best = (sh, origin, tick) => {
        touched.Clear();
        foreach (int s in sh) { List<int> l; if (!idx.TryGetValue(s, out l)) continue; foreach (int q in l) { if (score[q] == 0) touched.Add(q); score[q]++; } }
        int bq = -1, bs = 0; foreach (int q in touched) { if (rO[q] != origin && rT[q] < tick && score[q] > bs) { bs = score[q]; bq = q; } score[q] = 0; }
        return Tuple.Create(sh.Length == 0 ? 0.0 : (double)bs / sh.Length, bq); };
      var w = new StreamWriter(outPath, false, new UTF8Encoding(false)); w.NewLine = "\n";
      w.WriteLine("references: {0} kept genomes (1 in {1} from tick {2}); 3-byte runs indexed {3}", rO.Count, every, refFrom, idx.Count);
      var rnd = new Random(1); var ctl = new List<double>();
      for (int c = 0; c < 300; ++c) { var b = new byte[51]; rnd.NextBytes(b); ctl.Add(best(Sh(b), 0, ulong.MaxValue).Item1); }
      ctl.Sort(); w.WriteLine("control, 300 random 51-byte strings: median {0:F2}, p90 {1:F2}, max {2:F2}", ctl[150], ctl[270], ctl[299]);
      var bins = new int[5]; var binsBig = new int[5]; int k2 = 0, sameHome = 0;
      w.WriteLine("lineage kept founder_tick len contain best_lineage best_tick");
      foreach (var o in fT.Keys.Where(x => (long)fT[x] >= since).OrderByDescending(x => fN[x])) {
        var res = best(Sh(Bytes(g, fAt[o], fLen[o])), o, fT[o]); double c = res.Item1; int bi = c >= 0.9 ? 0 : c >= 0.7 ? 1 : c >= 0.5 ? 2 : c >= 0.3 ? 3 : 4; bins[bi]++; if (fN[o] >= 10000) binsBig[bi]++; ++k2;
        if (res.Item2 >= 0 && (rO[res.Item2] >> 40) == (o >> 40)) sameHome++;
        w.WriteLine("{0,-16} {1,7} {2,9} {3,4} {4:F2} {5,-16} {6}", Id(o), fN[o], fT[o], fLen[o], c, res.Item2 < 0 ? "-" : Id(rO[res.Item2]), res.Item2 < 0 ? 0 : rT[res.Item2]);
      }
      w.WriteLine("SUMMARY {0} lineages: containment >=0.9 {1}, 0.7-0.9 {2}, 0.5-0.7 {3}, 0.3-0.5 {4}, <0.3 {5}; best match founded in the same place {6}", k2, bins[0], bins[1], bins[2], bins[3], bins[4], sameHome);
      w.WriteLine("SUMMARY lineages with 10,000+ kept: >=0.9 {0}, 0.7-0.9 {1}, 0.5-0.7 {2}, 0.3-0.5 {3}, <0.3 {4}", binsBig[0], binsBig[1], binsBig[2], binsBig[3], binsBig[4]);
      w.Close();
    }
  }
}
'@
Add-Type -TypeDefinition $src -Language CSharp -ReferencedAssemblies System.Core
[Ct]::Run($Dir, $Out, $Since, $RefFrom, $RefEvery)
