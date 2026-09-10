<#
.SYNOPSIS
    The gate for tools\install-windows.ps1 -- install, upgrade, uninstall.

.DESCRIPTION
    Drives the Windows engine installer end to end against a local
    rakupp-windows-x64.zip and asserts what it did, in TAP:

      * the engine it unpacked RUNS (6 * 7, not a file-exists check)
      * -NoRakuAlias leaves no raku.exe, -RakuAlias makes one that runs
      * a re-run keeps the name the install had, and does not add a second
        PATH entry
      * the user PATH gains exactly one entry, and the rest of it comes back
        byte for byte -- including a %VAR% entry planted here on purpose, which
        an installer that writes PATH back through
        [Environment]::GetEnvironmentVariable(...,'User') would silently expand
      * -Uninstall removes the prefix and restores the PATH it found

    It writes to HKCU\Environment and puts the value it found back at the end,
    so it is safe to run on a real machine -- but it is a machine-level change
    while it runs.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File t\windows\install.ps1 -Archive rakupp-windows-x64.zip
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $Archive,
    [string] $Installer
)

$ErrorActionPreference = 'Stop'

if (-not $Installer) {
    $Installer = Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) 'tools\install-windows.ps1'
}
if (-not (Test-Path -LiteralPath $Installer)) { throw "no installer at $Installer" }
if (-not (Test-Path -LiteralPath $Archive))   { throw "no archive at $Archive" }
$Archive = (Resolve-Path -LiteralPath $Archive).Path

$script:n    = 0
$script:bad  = 0
function ok ([bool] $cond, [string] $what, [string] $detail = '') {
    $script:n++
    if ($cond) { Write-Host "ok $($script:n) - $what" }
    else {
        $script:bad++
        Write-Host "not ok $($script:n) - $what"
        if ($detail) { Write-Host "# $detail" }
    }
}

