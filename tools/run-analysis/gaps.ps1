param([string]$Inhabited, [long]$Since = 15000000, [int]$Bin = 200000, [string]$Dir = (Join-Path $PSScriptRoot '..\..\runs\open-2\records'))
# Per bin: how many of the ever-inhabited places kept no new genome, and how many places' top lineage was founded there (home) vs elsewhere.
$src = @'
using System;
using System.IO;
using System.Collections.Generic;
using System.Linq;
using System.Text;
public static class Gp {
  static FileStream Open(string p) { return new FileStream(p, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 1 << 22); }
  public static string Run(string dir, string inhab, long since, int bin) {
    var lax = new List<uint>(); foreach (var line in File.ReadAllLines(inhab)) { var p = line.Split(' '); if (p.Length == 2 && p[1] == "1") lax.Add(uint.Parse(p[0])); }
    var cells = new Dictionary<long, Dictionary<uint, Dictionary<ulong, long>>>(); long maxTick = 0;
    using (var f = Open(Path.ChangeExtension(Directory.GetFiles(dir, "*.genomes")[0], "genomes"))) {
      long rows = f.Length / 64; var buf = new byte[64 * 16384];
      for (long i = 0; i < rows; ) {
        int r = (int)Math.Min(16384, rows - i); int need = r * 64, got = 0;
        while (got < need) { int k = f.Read(buf, got, need - got); if (k <= 0) break; got += k; }
        for (int j = 0; j < r; ++j) { int o = j * 64; long ft = (long)BitConverter.ToUInt64(buf, o + 24); if (ft < since) continue; if (ft > maxTick) maxTick = ft;
          uint place = BitConverter.ToUInt32(buf, o + 56); ulong origin = BitConverter.ToUInt64(buf, o + 16); long b = ft / bin * bin;
          Dictionary<uint, Dictionary<ulong, long>> c; if (!cells.TryGetValue(b, out c)) { c = new Dictionary<uint, Dictionary<ulong, long>>(); cells[b] = c; }
          Dictionary<ulong, long> d; if (!c.TryGetValue(place, out d)) { d = new Dictionary<ulong, long>(); c[place] = d; } long v; d.TryGetValue(origin, out v); d[origin] = v + 1; }
        i += r;
      }
    }
    var sb = new StringBuilder(); sb.AppendFormat("ever-inhabited places {0}; bin {1}\nbin_start empty_of_new_kept  places_with_kept  top_lineage_founded_here  top_from_elsewhere  distinct_top_lineages\n", lax.Count, bin);
    foreach (var b in cells.Keys.OrderBy(x => x)) { if (b + bin > maxTick) continue; var c = cells[b]; int empty = lax.Count(p => !c.ContainsKey(p)); int home = 0, away = 0; var tops = new HashSet<ulong>();
      foreach (var kv in c) { var t = kv.Value.OrderByDescending(x => x.Value).First().Key; tops.Add(t); if ((t >> 40) == kv.Key + 1) home++; else away++; }
      sb.AppendFormat("{0,9} {1,4} {2,4} {3,4} {4,4} {5,4}\n", b, empty, c.Count, home, away, tops.Count); }
    return sb.ToString();
  }
}
'@
Add-Type -TypeDefinition $src -Language CSharp -ReferencedAssemblies System.Core
[Gp]::Run($Dir, $Inhabited, $Since, $Bin)
