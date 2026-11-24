#Requires -Version 5.1
<#
    Nova 2: the gate 4 equivalence test (build-plan-gate5.md 6)
    -----------------------------------------------------------
    Runs seed 1 to tick 1,600,000 at runs\gate4-reference-d\origin.cfg's
    settings with the build given, writing into a folder of its own, and
    compares its samples, turnover windows and ground record with the
    reference's, byte for byte. With every gate 5 strength off the three
    must be the same. The ground record is compared on the 22 columns the
    reference has: the build's later columns (build-plan-gate4.md 11, the
    reviewer's measures) come after them and are extra. 4 host threads,
    below normal priority; about 4 minutes beside the open-ended run.

      tools\gate4-equivalence.ps1 [-Exe build-g5\bin\nova2.exe] [-Out runs\gate5-equivalence]

    Exit code 0: the same. 1: a file differs or is missing. 2: the run failed.
#>
param([string]$Exe = 'build-g5\bin\nova2.exe', [string]$Out = 'runs\gate5-equivalence')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo
$ref = 'runs\gate4-reference-d'
if (-not (Test-Path $Exe)) { Write-Host "gate4-equivalence: no build at $Exe"; exit 2 }
$null = New-Item -ItemType Directory -Force $Out
$fwd = $Out -replace '\\', '/'
foreach ($f in @("$Out\samples\seed-1.csv", "$Out\windows\seed-1.csv", "$Out\windows\seed-1-ground.csv")) {
    if (Test-Path $f) { Remove-Item -Force $f }
}
# The observers as the reference was made: a sample each 100 ticks, every one
# written, genomes kept at 3 generations; and the samples as a CSV, written
# as the run goes (no origin.records).
$run = @('origin', 'runs/gate4-reference-d/origin.cfg', '--set', "origin.samples=$fwd/samples", '--set',
         "origin.windows=$fwd/windows", '--set', 'observe.sample_every=100', '--set',
         'observe.keep_generations=3',
         # Every gate 5 strength off (build-plan-gate5.md 3, 6).
         '--set', 'wear.rate=0', '--set', 'disk.block_endurance=0', '--set', 'visit.mean=0',
         '--set', 'link.cuts=0', '--set', 'net.lifetime=0', '--set', 'ramp.lead.max=0',
         '--set', 'patch.storm.rate=0', '--set', 'patch.swell.rate=0', '--set', 'patch.brownout.rate=0',
         '--set', 'patch.heatwave.rate=0', '--set', 'patch.outage.rate=0', '--set', 'hammer.strength=0',
         '--set', 'flaky.share=0', '--set', 'flaky.worn=1', '--set', 'strait.share=0',
         '--set', 'patch.gust.rate=0', '--set', 'current.strength=0',
         '--set', 'os.killer=0', '--set', 'os.watchdog=0')
$p = Start-Process -FilePath $Exe -ArgumentList $run -NoNewWindow -PassThru `
    -RedirectStandardOutput "$Out\report.txt" -RedirectStandardError "$Out\progress.txt"
$null = $p.Handle
$p.PriorityClass = 'BelowNormal'
if (-not $p.WaitForExit(3 * 3600 * 1000)) {
    $p.Kill()
    Write-Host 'gate4-equivalence: stopped after 3 hours'
    exit 2
}
if ($p.ExitCode -ne 0) { Write-Host "gate4-equivalence: the run ended with exit code $($p.ExitCode)"; exit 2 }
$bad = 0
foreach ($f in @('samples\seed-1.csv', 'windows\seed-1.csv', 'windows\seed-1-ground.csv')) {
    $a = Join-Path $ref $f
    $b = Join-Path $Out $f
    if (-not (Test-Path $b)) { Write-Host "gate4-equivalence: MISSING $b"; $bad++; continue }
    $size = (Get-Item $b).Length
    if ($f -like '*-ground.csv') {
        # The reference's columns, in the reference's order, line for line.
        $la = [System.IO.File]::ReadAllLines((Resolve-Path $a))
        $lb = [System.IO.File]::ReadAllLines((Resolve-Path $b))
        $cols = $la[0].Split(',').Length
        $same = $la.Length -eq $lb.Length
        for ($i = 0; $same -and $i -lt $la.Length; $i++) {
            $part = $lb[$i].Split(',')
            $same = $part.Length -ge $cols -and (($part[0..($cols - 1)] -join ',') -ceq $la[$i])
        }
        if ($same) { Write-Host "gate4-equivalence: same   $f ($($la.Length) lines, on the reference's $cols columns)" }
        else { Write-Host "gate4-equivalence: DIFFERS $f"; $bad++ }
        continue
    }
    $ha = (Get-FileHash -Algorithm SHA256 $a).Hash
    $hb = (Get-FileHash -Algorithm SHA256 $b).Hash
    if ($ha -eq $hb) { Write-Host "gate4-equivalence: same   $f ($size bytes)" }
    else { Write-Host "gate4-equivalence: DIFFERS $f"; $bad++ }
}
if ($bad) { Write-Host 'gate4-equivalence: FAIL'; exit 1 }
Write-Host 'gate4-equivalence: PASS - the build gives the reference again, byte for byte'
exit 0
