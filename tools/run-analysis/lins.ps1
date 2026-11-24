param([string]$Ids, [long]$Mark1 = 20000000, [long]$Mark2 = 24700000, [string]$Dir = (Join-Path $PSScriptRoot '..\..\runs\open-2\records'))
# For named lineages (place.serial as the rundowns print them): kept genomes, places, first and last kept tick, max generation,
# and kept since -Mark1 and since -Mark2. Ids are comma-separated, e.g. -Ids 66.9897477,95.13435271
$src = @'
using System;
using System.IO;
using System.Collections.Generic;
using System.Linq;
using System.Text;
public static class Ln {
  static FileStream Open(string p) { return new FileStream(p, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 1 << 22); }
  public static string Run(string dir, string ids, long m1, long m2) {
    var want = new Dictionary<ulong, string>(); foreach (var s in ids.Split(',')) { var p = s.Trim().Split('.'); want[(ulong.Parse(p[0]) << 40) | ulong.Parse(p[1])] = s.Trim(); }
    var n = new Dictionary<ulong, long>(); var n20 = new Dictionary<ulong, long>(); var n247 = new Dictionary<ulong, long>(); var fst = new Dictionary<ulong, ulong>(); var lst = new Dictionary<ulong, ulong>(); var mg = new Dictionary<ulong, uint>(); var pl = new Dictionary<ulong, HashSet<uint>>();
    foreach (var k in want.Keys) { n[k] = 0; n20[k] = 0; n247[k] = 0; fst[k] = ulong.MaxValue; lst[k] = 0; mg[k] = 0; pl[k] = new HashSet<uint>(); }
    using (var f = Open(Path.ChangeExtension(Directory.GetFiles(dir, "*.genomes")[0], "genomes"))) {
      long rows = f.Length / 64; var buf = new byte[64 * 16384];
      for (long i = 0; i < rows; ) {
        int r = (int)Math.Min(16384, rows - i); int need = r * 64, got = 0;
        while (got < need) { int k = f.Read(buf, got, need - got); if (k <= 0) break; got += k; }
        for (int j = 0; j < r; ++j) { int o = j * 64; ulong origin = BitConverter.ToUInt64(buf, o + 16); if (!want.ContainsKey(origin)) continue;
          ulong ft = BitConverter.ToUInt64(buf, o + 24); uint place = BitConverter.ToUInt32(buf, o + 56), gen = BitConverter.ToUInt32(buf, o + 60);
          n[origin]++; if ((long)ft >= m1) n20[origin]++; if ((long)ft >= m2) n247[origin]++; if (ft < fst[origin]) fst[origin] = ft; if (ft > lst[origin]) lst[origin] = ft; if (gen > mg[origin]) mg[origin] = gen; pl[origin].Add(place); }
        i += r;
      }
    }
    var sb = new StringBuilder();
    foreach (var k in want.Keys) sb.AppendFormat("{0,-16} kept {1,8} places {2,3} first {3,9} last {4,9} maxgen {5,5} kept since " + m1 + " {6,8} since " + m2 + " {7,7}\n", want[k], n[k], pl[k].Count, fst[k] == ulong.MaxValue ? 0 : fst[k], lst[k], mg[k], n20[k], n247[k]);
    return sb.ToString();
  }
}
'@
Add-Type -TypeDefinition $src -Language CSharp -ReferencedAssemblies System.Core
[Ln]::Run($Dir, $Ids, $Mark1, $Mark2)
