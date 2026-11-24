param([int]$Place, [long]$Since = 24000000, [int]$Bin = 50000, [string]$Out, [string]$Dir = (Join-Path $PSScriptRoot '..\..\runs\open-2\records'))
# One place's kept genomes per time bin: count, lineages, new lineages, founded here, mean len, mean gen, top lineage; plus windows rows.
$src = @'
using System;
using System.IO;
using System.Collections.Generic;
using System.Linq;
using System.Text;
public static class Pl {
  static string Id(ulong u) { return (u >> 40).ToString() + "." + (u & ((1UL << 40) - 1)).ToString(); }
  static FileStream Open(string p) { return new FileStream(p, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 1 << 22); }
  public static void Run(string dir, string outPath, int place, long since, int bin) {
    var bins = new SortedDictionary<long, Dictionary<ulong, long>>(); var lenS = new Dictionary<long, long>(); var genS = new Dictionary<long, long>(); var seen = new HashSet<ulong>(); var newL = new Dictionary<long, int>();
    var lenC = new Dictionary<long, Dictionary<uint, int>>();
    using (var f = Open(Path.ChangeExtension(Directory.GetFiles(dir, "*.genomes")[0], "genomes"))) {
      long rows = f.Length / 64; var buf = new byte[64 * 16384];
      for (long i = 0; i < rows; ) {
        int r = (int)Math.Min(16384, rows - i); int need = r * 64, got = 0;
        while (got < need) { int k = f.Read(buf, got, need - got); if (k <= 0) break; got += k; }
        for (int j = 0; j < r; ++j) { int o = j * 64; uint pl = BitConverter.ToUInt32(buf, o + 56); if (pl != place) continue;
          ulong origin = BitConverter.ToUInt64(buf, o + 16), ft = BitConverter.ToUInt64(buf, o + 24); uint len = BitConverter.ToUInt32(buf, o + 48), gen = BitConverter.ToUInt32(buf, o + 60);
          bool isNew = seen.Add(origin); if ((long)ft < since) continue; long b = (long)ft / bin * bin;
          Dictionary<ulong, long> d; if (!bins.TryGetValue(b, out d)) { d = new Dictionary<ulong, long>(); bins[b] = d; lenS[b] = 0; genS[b] = 0; newL[b] = 0; lenC[b] = new Dictionary<uint, int>(); }
          long v; d.TryGetValue(origin, out v); d[origin] = v + 1; lenS[b] += len; genS[b] += gen; if (isNew) newL[b]++; int c; lenC[b].TryGetValue(len, out c); lenC[b][len] = c + 1; }
        i += r;
      }
    }
    using (var w = new StreamWriter(outPath, false, new UTF8Encoding(false))) { w.NewLine = "\n";
      w.WriteLine("place {0}: bin kept lineages new founded_here mean_len mean_gen top_lineage(share) top_lengths", place);
      foreach (var kv in bins) { long n = kv.Value.Values.Sum(); var top = kv.Value.OrderByDescending(x => x.Value).First(); int here = kv.Value.Keys.Count(x => (x >> 40) == (ulong)place + 1);
        w.WriteLine("{0,9} {1,6} {2,4} {3,4} {4,4} {5,5:F0} {6,5:F0} {7} ({8:F0}%) {9}", kv.Key, n, kv.Value.Count, newL[kv.Key], here, (double)lenS[kv.Key] / n, (double)genS[kv.Key] / n, Id(top.Key), 100.0 * top.Value / n,
          string.Join(",", lenC[kv.Key].OrderByDescending(x => x.Value).Take(4).Select(x => x.Key + ":" + x.Value))); }
      w.WriteLine("windows rows (start starters died births deaths flags):");
      using (var f = Open(Path.ChangeExtension(Directory.GetFiles(dir, "*.genomes")[0], "windows"))) { var b = new byte[40]; long n = f.Length / 40;
        for (long i = 0; i < n; ++i) { f.Read(b, 0, 40); if (BitConverter.ToUInt32(b, 0) != place) continue; ulong st = BitConverter.ToUInt64(b, 8); if ((long)st < since - 1000000) continue;
          w.WriteLine("  {0,9} {1,6} {2,6} {3,7} {4,7} {5:x8}", st, BitConverter.ToUInt32(b, 16), BitConverter.ToUInt32(b, 20), BitConverter.ToUInt32(b, 24), BitConverter.ToUInt32(b, 28), BitConverter.ToUInt32(b, 32)); } }
    }
  }
}
'@
Add-Type -TypeDefinition $src -Language CSharp -ReferencedAssemblies System.Core
[Pl]::Run($Dir, $Out, $Place, $Since, $Bin)
