<#
.SYNOPSIS
    The gate for the Inno Setup GUI installer -- install, upgrade, uninstall.

.DESCRIPTION
    Drives rakupp-setup-windows-x64.exe silently and asserts what it did to the
    machine, in TAP:

      * the engine it installed RUNS (6 * 7, not a file-exists check)
      * the "raku" task is what decides whether raku.exe exists, and the copy
        it makes is a working engine
      * Add/Remove Programs gets an entry, and its version is the version the
        installed binary reports -- the check that catches a setup built with
        the /DAppVersion default still in place
      * the user PATH gains exactly one entry, and the rest of it comes back
        byte for byte, including a %VAR% entry planted here on purpose
      * the uninstaller removes the directory, the PATH entry and the
        Add/Remove entry

    One shell is enough here, unlike t\windows\install.ps1: what is under test
    is a compiled product, not a script whose language runtime differs between
    Windows PowerShell and pwsh.

    It writes to HKCU\Environment and puts the value it found back at the end.

.EXAMPLE
    pwsh -File t\windows\setup.ps1 -Setup rakupp-setup-windows-x64.exe
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $Setup
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Setup)) { throw "no setup at $Setup" }
$Setup = (Resolve-Path -LiteralPath $Setup).Path

$script:n   = 0
$script:bad = 0
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
function Answer ([string] $exe) {
    if (-not (Test-Path -LiteralPath $exe)) { return '<missing>' }
    return (& $exe -e 'print 6 * 7' 2>&1 | Out-String).Trim()
}
# The Add/Remove Programs record, found by name rather than by AppId: the GUID
# would then live in two files and drift.
function Get-ArpEntry {
    $root = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall'
    if (-not (Test-Path $root)) { return $null }
    foreach ($k in (Get-ChildItem $root -ErrorAction SilentlyContinue)) {
        $p = Get-ItemProperty -Path $k.PSPath -ErrorAction SilentlyContinue
        if ($p -and $p.DisplayName -and $p.DisplayName.StartsWith('Raku++')) { return $p }
    }
    return $null
}
# Inno's setup and uninstaller both hand off to a copy of themselves, so -Wait
# can return before the work is done. Wait for the OUTCOME instead.
function Wait-For ([scriptblock] $cond, [int] $seconds = 60) {
    for ($i = 0; $i -lt $seconds; $i++) {
        if (& $cond) { return $true }
        Start-Sleep -Seconds 1
    }
    return (& $cond)
}
function Run-Silent ([string] $exe, [string[]] $arguments) {
    $p = Start-Process -FilePath $exe -ArgumentList $arguments -Wait -PassThru
    return $p.ExitCode
}

$prefix = Join-Path ([System.IO.Path]::GetTempPath()) ('rakupp-setup-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$bin    = Join-Path $prefix 'bin'
$exe    = Join-Path $bin 'rakupp.exe'
$alias  = Join-Path $bin 'raku.exe'
$probe  = '%LOCALAPPDATA%\rakupp-path-probe'
$silent = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/NOCANCEL')

$orig = Get-RawUserPath
$want = $orig.TrimEnd(';') + ';' + $probe
Set-RawUserPath $want
Write-Host "# prefix $prefix"

try {
    # ---- install, PATH only ------------------------------------------------
    $rc = Run-Silent $Setup ($silent + @("/DIR=$prefix", '/TASKS=addtopath', "/LOG=$prefix-install.log"))
    ok ($rc -eq 0) 'setup exits 0' "exit $rc"
    ok (Wait-For { Test-Path -LiteralPath $exe }) 'bin\rakupp.exe is there'
    ok ((Answer $exe) -eq '42') 'the installed rakupp runs' (Answer $exe)
    ok (-not (Test-Path -LiteralPath $alias)) 'without the raku task there is no raku.exe'

    $after = Get-RawUserPath
    ok ((Count-Entry $after $bin) -eq 1) 'the user PATH gained exactly one entry' $after
    ok ($after.Contains($probe)) 'the rest of the user PATH is unexpanded' $after

    $arp = Get-ArpEntry
    ok ($null -ne $arp) 'Add/Remove Programs has an entry'
    # The version reaches the wizard through /DAppVersion; without this the
    # .iss default (0.0.0) would ship and nothing else would notice.
    $engine = ''
    if ((& $exe --version 2>&1 | Out-String) -match '(\d+\.\d+\.\d+)') { $engine = $Matches[1] }
    $shown = ''
    if ($arp) { $shown = [string] $arp.DisplayVersion }
    ok ($shown -eq $engine -and $engine -ne '') 'its version is the version the binary reports' "shown [$shown] engine [$engine]"

    # ---- upgrade, this time with the second name ---------------------------
    $rc = Run-Silent $Setup ($silent + @("/DIR=$prefix", '/TASKS=rakualias,addtopath', "/LOG=$prefix-upgrade.log"))
    ok ($rc -eq 0) 'a re-run over an existing install exits 0' "exit $rc"
    ok (Wait-For { Test-Path -LiteralPath $alias }) 'the raku task installs raku.exe'
    ok ((Answer $alias) -eq '42') 'raku.exe runs, and finds the runtime beside it' (Answer $alias)
    $after = Get-RawUserPath
    ok ((Count-Entry $after $bin) -eq 1) 'the re-run did not add a second PATH entry' $after

    # ---- uninstall ---------------------------------------------------------
    $uninst = Join-Path $prefix 'unins000.exe'
    ok (Test-Path -LiteralPath $uninst) 'the install left an uninstaller'
    if (Test-Path -LiteralPath $uninst) {
        $rc = Run-Silent $uninst $silent
        ok (Wait-For { -not (Test-Path -LiteralPath $prefix) }) 'uninstall removed the prefix'
        $final = Get-RawUserPath
        ok ((Count-Entry $final $bin) -eq 0) 'uninstall removed the PATH entry' $final
        ok ($final -eq $want) 'the user PATH came back byte for byte' "want [$want] got [$final]"
        ok ($null -eq (Get-ArpEntry)) 'the Add/Remove Programs entry is gone'
    }
}
finally {
    Set-RawUserPath $orig
    if (Test-Path -LiteralPath $prefix) { Remove-Item -LiteralPath $prefix -Recurse -Force -ErrorAction SilentlyContinue }
}

Write-Host "1..$script:n"
if ($script:bad -gt 0) { Write-Host "# $script:bad of $script:n checks failed"; exit 1 }
Write-Host "# all $script:n checks passed"
exit 0
