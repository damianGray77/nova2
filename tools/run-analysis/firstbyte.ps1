param([string]$Out, [long]$Bin = 1000000, [string]$Dir = (Join-Path $PSScriptRoot '..\..\runs\open-2\records'))
# Per time bin of kept genomes: share whose first byte (signed) equals the genome's length, and where that byte lands in
# open-2's call slots (os.slots = 0..15 then 0 3 0 3 0 3 0 3: s mod 24; slot 0/16/18/20/22 ALLOC, 3/17/19/21/23 SPAWN).
$src = @'
using System;
using System.IO;
using System.Text;
public static class Fb {
  static FileStream Open(string p) { return new FileStream(p, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 1 << 22); }
  static int Slot(int v) { uint u = (uint)v; int s = (int)(u % 24); int[] map = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,0,3,0,3,0,3,0,3}; return map[s]; }
  public static void Run(string dir, string outPath, long bin) {
    const int B = 2000; var n = new long[B]; var eq = new long[B]; var eqSp = new long[B]; var eqAl = new long[B]; var fbSp = new long[B]; var fbAl = new long[B];
    var lenSp = new long[B]; var lenAl = new long[B];
    using (var f = Open(Path.ChangeExtension(Directory.GetFiles(dir, "*.genomes")[0], "genomes"))) using (var g = Open(Path.ChangeExtension(Directory.GetFiles(dir, "*.genomes")[0], "genome_bytes"))) {
      long gl = g.Length; long rows = f.Length / 64; var buf = new byte[64 * 16384]; var one = new byte[1]; var chunk = new byte[64 << 20]; long cs = -1; int cl = 0;
      for (long i = 0; i < rows; ) {
        int r = (int)Math.Min(16384, rows - i); int need = r * 64, got = 0;
        while (got < need) { int k = f.Read(buf, got, need - got); if (k <= 0) break; got += k; }
        for (int j = 0; j < r; ++j) { int o = j * 64;
          ulong ft = BitConverter.ToUInt64(buf, o + 24); long at = (long)BitConverter.ToUInt64(buf, o + 40); uint len = BitConverter.ToUInt32(buf, o + 48);
          if (len < 8 || at >= gl) continue;
          if (at < cs || at >= cs + cl) { cs = at; g.Seek(at, SeekOrigin.Begin); cl = 0; while (cl < chunk.Length) { int q = g.Read(chunk, cl, chunk.Length - cl); if (q <= 0) break; cl += q; } if (cl == 0) continue; } one[0] = chunk[(int)(at - cs)];
          int fb = (sbyte)one[0]; int k2 = (int)((long)ft / bin); if (k2 >= B) k2 = B - 1;
          n[k2]++; int sf = Slot(fb), sl = Slot((int)len);
          if (sf == 3) fbSp[k2]++; else if (sf == 0) fbAl[k2]++;
          if (sl == 3) lenSp[k2]++; else if (sl == 0) lenAl[k2]++;
          if (fb == (int)len) { eq[k2]++; if (sf == 3) eqSp[k2]++; else if (sf == 0) eqAl[k2]++; }
        }
        i += r;
      }
    }
    using (var w = new StreamWriter(outPath, false, new UTF8Encoding(false))) { w.NewLine = "\n";
      w.WriteLine("bin_start kept(len>=8) first_byte==len% (of those: SPAWN slot%, ALLOC slot%) | first byte on SPAWN slot%, ALLOC slot% | length on SPAWN slot%, ALLOC slot%");
      for (int k = 0; k < B; ++k) { if (n[k] == 0) continue;
        w.WriteLine("{0,9} {1,9} {2,5:F1}% ({3,5:F1}% {4,5:F1}%) | {5,5:F1}% {6,5:F1}% | {7,5:F1}% {8,5:F1}%", k * bin, n[k], 100.0 * eq[k] / n[k], 100.0 * eqSp[k] / Math.Max(1, eq[k]), 100.0 * eqAl[k] / Math.Max(1, eq[k]),
          100.0 * fbSp[k] / n[k], 100.0 * fbAl[k] / n[k], 100.0 * lenSp[k] / n[k], 100.0 * lenAl[k] / n[k]); }
      w.WriteLine("chance levels: a random byte lands on a SPAWN slot 5/24 = 20.8%, ALLOC 5/24 = 20.8%; first byte equal to length by chance ~0.4% (1 in 256)");
    }
  }
}
'@
Add-Type -TypeDefinition $src -Language CSharp
[Fb]::Run($Dir, $Out, $Bin)

