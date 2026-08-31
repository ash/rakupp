<#
.SYNOPSIS
  Measure every released Raku++ build against Rakudo, on Windows x64.

.DESCRIPTION
  The Windows counterpart of rakupp-bench-sweep.sh. It fetches every Raku++
  release binary and the pinned benchmark kernels, then runs the project's own
  harness once per release.

  Unlike the Unix script it does NOT build Rakudo: a from-source Rakudo build on
  Windows needs an MSVC or MinGW toolchain set up by hand, which is not
  something a measurement script should attempt. Install Rakudo yourself
  (https://rakudo.org/downloads, or `rakubrew`) and point this at it.

  Era-matching matters: on the machine of record each Raku++ release was
  compared against the Rakudo current on its release date (2026.06 / .07 / .08).
  Supply -RakudoEra to reproduce that. With a single -Rakudo the rakudo column
  is a constant reference instead, which is still useful but is NOT the same
  measurement -- the output records which was used.

.EXAMPLE
  .\rakupp-bench-sweep.ps1 -Rakudo 'C:\rakudo\bin\raku.exe'

.EXAMPLE
  .\rakupp-bench-sweep.ps1 -RakudoEra @{
      '2026.06' = 'C:\rakudo-2026.06\bin\raku.exe'
      '2026.07' = 'C:\rakudo-2026.07\bin\raku.exe'
      '2026.08' = 'C:\rakudo-2026.08\bin\raku.exe' }

.NOTES
  Send back: <WorkDir>\series.tsv and <WorkDir>\environment.txt
#>
[CmdletBinding()]
param(
  [string]    $WorkDir  = (Join-Path (Get-Location) 'rakupp-sweep'),
  [string]    $Rakudo   = '',
  [hashtable] $RakudoEra,
  [string]    $Kernels  = 'fib,loopsum,strcat',   # 'all' = every kernel
  [string]    $RepoRef  = 'v3.23.0',              # pinned: identical kernels everywhere
  [string]    $DriverTag = 'v3.23.0',             # which rakupp interprets the harness
  [string]    $OnlyTags = '',                     # comma-separated subset of tags
  [int]       $Passes = 4,                        # interleaved passes for the per-ERA Rakudo run
  [switch]    $Mingw                              # use the MinGW build instead of MSVC
)

$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'   # Invoke-WebRequest is ~10x faster without it

$RepoSlug = 'ash/rakupp'
$RepoUrl  = 'https://github.com/ash/rakupp.git'
$Asset    = if ($Mingw) { 'rakupp-windows-x64-mingw.zip' } else { 'rakupp-windows-x64.zip' }

# tag, release date, the Rakudo current on that date. In DATE order: Raku++
# version numbers are not chronological (v3.14.0 shipped 2026-08-11, v3.5.0 on
# 2026-08-20), so never re-sort this by version.
$Tags = @(
  @('v0.5.1','2026-07-13','2026.06'), @('v0.7.1','2026-07-16','2026.06')
  @('v0.9.0','2026-07-19','2026.06'), @('v0.9.1','2026-07-20','2026.06')
  @('v1.0.0','2026-07-22','2026.06'), @('v1.1.0','2026-07-24','2026.06')
  @('v1.2.0','2026-07-26','2026.07'), @('v1.2.5','2026-07-28','2026.07')
  @('v1.2.6','2026-07-28','2026.07'), @('v1.5.0','2026-07-28','2026.07')
  @('v1.5.1','2026-07-29','2026.07'), @('v1.5.2','2026-07-31','2026.07')
  @('v1.7.0','2026-07-31','2026.07'), @('v1.8.0','2026-08-03','2026.07')
  @('v2.0.0','2026-08-06','2026.07'), @('v3.0.0','2026-08-08','2026.07')
  @('v3.0.1','2026-08-09','2026.07'), @('v3.1.0','2026-08-11','2026.07')
  @('v3.14.0','2026-08-11','2026.07'),@('v3.5.0','2026-08-20','2026.07')
  @('v3.5.1','2026-08-20','2026.07'), @('v3.6.0','2026-08-21','2026.07')
  @('v3.7.0','2026-08-24','2026.08'), @('v3.20.0','2026-08-27','2026.08')
  @('v3.20.1','2026-08-27','2026.08'),@('v3.21.0','2026-08-29','2026.08')
  @('v3.22.0','2026-08-29','2026.08'),@('v3.23.0','2026-08-29','2026.08')
)
# v0.5.1 and v0.7.1 predate the MinGW asset; fall back for those two.
$NoMingw = @('v0.5.1','v0.7.1')

function Die($m) { Write-Error $m; exit 1 }

# Optional subset, e.g. -OnlyTags 'v3.22.0,v3.23.0'. Filtered here so that
# everything downstream -- fetch, sweep, aggregate -- sees only these tags.
if ($OnlyTags) {
  $want = $OnlyTags -split ',' | ForEach-Object { $_.Trim() }
  $Tags = @($Tags | Where-Object { $want -contains $_[0] })
  if ($Tags.Count -eq 0) { Die "-OnlyTags matched no known tag" }
}

if (-not $Rakudo -and -not $RakudoEra) {
  Die @"
No Rakudo given. This script does not build one on Windows.
Install Rakudo (https://rakudo.org/downloads or rakubrew), then either:
  -Rakudo 'C:\path\to\raku.exe'                  (one build for every tag)
  -RakudoEra @{'2026.06'=...;'2026.07'=...;'2026.08'=...}   (era-matched)
"@
}
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Die 'git is required.' }
if ([Environment]::Is64BitOperatingSystem -eq $false) { Die 'x64 Windows required.' }

New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
Write-Host "workdir : $WorkDir"
Write-Host "asset   : $Asset"

# ---- kernels + harness, pinned so every machine measures the same workload ----
$RepoDir = Join-Path $WorkDir 'repo'
if (-not (Test-Path $RepoDir)) {
  Write-Host "cloning $RepoSlug at $RepoRef ..."
  git clone --quiet --depth 1 --branch $RepoRef $RepoUrl $RepoDir
}
$Harness = Join-Path $RepoDir 'tools\run-bench.raku'
if (-not (Test-Path $Harness)) { Die "harness not found: $Harness" }

# ---- Raku++ release binaries (GitHub download URLs are predictable) ----
Write-Host 'fetching Raku++ release binaries ...'
foreach ($t in $Tags) {
  $tag = $t[0]
  $dir = Join-Path $WorkDir "rakupp\$tag"
  $exe = Join-Path $dir 'bin\rakupp.exe'
  if (Test-Path $exe) { continue }
  $a = if ($Mingw -and $NoMingw -contains $tag) { 'rakupp-windows-x64.zip' } else { $Asset }
  $url = "https://github.com/$RepoSlug/releases/download/$tag/$a"
  $zip = Join-Path $WorkDir "$tag.zip"
  try {
    Invoke-WebRequest -Uri $url -OutFile $zip
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    Expand-Archive -Path $zip -DestinationPath $dir -Force
    Remove-Item $zip -Force
    # archives may or may not carry a top-level folder; normalise
    if (-not (Test-Path $exe)) {
      $found = Get-ChildItem -Path $dir -Recurse -Filter 'rakupp.exe' | Select-Object -First 1
      if ($found) {
        $root = Split-Path (Split-Path $found.FullName -Parent) -Parent
        Get-ChildItem -Path $root | Move-Item -Destination $dir -Force -ErrorAction SilentlyContinue
      }
    }
    if (Test-Path $exe) { Write-Host ("  {0,-8} ok" -f $tag) }
    else                { Write-Host ("  {0,-8} NO rakupp.exe" -f $tag) }
  } catch { Write-Host ("  {0,-8} download FAILED" -f $tag) }
}

$Driver = Join-Path $WorkDir "rakupp\$DriverTag\bin\rakupp.exe"
if (-not (Test-Path $Driver)) { Die "driver rakupp missing: $Driver" }

function Rakudo-For($era) {
  if ($RakudoEra -and $RakudoEra.ContainsKey($era)) { return $RakudoEra[$era] }
  return $Rakudo
}

# ---- environment record ----
$cpu = (Get-CimInstance Win32_Processor | Select-Object -First 1).Name
$envLines = @(
  "date          : $((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))"
  "os / arch     : Windows / x64"
  "cpu           : $cpu"
  "cores         : $($env:NUMBER_OF_PROCESSORS)"
  "kernels ref   : $RepoRef"
  "driver rakupp : $DriverTag"
  "asset         : $Asset"
  "rakudo mode   : $(if ($RakudoEra) {'era-matched'} else {'single (NOT era-matched)'})"
)
foreach ($k in @('2026.06','2026.07','2026.08')) {
  $p = Rakudo-For $k
  if ($p -and (Test-Path $p)) { $envLines += "rakudo $k : $((& $p --version 2>&1 | Select-Object -First 1))" }
}
$envLines | Set-Content (Join-Path $WorkDir 'environment.txt')
$envLines | ForEach-Object { Write-Host $_ }

if ($Kernels -eq 'all') {
  $Kernels = (Get-ChildItem (Join-Path $RepoDir 'tools\bench') -Filter '*.raku' |
              ForEach-Object { $_.BaseName }) -join ','
}
Write-Host "kernels: $Kernels"

# ---- repeatability probe: this box's honest noise figure ----
$probeRaku = Join-Path $WorkDir 'probe.raku'
@'
my $bin    = @*ARGS[0];
my $kernel = @*ARGS[1];
my @r;
for ^9 {
    my $t0 = now;
    run($bin, $kernel, :out).out.slurp(:close);
    @r.push: (now - $t0) * 1000;
}
printf "%.3f (min %.1fms max %.1fms)\n", @r.max / @r.min, @r.min, @r.max;
'@ | Set-Content $probeRaku
$probe = & $Driver $probeRaku $Driver (Join-Path $RepoDir 'tools\bench\hash.raku') 2>$null
Write-Host "repeatability max/min = $probe"
Add-Content (Join-Path $WorkDir 'environment.txt') "probe         : $probe"

# ---- the sweep ----
New-Item -ItemType Directory -Force -Path (Join-Path $WorkDir 'tsv'), (Join-Path $WorkDir 'logs') | Out-Null
Write-Host 'sweeping ...'
foreach ($t in $Tags) {
  $tag, $date, $era = $t
  $rk = Join-Path $WorkDir "rakupp\$tag\bin\rakupp.exe"
  $rd = Rakudo-For $era
  if (-not (Test-Path $rk)) { Write-Host "  SKIP $tag (no binary)";  continue }
  if (-not (Test-Path $rd)) { Write-Host "  SKIP $tag (no rakudo $era)"; continue }
  $tsv = Join-Path $WorkDir "tsv\$tag.tsv"
  $env:RAKUPP = $rk
  $env:RAKUDO = $rd
  $sw = [Diagnostics.Stopwatch]::StartNew()
  # a non-zero exit means an output mismatch, which the TSV records per row
  & $Driver $Harness "--tsv=$tsv" "--only=$Kernels" *> (Join-Path $WorkDir "logs\$tag.log")
  $sw.Stop()
  Write-Host ("  {0,-8} rakudo {1}  {2}s" -f $tag, $era, [int]$sw.Elapsed.TotalSeconds)
  # run-bench.raku hardcodes '/tmp/rakupp-bench-<pid>-<kernel>' and never
  # unlinks it; on Windows that resolves to C:\tmp, not %TEMP%.
  foreach ($d in @('C:\tmp', $env:TEMP)) {
    if ($d -and (Test-Path $d)) {
      Get-ChildItem $d -Filter 'rakupp-bench-*' -ErrorAction SilentlyContinue |
        Remove-Item -Force -Recurse -ErrorAction SilentlyContinue
    }
  }
}
Remove-Item Env:\RAKUPP, Env:\RAKUDO -ErrorAction SilentlyContinue

# ---- aggregation ----
# ---- Rakudo, once per RAKUDO release -----------------------------------------
# The sweep above measures the rakudo lane once per RAKU++ tag because the
# harness needs it as its correctness oracle. Do NOT chart those numbers:
# Rakudo ships monthly, so it cannot change between two Raku++ tags cut on the
# same day, and every difference there is run-to-run noise (up to 19% on sortby
# on the reference machine). Measure each Rakudo ONCE instead, keyed by date.
$erasOut = Join-Path $WorkDir 'rakudo-eras.tsv'
$erasRaku = Join-Path $WorkDir 'eras.raku'
$benchDir = (Join-Path $RepoDir 'tools\bench') -replace '\\','/'
@"
my `$out = @*ARGS[0]; my `$passes = +@*ARGS[1]; my @kernels = @*ARGS[2].split(",");
my @eras; for @*ARGS[3..*] -> `$spec { my (`$n, `$p) = `$spec.split("="); @eras.push: { name => `$n, path => `$p } }
sub t(`$b, `$k) { my `$t0 = now; run(`$b, `$k, :out, :err).out.slurp(:close); (now - `$t0) * 1000 }
my %best; my %half;
for ^`$passes -> `$pass {
  for @kernels -> `$k {
    my `$path = "$benchDir/`$k.raku";
    for ^21 -> `$r {
      for @eras -> %e {
        my `$ms = t(%e<path>, `$path);
        next if `$r == 0;
        my `$key = %e<name> ~ "\t" ~ `$k;
        %best{`$key} = `$ms if !(%best{`$key}:exists) || `$ms < %best{`$key};
        my `$h = `$pass < `$passes / 2 ?? "A" !! "B";
        %half{`$h}{`$key} = `$ms if !(%half{`$h}{`$key}:exists) || `$ms < %half{`$h}{`$key};
      }
    }
  }
  note "  pass {`$pass + 1}/`$passes done";
}
my `$fh = `$out.IO.open(:w);
`$fh.say: "era\tkernel\tms\tconverge_pct";
my `$worst = 0;
for %best.keys.sort -> `$key {
  my `$a = %half<A>{`$key} // %best{`$key}; my `$b = %half<B>{`$key} // %best{`$key};
  my `$d = (((`$a max `$b) / (`$a min `$b)) - 1) * 100;
  `$worst = `$d if `$d > `$worst;
  `$fh.say: (`$key, sprintf("%.1f", %best{`$key}), sprintf("%.1f", `$d)).join("\t");
}
`$fh.close;
printf "  halves agree to within %.1f%% (>2%% means the floor has NOT converged)\n", `$worst;
"@ | Set-Content $erasRaku

Write-Host ''
Write-Host "measuring Rakudo once per release ($Passes interleaved passes) ..."
$eraArgs = @()
foreach ($e in ($Tags | ForEach-Object { $_[2] } | Sort-Object -Unique)) {
  $rp = Rakudo-For $e
  if ($rp -and (Test-Path $rp)) { $eraArgs += "$e=$rp" }
}
& $Driver $erasRaku $erasOut "$Passes" $Kernels @eraArgs

$out = Join-Path $WorkDir 'series.tsv'
$rows = @("tag`tdate`trakudo`tkernel`tinterp_min`tinterp_med`tnative_min`tnative_med`trakudo_min`trakudo_med`tflags")
foreach ($t in $Tags) {
  $tag, $date, $era = $t
  $f = Join-Path $WorkDir "tsv\$tag.tsv"
  if (-not (Test-Path $f)) { continue }
  # Columns by HEADER NAME, not position: the harness grew a mutsu lane after
  # v3.23.0, so rakudo_min_ms moves from field 6 to field 8 depending on the
  # pinned ref. Hardcoded positions yield zero rows against the wrong one.
  $col = @{}
  foreach ($line in Get-Content $f) {
    if ($line.StartsWith('#')) { continue }
    $c = $line -split "`t"
    if ($line.StartsWith('kernel')) {
      for ($i = 0; $i -lt $c.Count; $i++) { $col[$c[$i]] = $i }
      continue
    }
    if ($col.Count -eq 0) { continue }
    function Get-Col($n) { if ($col.ContainsKey($n) -and $col[$n] -lt $c.Count) { $c[$col[$n]] } else { '' } }
    $rows += ($tag, $date, $era, $c[0],
              (Get-Col 'interp_min_ms'), (Get-Col 'interp_med_ms'),
              (Get-Col 'native_min_ms'), (Get-Col 'native_med_ms'),
              (Get-Col 'rakudo_min_ms'), (Get-Col 'rakudo_med_ms'),
              (Get-Col 'flags')) -join "`t"
  }
}
$rows | Set-Content $out
Write-Host ''
Write-Host ''
Write-Host "done."
Write-Host "  $($rows.Count - 1) rows -> $out   (per-tag; rakudo column is ORACLE ONLY)"
Write-Host "  the rakudo reference, one value per release -> $erasOut"
Write-Host "send back: $out, $erasOut and $(Join-Path $WorkDir 'environment.txt')"
