param([string]$Inhabited, [string]$Periods = '20000000-21000000,26000000-27000000,28000000-', [int]$Top = 15,
      [string]$Dir = (Join-Path $PSScriptRoot '..\..\runs\open-2\records'))
# Births by place from the place windows (40-byte rows: u32 place, u64 start, u32 starters, died, births, deaths, flags).
# For each period ("lo-hi", an empty hi means to now; a window counts when its start is inside): births per tick in all,
# in inhabited and never-inhabited places (-Inhabited from places.ps1), places with 10,000+ births in a window, and the
# places whose births per tick changed most between the first and last period. Shared read only.
$src = @'
using System;
using System.IO;
using System.Collections.Generic;
using System.Linq;
using System.Text;
public static class Win {
  static FileStream Open(string p) { return new FileStream(p, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 1 << 22); }
  public static string Run(string dir, string inhab, string periods, int top) {
    var lax = new HashSet<uint>(); foreach (var line in File.ReadAllLines(inhab)) { var p = line.Split(' '); if (p.Length == 2 && p[1] == "1") lax.Add(uint.Parse(p[0])); }
    var sp = periods.Split(','); int P = sp.Length; var lo = new long[P]; var hi = new long[P];
    for (int i = 0; i < P; ++i) { var lh = sp[i].Split('-'); lo[i] = long.Parse(lh[0]); hi[i] = lh[1].Length > 0 ? long.Parse(lh[1]) : long.MaxValue; }
    var byPlace = new Dictionary<uint, double>[P]; var starts = new HashSet<ulong>[P]; var big = new long[P, 2]; var win = new long[P, 2];
    var tot = new double[P, 2]; var tmin = new ulong[P]; var tmax = new ulong[P];
    for (int i = 0; i < P; ++i) { byPlace[i] = new Dictionary<uint, double>(); starts[i] = new HashSet<ulong>(); tmin[i] = ulong.MaxValue; }
    ulong span = 0, prev = 0;
    using (var f = Open(Path.ChangeExtension(Directory.GetFiles(dir, "*.genomes")[0], "windows"))) {
      var b = new byte[40]; long n = f.Length / 40;
      for (long r = 0; r < n; ++r) {
        int got = 0; while (got < 40) { int k = f.Read(b, got, 40 - got); if (k <= 0) break; got += k; } if (got < 40) break;
        uint place = BitConverter.ToUInt32(b, 0); ulong st = BitConverter.ToUInt64(b, 8); uint births = BitConverter.ToUInt32(b, 24);
        if (st > prev && prev > 0 && span == 0) span = st - prev; if (st > prev) prev = st;
        for (int i = 0; i < P; ++i) if ((long)st >= lo[i] && (long)st < hi[i]) {
          int s = lax.Contains(place) ? 0 : 1; tot[i, s] += births; win[i, s]++; if (births >= 10000) big[i, s]++;
          double v; byPlace[i].TryGetValue(place, out v); byPlace[i][place] = v + births; starts[i].Add(st);
          if (st < tmin[i]) tmin[i] = st; if (st > tmax[i]) tmax[i] = st; }
      }
    }
    var sb = new StringBuilder(); sb.AppendFormat("window length {0} ticks; inhabited places {1}\n", span, lax.Count);
    sb.Append("period            windows | births/tick all  inhabited  never-inhabited | windows with 10k+ births: inhabited, never-inhabited (of all such place-windows)\n");
    var ticks = new double[P];
    for (int i = 0; i < P; ++i) { ticks[i] = starts[i].Count * (double)span; if (ticks[i] == 0) continue;
      sb.AppendFormat("{0,9}-{1,-9} {2,4} | {3,8:F1} {4,9:F1} {5,9:F1} | {6} of {7}, {8} of {9}\n", tmin[i], tmax[i] + span, starts[i].Count, (tot[i, 0] + tot[i, 1]) / ticks[i], tot[i, 0] / ticks[i], tot[i, 1] / ticks[i], big[i, 0], win[i, 0], big[i, 1], win[i, 1]); }
    if (P >= 2 && ticks[0] > 0 && ticks[P - 1] > 0) {
      var places = new SortedSet<uint>(byPlace[0].Keys.Concat(byPlace[P - 1].Keys)); var d = new List<Tuple<double, uint, double, double>>();
      foreach (var p in places) { double a, z; byPlace[0].TryGetValue(p, out a); byPlace[P - 1].TryGetValue(p, out z); a /= ticks[0]; z /= ticks[P - 1]; d.Add(Tuple.Create(z - a, p, a, z)); }
      sb.AppendFormat("places with the largest change in births/tick, first period to last ({0} rising, {1} falling by 0.1+):\n", d.Count(x => x.Item1 >= 0.1), d.Count(x => x.Item1 <= -0.1));
      foreach (var t in d.OrderByDescending(x => Math.Abs(x.Item1)).Take(top)) sb.AppendFormat("  place {0,3} {1} {2,7:F2} -> {3,7:F2}\n", t.Item2, lax.Contains(t.Item2) ? "inhabited      " : "never-inhabited", t.Item3, t.Item4);
      sb.AppendFormat("sum of rises in inhabited places {0:F1}, never-inhabited {1:F1} births/tick\n", d.Where(x => lax.Contains(x.Item2)).Sum(x => x.Item1), d.Where(x => !lax.Contains(x.Item2)).Sum(x => x.Item1));
    }
    return sb.ToString();
  }
}
'@
Add-Type -TypeDefinition $src -Language CSharp -ReferencedAssemblies System.Core
[Win]::Run($Dir, $Inhabited, $Periods, $Top)