function Get-RawUserPath {
    $k = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Environment', $false)
    if (-not $k) { return '' }
    try {
        $v = $k.GetValue('Path', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        if ($null -eq $v) { return '' }
        return [string] $v
    } finally { $k.Close() }
}
function Set-RawUserPath ([string] $value) {
    $k = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Environment', $true)
    if (-not $k) { $k = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Environment') }
    try { $k.SetValue('Path', $value, [Microsoft.Win32.RegistryValueKind]::ExpandString) } finally { $k.Close() }
}
function Count-Entry ([string] $raw, [string] $dir) {
    $c = 0
    foreach ($e in ($raw -split ';')) { if ($e.Trim().TrimEnd('\') -ieq $dir.TrimEnd('\')) { $c++ } }
    return $c
}
# The installer exits with an explicit code; a sentinel first, so a stale
# $LASTEXITCODE from an earlier command cannot stand in for a run that
# never happened.
#
# A HASHTABLE, not an array. Splatting an array feeds the elements to the
# POSITIONAL parameters -- the "-Dir" in it is a value, not a name -- so
# @('-Archive', $zip, '-Dir', $prefix, ...) put "-Archive" in $Version, the zip
# in $Dir, "-Dir" in $Archive, and then ran out of positions:
#
#   A positional parameter cannot be found that accepts argument '...rakupp-gate-...'
#
# which is how this gate failed on the day it landed. Only a hashtable splat
# binds by name.
function Invoke-Installer ([hashtable] $parameters, [string] $scriptPath = '') {
    if (-not $scriptPath) { $scriptPath = $Installer }
    $global:LASTEXITCODE = 99
    & $scriptPath @parameters
    return $global:LASTEXITCODE
}
function Answer ([string] $exe) {
    if (-not (Test-Path -LiteralPath $exe)) { return '<missing>' }
    return (& $exe -e 'print 6 * 7' 2>&1 | Out-String).Trim()
}

$prefix  = Join-Path ([System.IO.Path]::GetTempPath()) ('rakupp-gate-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$prefix2 = $prefix + '-sb'
$bin     = Join-Path $prefix 'bin'
$exe     = Join-Path $bin 'rakupp.exe'
$alias   = Join-Path $bin 'raku.exe'
# An entry that MUST come back unexpanded. %LOCALAPPDATA% is set on every
# Windows, so an installer that expands PATH turns this into a literal path
# and the last check fails.
$probe  = '%LOCALAPPDATA%\rakupp-path-probe'

$orig = Get-RawUserPath
$want = $orig.TrimEnd(';') + ';' + $probe
Set-RawUserPath $want
Write-Host "# prefix $prefix"

try {
    # ---- install, without the second name ----------------------------------
    $rc = Invoke-Installer @{ Archive = $Archive; Dir = $prefix; NoRakuAlias = $true; Yes = $true }
    ok ($rc -eq 0) 'installer exits 0' "exit $rc"
    ok (Test-Path -LiteralPath $exe) 'bin\rakupp.exe is there'
    ok ((Answer $exe) -eq '42') 'the installed rakupp runs' (Answer $exe)
    ok (-not (Test-Path -LiteralPath $alias)) '-NoRakuAlias leaves no raku.exe'

    $after = Get-RawUserPath
    ok ((Count-Entry $after $bin) -eq 1) 'the user PATH gained exactly one entry' $after
    ok ($after.Contains($probe)) 'the rest of the user PATH is unexpanded' $after

    # ---- upgrade, asking for the second name -------------------------------
    $rc = Invoke-Installer @{ Archive = $Archive; Dir = $prefix; RakuAlias = $true; Yes = $true }
    ok ($rc -eq 0) 'a re-run over an existing install exits 0' "exit $rc"
    ok ((Answer $alias) -eq '42') 'raku.exe runs, and finds the runtime beside it' (Answer $alias)
    $after = Get-RawUserPath
    ok ((Count-Entry $after $bin) -eq 1) 'the re-run did not add a second PATH entry' $after

    # ---- upgrade with no say either way keeps the name it had ---------------
    $rc = Invoke-Installer @{ Archive = $Archive; Dir = $prefix; Yes = $true }
    ok ($rc -eq 0) 'a third run exits 0' "exit $rc"
    ok ((Answer $alias) -eq '42') 'an upgrade keeps the raku name it had' (Answer $alias)

    # ---- uninstall, through the copy the install left in the prefix ---------
    # Someone who installed through `irm | iex` has no file of their own, so
    # that copy IS the documented uninstall command -- run it, don't just look
    # for it.
    $copy     = Join-Path $prefix 'install-windows.ps1'
    $haveCopy = Test-Path -LiteralPath $copy
    ok $haveCopy 'the install left a copy of itself in the prefix'
    $undo = $Installer
    if ($haveCopy) { $undo = $copy }
    $rc = Invoke-Installer @{ Dir = $prefix; Uninstall = $true; Yes = $true } $undo
    ok ($rc -eq 0) 'uninstall exits 0' "exit $rc"
    ok (-not (Test-Path -LiteralPath $prefix)) 'uninstall removed the prefix'
    $final = Get-RawUserPath
    ok ((Count-Entry $final $bin) -eq 0) 'uninstall removed the PATH entry' $final
    ok ($final -eq $want) 'the user PATH came back byte for byte' "want [$want] got [$final]"

    # ---- the script-block form ---------------------------------------------
    # `irm ... | iex` cannot take arguments, so INSTALL.md tells users to run
    # the script through [scriptblock]::Create instead. That form has no
    # $PSCommandPath and no script to `exit` from -- the two things the bottom
    # of the installer branches on -- so it is worth its own run. -NoPath keeps
    # this phase off the registry entirely.
    $sb = [scriptblock]::Create((Get-Content -Raw -LiteralPath $Installer))
    $global:LASTEXITCODE = 99
    & $sb -Archive $Archive -Dir $prefix2 -RakuAlias -NoPath -Yes
    $rc = $global:LASTEXITCODE
    ok ($rc -eq 0) 'the script-block form reports 0 without exiting the session' "exit $rc"
    $raku2 = Join-Path $prefix2 'bin\raku.exe'
    ok ((Answer $raku2) -eq '42') 'the script-block form installs a working raku' (Answer $raku2)
    ok (Test-Path -LiteralPath (Join-Path $prefix2 'install-windows.ps1')) 'it leaves a copy of itself with no file to copy from'
    ok ((Get-RawUserPath) -eq $want) '-NoPath left the user PATH alone' (Get-RawUserPath)
}
finally {
    Set-RawUserPath $orig
    foreach ($p in @($prefix, $prefix2)) {
        if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Recurse -Force -ErrorAction SilentlyContinue }
    }
}

Write-Host "1..$script:n"
if ($script:bad -gt 0) { Write-Host "# $script:bad of $script:n checks failed"; exit 1 }
Write-Host "# all $script:n checks passed"
exit 0
