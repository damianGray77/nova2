param([string]$Out, [string]$Inhabited, [string]$Dir = (Join-Path $PSScriptRoot '..\..\runs\open-2\records'))
# Kept genomes touching the never-inhabited (strict) places: rows kept IN such a place, and lineages FOUNDED in one.
$src = @'
using System;
using System.IO;
using System.Collections.Generic;
using System.Linq;
using System.Text;
public static class St {
  static string Id(ulong u) { return (u >> 40).ToString() + "." + (u & ((1UL << 40) - 1)).ToString(); }
  static FileStream Open(string p) { return new FileStream(p, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 1 << 22); }
  public static void Run(string dir, string outPath, string inhab) {
    var empty = new HashSet<uint>(); foreach (var line in File.ReadAllLines(inhab)) { var p = line.Split(' '); if (p.Length == 2 && p[1] == "0") empty.Add(uint.Parse(p[0])); }
    var inRows = new List<string>(); var founded = new Dictionary<ulong, Dictionary<uint, long>>(); var fFirst = new Dictionary<ulong, ulong>(); var fLast = new Dictionary<ulong, ulong>(); var fGen = new Dictionary<ulong, uint>();
    long gl = new FileInfo(Path.ChangeExtension(Directory.GetFiles(dir, "*.genomes")[0], "genome_bytes")).Length;
    using (var f = Open(Path.ChangeExtension(Directory.GetFiles(dir, "*.genomes")[0], "genomes"))) using (var g = Open(Path.ChangeExtension(Directory.GetFiles(dir, "*.genomes")[0], "genome_bytes"))) {
      long rows = f.Length / 64; var buf = new byte[64 * 16384];
      for (long i = 0; i < rows; ) {
        int r = (int)Math.Min(16384, rows - i); int need = r * 64, got = 0;
        while (got < need) { int k = f.Read(buf, got, need - got); if (k <= 0) break; got += k; }
        for (int j = 0; j < r; ++j) { int o = j * 64;
          ulong origin = BitConverter.ToUInt64(buf, o + 16), ft = BitConverter.ToUInt64(buf, o + 24); long at = (long)BitConverter.ToUInt64(buf, o + 40);
          uint len = BitConverter.ToUInt32(buf, o + 48), place = BitConverter.ToUInt32(buf, o + 56), gen = BitConverter.ToUInt32(buf, o + 60);
          uint home = (uint)(origin >> 40) - 1;
          if (empty.Contains(place)) { string hex = ""; if (at + len <= gl && len <= 200) { long pos = f.Position; var b = new byte[len]; g.Seek(at, SeekOrigin.Begin); g.Read(b, 0, (int)len); hex = BitConverter.ToString(b).Replace("-", "").ToLower(); }
            inRows.Add(string.Format("row {0} place {1} lineage {2} tick {3} len {4} gen {5} {6}", i + j, place, Id(origin), ft, len, gen, hex)); }
          if (empty.Contains(home)) { Dictionary<uint, long> d; if (!founded.TryGetValue(origin, out d)) { d = new Dictionary<uint, long>(); founded[origin] = d; fFirst[origin] = ft; fLast[origin] = ft; fGen[origin] = gen; }
            long v; d.TryGetValue(place, out v); d[place] = v + 1; if (ft < fFirst[origin]) fFirst[origin] = ft; if (ft > fLast[origin]) fLast[origin] = ft; if (gen > fGen[origin]) fGen[origin] = gen; }
        }
        i += r;
      }
    }
    using (var w = new StreamWriter(outPath, false, new UTF8Encoding(false))) { w.NewLine = "\n";
      w.WriteLine("never-inhabited places (from the earlier list): {0}", empty.Count);
      w.WriteLine("kept genomes whose row place is one of them: {0}", inRows.Count); foreach (var s in inRows.Take(60)) w.WriteLine("  " + s);
      w.WriteLine("lineages founded in one of them that have kept genomes: {0}", founded.Count);
      foreach (var kv in founded.OrderBy(x => fFirst[x.Key])) w.WriteLine("  {0,-16} first {1} last {2} maxgen {3} kept by place: {4}", Id(kv.Key), fFirst[kv.Key], fLast[kv.Key], fGen[kv.Key], string.Join(" ", kv.Value.OrderByDescending(x => x.Value).Select(x => x.Key + ":" + x.Value)));
    }
  }
}
'@
Add-Type -TypeDefinition $src -Language CSharp -ReferencedAssemblies System.Core
[St]::Run($Dir, $Out, $Inhabited)
