param([string]$Out, [int]$MinKept = 2, [int]$Places = 0, [string]$Dir = (Join-Path $PSScriptRoot '..\..\runs\open-2\records'))
# Writes the place list strict.ps1, gaps.ps1 and windows.ps1 take as -Inhabited: one line per place, "place 1" when the place
# has kept at least -MinKept genomes over the whole run, else "place 0". In open-2 the never-inhabited half matches the strict
# places (design.md: strict places stay barren); the map's real strict flags are only in saves. Shared read only.
# The number of places is the run's world.places, read from origin.cfg in the folder above -Dir (runs/<run>/origin.cfg);
# -Places N overrides it. It stops if a kept genome's place is outside 0..N-1, rather than drop it.
if ($Places -le 0) {
  $cfg = Join-Path (Split-Path -Parent ([IO.Path]::GetFullPath($Dir))) 'origin.cfg'
  if (-not (Test-Path -LiteralPath $cfg)) { throw "no origin.cfg at $cfg to read world.places from; pass -Places" }
  $m = Select-String -LiteralPath $cfg -Pattern '^\s*world\.places\s*=\s*(\d+)' | Select-Object -First 1
  if (-not $m) { throw "no world.places line in $cfg; pass -Places" }
  $Places = [int]$m.Matches[0].Groups[1].Value
}
$src = @'
using System;
using System.IO;
using System.Text;
public static class Pls {
  static FileStream Open(string p) { return new FileStream(p, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 1 << 22); }
  public static string Run(string dir, string outPath, int minKept, int places) {
    var n = new long[places]; long outside = 0; uint maxOut = 0;
    using (var f = Open(Directory.GetFiles(dir, "*.genomes")[0])) {
      long rows = f.Length / 64; var buf = new byte[64 * 16384];
      for (long i = 0; i < rows; ) {
        int r = (int)Math.Min(16384, rows - i); int need = r * 64, got = 0;
        while (got < need) { int k = f.Read(buf, got, need - got); if (k <= 0) break; got += k; }
        for (int j = 0; j < r; ++j) { uint p = BitConverter.ToUInt32(buf, j * 64 + 56); if (p < places) n[p]++; else { ++outside; if (p > maxOut) maxOut = p; } }
        i += r;
      }
    }
    if (outside > 0) throw new Exception(string.Format("{0} kept genomes have a place outside 0..{1} (highest {2}); check -Places", outside, places - 1, maxOut));
    int yes = 0; var sb = new StringBuilder();
    for (int p = 0; p < places; ++p) { bool y = n[p] >= minKept; if (y) ++yes; sb.Append(p).Append(y ? " 1\n" : " 0\n"); }
    File.WriteAllText(outPath, sb.ToString(), new UTF8Encoding(false));
    return string.Format("{0} of {1} places have {2}+ kept genomes", yes, places, minKept);
  }
}
'@
Add-Type -TypeDefinition $src -Language CSharp
[Pls]::Run($Dir, $Out, $MinKept, $Places)
