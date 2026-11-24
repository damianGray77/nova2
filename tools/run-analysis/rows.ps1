param([string]$Rows, [string]$Dir = (Join-Path $PSScriptRoot '..\..\runs\open-2\records'))
# Print kept-genome rows by index: row lineage parent_hash hash first_tick len gen place hex. Shared read only.
$src = @'
using System;
using System.IO;
using System.Text;
public static class Rw {
  static string Id(ulong u) { return (u >> 40).ToString() + "." + (u & ((1UL << 40) - 1)).ToString(); }
  static FileStream Open(string p) { return new FileStream(p, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 1 << 16); }
  public static string Run(string dir, string rows) {
    var sb = new StringBuilder();
    using (var f = Open(Path.ChangeExtension(Directory.GetFiles(dir, "*.genomes")[0], "genomes"))) using (var g = Open(Path.ChangeExtension(Directory.GetFiles(dir, "*.genomes")[0], "genome_bytes"))) {
      foreach (var s in rows.Split(',')) { long i = long.Parse(s.Trim()); var b = new byte[64]; f.Seek(i * 64, SeekOrigin.Begin); f.Read(b, 0, 64);
        ulong at = BitConverter.ToUInt64(b, 40); uint len = BitConverter.ToUInt32(b, 48); var x = new byte[len]; g.Seek((long)at, SeekOrigin.Begin); int got = g.Read(x, 0, (int)len);
        var h = new StringBuilder(); for (int k = 0; k < got; ++k) h.Append(x[k].ToString("x2"));
        sb.AppendFormat("{0} {1} parent {2:x16} hash {3:x16} tick {4} len {5} gen {6} place {7} {8}\n", i, Id(BitConverter.ToUInt64(b, 16)), BitConverter.ToUInt64(b, 8), BitConverter.ToUInt64(b, 0),
          BitConverter.ToUInt64(b, 24), len, BitConverter.ToUInt32(b, 60), BitConverter.ToUInt32(b, 56), h); }
    }
    return sb.ToString();
  }
}
'@
Add-Type -TypeDefinition $src -Language CSharp
[Rw]::Run($Dir, $Rows)
