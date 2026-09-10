<#
.SYNOPSIS
    Install Raku++ (rakupp) on Windows: per user, no admin, on PATH.

.DESCRIPTION
    Downloads a release's rakupp-windows-x64.zip, checks it against the
    published SHA-256, unpacks it into a per-user prefix, offers to install the
    engine under the second name "raku", and puts the prefix's bin\ on the user
    PATH. Nothing is written outside the prefix and HKCU\Environment, so no
    elevation is needed and the whole thing comes off again with -Uninstall.

    This is the ENGINE installer. `rakupp install Foo` -- the MODULE installer,
    tools/install.raku -- is a different program that travels inside the binary.

.PARAMETER Version
    Release tag to install, e.g. v3.26.0 (a leading "v" is added if missing).
    Default: the latest release.

.PARAMETER Dir
    Install prefix. Default: %LOCALAPPDATA%\Programs\rakupp

.PARAMETER Archive
    Install from a local rakupp-windows-x64.zip instead of downloading one.
    No checksum is fetched for a local file.

.PARAMETER RakuAlias
    Install the engine under the name "raku" as well, without asking.

.PARAMETER NoRakuAlias
    Do not install the "raku" name, and do not ask.

.PARAMETER NoPath
    Leave the user PATH alone.

.PARAMETER Prepend
    Put bin\ FIRST on PATH instead of last. Use this when another Raku is
    installed and you want this one to win.

.PARAMETER SkipChecksum
    Install without verifying the download against its .sha256.

.PARAMETER Uninstall
    Remove the install prefix and its PATH entry.

.PARAMETER Yes
    Never prompt: take the defaults (no "raku" name unless -RakuAlias).

.EXAMPLE
    irm https://raw.githubusercontent.com/ash/rakupp/main/tools/install-windows.ps1 | iex

.EXAMPLE
    # with options, since `| iex` cannot pass arguments:
    & ([scriptblock]::Create((irm https://raw.githubusercontent.com/ash/rakupp/main/tools/install-windows.ps1))) -RakuAlias -Prepend

.EXAMPLE
    .\install-windows.ps1 -Uninstall
#>

[CmdletBinding()]
param(
    [string] $Version,
    [string] $Dir,
    [string] $Archive,
    [switch] $RakuAlias,
    [switch] $NoRakuAlias,
    [switch] $NoPath,
    [switch] $Prepend,
    [switch] $SkipChecksum,
    [switch] $Uninstall,
    [switch] $Yes
)

$ErrorActionPreference = 'Stop'
# Invoke-WebRequest redraws a progress bar per chunk on Windows PowerShell, and
# it costs more than the transfer does: a 20 MB asset takes minutes with it on
# and seconds with it off.
$ProgressPreference = 'SilentlyContinue'

$Repo  = 'ash/rakupp'
$Asset = 'rakupp-windows-x64.zip'

function Say  ([string] $m) { Write-Host "==> $m" }
function Note ([string] $m) { Write-Host "    $m" }
# THROWS, and never exits. `exit` inside `irm | iex` or a script block has no
# script to leave, so it closes the user's PowerShell session -- and takes the
# error message with it. The one deliberate exit is at the very bottom, and
# only when this ran as a file.
function Fail ([string] $m) { throw ('rakupp install: ' + $m) }

# `irm ... | iex` cannot pass parameters, so the decisions worth steering from
# an unattended box are also readable from the environment. An explicit switch
# always wins over the variable.
function Test-EnvIs ([string] $name, [string[]] $words) {
    $v = [Environment]::GetEnvironmentVariable($name)
    if ([string]::IsNullOrWhiteSpace($v)) { return $false }
    return $words -contains $v.Trim().ToLowerInvariant()
}

# ---- the user PATH ---------------------------------------------------------
# Read and write HKCU\Environment DIRECTLY, never through
# [Environment]::GetEnvironmentVariable(...,'User'): that call EXPANDS
# %USERPROFILE%-style entries, so writing the result back bakes today's
# expansion into the user's PATH for good. setx is worse still -- it truncates
# the value at 1024 characters.
function Get-UserPath {
    $raw  = ''
    $kind = [Microsoft.Win32.RegistryValueKind]::ExpandString
    $key  = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Environment', $false)
    if ($key) {
        try {
            $v = $key.GetValue('Path', $null,
                     [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
            if ($null -ne $v) {
                $raw = [string] $v
                try { $kind = $key.GetValueKind('Path') } catch { }
            }
        } finally { $key.Close() }
    }
    return New-Object psobject -Property @{ Raw = $raw; Kind = $kind }
}

function Set-UserPath ([string] $value, $kind) {
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Environment', $true)
    if (-not $key) { $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Environment') }
    try { $key.SetValue('Path', $value, $kind) } finally { $key.Close() }
}

# Two entries name the same directory if their text matches or their expansions
# do -- so a "%LOCALAPPDATA%\Programs\rakupp\bin" left by an earlier install is
# recognised as this one, and not added a second time.
function Test-SameDir ([string] $a, [string] $b) {
    if ([string]::IsNullOrWhiteSpace($a) -or [string]::IsNullOrWhiteSpace($b)) { return $false }
    $na = $a.Trim().Trim('"').TrimEnd('\', '/')
    $nb = $b.Trim().Trim('"').TrimEnd('\', '/')
    if ($na -ieq $nb) { return $true }
    try {
        $ea = [Environment]::ExpandEnvironmentVariables($na).TrimEnd('\', '/')
        $eb = [Environment]::ExpandEnvironmentVariables($nb).TrimEnd('\', '/')
        return ($ea -ieq $eb)
    } catch { return $false }
}

# Tell the shell the environment moved, so anything started from Explorer after
# this sees the new PATH without a sign-out. Best effort: a machine where
# Add-Type has no compiler still gets a correct registry, just a later pickup.
function Publish-EnvChange {
    try {
        if (-not ('Rakupp.Native' -as [type])) {
            Add-Type -Namespace Rakupp -Name Native -MemberDefinition @'
[DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Auto)]
public static extern IntPtr SendMessageTimeout(IntPtr hWnd, uint Msg, UIntPtr wParam,
    string lParam, uint fuFlags, uint uTimeout, out UIntPtr lpdwResult);
'@
        }
        $res = [UIntPtr]::Zero
        # HWND_BROADCAST, WM_SETTINGCHANGE, SMTO_ABORTIFHUNG, 5 s
        [void] [Rakupp.Native]::SendMessageTimeout([IntPtr] 0xffff, 0x1A, [UIntPtr]::Zero,
                                                   'Environment', 2, 5000, [ref] $res)
    } catch { }
}

# File by file, rather than `Copy-Item -Recurse` over a wildcard: that form
# nests bin\ inside an existing bin\ instead of merging into it. File.Copy also
# truncates the destination in place, so a hard link to it survives an upgrade.
function Copy-Tree ([string] $from, [string] $to) {
    $from = $from.TrimEnd('\', '/')
    foreach ($f in [System.IO.Directory]::GetFiles($from, '*', 'AllDirectories')) {
        $rel  = $f.Substring($from.Length).TrimStart('\', '/')
        $dest = Join-Path $to $rel
        [void] [System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($dest))
        [System.IO.File]::Copy($f, $dest, $true)
    }
}

# This script's own path and source, read HERE at script scope: inside the
# function below, $MyInvocation would describe the function instead. Either can
# be empty (a script block has no path, and not every host hands back its
# source); the install works regardless, it just cannot leave a copy of itself.
#
# MyCommand.Path, NOT $PSCommandPath: a script block invoked from inside
# another script INHERITS that script's $PSCommandPath, and would then take
# itself for a file -- copying the caller into the prefix and, at the bottom,
# `exit`ing the caller's script out from under it.
$SelfPath = ''
$SelfText = ''
try { $SelfPath = [string] $MyInvocation.MyCommand.Path } catch { }
try {
    $sb = $MyInvocation.MyCommand.ScriptBlock   # a script file carries one...
    if (-not $sb) { $sb = $MyInvocation.MyCommand }   # ...an anonymous block IS one
    $SelfText = [string] $sb
} catch { }

function Invoke-RakuppInstall {
    [CmdletBinding()]
    param(
        [string] $Version,
        [string] $Dir,
        [string] $Archive,
        [switch] $RakuAlias,
        [switch] $NoRakuAlias,
        [switch] $NoPath,
        [switch] $Prepend,
        [switch] $SkipChecksum,
        [switch] $Uninstall,
        [switch] $Yes
    )

    if ($PSVersionTable.PSVersion.Major -lt 5) {
        Fail "needs Windows PowerShell 5.1 or PowerShell 7+ (this is $($PSVersionTable.PSVersion))"
    }
    if ($RakuAlias -and $NoRakuAlias) { Fail '-RakuAlias and -NoRakuAlias contradict each other' }

    if (-not $Dir     -and $env:RAKUPP_INSTALL_DIR) { $Dir     = $env:RAKUPP_INSTALL_DIR }
    if (-not $Version -and $env:RAKUPP_VERSION)     { $Version = $env:RAKUPP_VERSION }
    if (-not $NoPath  -and (Test-EnvIs 'RAKUPP_NO_PATH' @('1', 'y', 'yes', 'true', 'on'))) { $NoPath = $true }
    if (-not $RakuAlias -and -not $NoRakuAlias) {
        if     (Test-EnvIs 'RAKUPP_RAKU_ALIAS' @('1', 'y', 'yes', 'true', 'on'))  { $RakuAlias   = $true }
        elseif (Test-EnvIs 'RAKUPP_RAKU_ALIAS' @('0', 'n', 'no', 'false', 'off')) { $NoRakuAlias = $true }
    }

    if ([string]::IsNullOrWhiteSpace($Dir)) {
        $base = $env:LOCALAPPDATA
        if ([string]::IsNullOrWhiteSpace($base)) { $base = $env:USERPROFILE }
        if ([string]::IsNullOrWhiteSpace($base)) { Fail 'neither %LOCALAPPDATA% nor %USERPROFILE% is set -- pass -Dir' }
        $Dir = Join-Path (Join-Path $base 'Programs') 'rakupp'
    }
    # An unrooted -Dir means "here", where the user typed it.
    if (-not [System.IO.Path]::IsPathRooted($Dir)) { $Dir = Join-Path (Get-Location).Path $Dir }
    $Dir    = [System.IO.Path]::GetFullPath($Dir.TrimEnd('\', '/'))
    $BinDir = Join-Path $Dir 'bin'
    $Exe    = Join-Path $BinDir 'rakupp.exe'
    $Alias  = Join-Path $BinDir 'raku.exe'

    # Whether a question can be asked at all. `irm | iex` still has a console,
    # so this is true there; a CI step or a redirected stdin is not.
    $interactive = $false
    if (-not $Yes) {
        try { $interactive = [Environment]::UserInteractive -and -not [Console]::IsInputRedirected } catch { }
    }

    # ---- uninstall ---------------------------------------------------------
    if ($Uninstall) {
        # Everything that can say no says it BEFORE anything is touched: -Dir
        # can be mistyped, and deleting is the one step here with no undo. So
        # is the question -- answering it after half the work is done would
        # leave the PATH stripped and the files in place.
        $installed = Test-Path -LiteralPath $Dir
        if ($installed -and -not (Test-Path -LiteralPath $Exe)) {
            Fail "$Dir holds no bin\rakupp.exe -- refusing to delete it. Remove it by hand if that is what you meant."
        }
        if ($installed -and -not $Yes) {
            if (-not $interactive) { Fail "re-run with -Yes to delete $Dir" }
            $a = Read-Host "Remove $Dir and its PATH entry? [y/N]"
            if ($a -notmatch '^(y|yes)$') { Say 'nothing changed'; return }
        }

        # PATH first, then the files: an uninstall that breaks off half way
        # leaves an entry pointing at a directory that is still there, rather
        # than one pointing at nothing.
        $p     = Get-UserPath
        $kept  = @()
        $found = $false
        foreach ($e in ($p.Raw -split ';')) {
            if (Test-SameDir $e $BinDir) { $found = $true; continue }
            $kept += $e      # empties included: the rest of PATH goes back byte for byte
        }
        if ($found) {
            Set-UserPath ($kept -join ';') $p.Kind
            Publish-EnvChange
            Say "removed $BinDir from your user PATH"
        }
        else { Say "no $BinDir on your user PATH" }

        if ($installed) {
            Remove-Item -LiteralPath $Dir -Recurse -Force
            Say "deleted $Dir"
        }
        else { Say "nothing installed at $Dir" }
        return
    }

    # ---- get the archive ---------------------------------------------------
    if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64' -or $env:PROCESSOR_ARCHITEW6432 -eq 'ARM64') {
        Say 'this is an ARM64 machine; installing the x64 build, which Windows runs emulated'
    }

    # Read before the unpack overwrites it: an upgrade keeps the names it had.
    $hadAlias = Test-Path -LiteralPath $Alias

    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('rakupp-install-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $tmp -Force | Out-Null
    try {
        if ($Archive) {
            if (-not (Test-Path -LiteralPath $Archive)) { Fail "no such archive: $Archive" }
            $zip = (Resolve-Path -LiteralPath $Archive).Path
            Say "installing from $zip"
        }
        else {
            # The /releases/latest/download/ and /releases/download/<tag>/
            # redirects need no API call: no token, no 60-per-hour rate limit.
            if ($Version) {
                $tag = $Version.Trim()
                if ($tag -notmatch '^v') { $tag = "v$tag" }
                $url = "https://github.com/$Repo/releases/download/$tag/$Asset"
            }
            else {
                $url = "https://github.com/$Repo/releases/latest/download/$Asset"
            }
            # Windows PowerShell 5.1 offers TLS 1.0/1.1 by default; github.com refuses those.
            try {
                [Net.ServicePointManager]::SecurityProtocol =
                    [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
            } catch { }

            $zip = Join-Path $tmp $Asset
            Say "downloading $url"
            try { Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing }
            catch { Fail "download failed: $($_.Exception.Message)" }

            if ($SkipChecksum) { Note 'checksum not verified (-SkipChecksum)' }
            else {
                $sums = "$zip.sha256"
                try { Invoke-WebRequest -Uri "$url.sha256" -OutFile $sums -UseBasicParsing }
                catch { Fail "cannot fetch $Asset.sha256 to check the download: $($_.Exception.Message). Re-run with -SkipChecksum to install without checking." }
                # Both shapes: the bare hash the release step writes, and a
                # "<hash>  <file>" line, in case it is ever made by shasum.
                $want = ''
                foreach ($line in (Get-Content -LiteralPath $sums)) {
                    if ($line -match '([0-9A-Fa-f]{64})') { $want = $Matches[1]; break }
                }
                if (-not $want) { Fail "$Asset.sha256 holds no SHA-256" }
                $got = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
                if ($got -ine $want) { Fail "checksum mismatch`n    expected $want`n    got      $got" }
                Note "sha256 ok ($($got.ToLowerInvariant()))"
            }
        }

        # ---- unpack --------------------------------------------------------
        # Overwriting a running rakupp.exe fails with a bare access-denied from
        # deep inside the copy; name the actual cause instead.
        $busy = @(Get-Process -Name 'rakupp', 'raku' -ErrorAction SilentlyContinue |
                  Where-Object { $_.Path -and $_.Path.StartsWith($Dir, [StringComparison]::OrdinalIgnoreCase) })
        if ($busy.Count -gt 0) {
            Fail "rakupp is running from $Dir (pid $($busy[0].Id)) -- close it and re-run"
        }

        $stage = Join-Path $tmp 'unpacked'
        Expand-Archive -LiteralPath $zip -DestinationPath $stage -Force

        # The release zip carries bin\ lib\ include\ at its root; accept one
        # wrapping folder too, so a re-zipped download still installs.
        $root = $stage
        if (-not (Test-Path -LiteralPath (Join-Path $stage 'bin\rakupp.exe'))) {
            $sub = @(Get-ChildItem -LiteralPath $stage -Directory |
                     Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'bin\rakupp.exe') })
            if ($sub.Count -eq 1) { $root = $sub[0].FullName }
            else { Fail 'the archive holds no bin\rakupp.exe' }
        }

        New-Item -ItemType Directory -Path $Dir -Force | Out-Null
        Copy-Tree $root $Dir
        if (-not (Test-Path -LiteralPath $Exe)) { Fail "install failed: no $Exe" }
        Say "unpacked into $Dir"
    }
    finally {
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
    }

    # It has to RUN, not merely exist: a truncated download, a cut-down zip and
    # a half-copied runtime all pass a file-exists check.
    $ver = ''
    # Whole output, then the first line -- `Select-Object -First 1` would stop
    # the pipeline early and leave $LASTEXITCODE describing the stop rather
    # than rakupp.
    try { $ver = ((& $Exe --version 2>&1 | Out-String) -split "`r?`n")[0].Trim() } catch { }
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($ver)) {
        Fail "$Exe does not run (exit $LASTEXITCODE)"
    }
    $check = (& $Exe -e 'print 6 * 7' 2>&1 | Out-String).Trim()
    if ($check -ne '42') { Fail "$Exe ran but answered [$check] where 42 was due" }

    # Leave a copy of this script in the prefix: -Uninstall is what a user comes
    # back for, and someone who installed through `irm | iex` has no file to
    # re-run. $SelfPath/$SelfText were read at script scope, above.
    $selfCopy = Join-Path $Dir 'install-windows.ps1'
    try {
        if ($SelfPath -and (Test-Path -LiteralPath $SelfPath)) {
            # ...unless this IS that copy, being re-run to upgrade in place.
            if (-not (Test-SameDir (Split-Path $SelfPath -Parent) $Dir)) {
                Copy-Item -LiteralPath $SelfPath -Destination $selfCopy -Force
            }
        }
        elseif ($SelfText.Length -gt 1000) {
            Set-Content -LiteralPath $selfCopy -Value $SelfText -Encoding ASCII
        }
    } catch { }
    if (-not (Test-Path -LiteralPath $selfCopy)) { $selfCopy = '' }

    # ---- the second name ---------------------------------------------------
    # rakupp finds its runtime relative to its own executable
    # (GetModuleFileNameW), so a second name INSIDE bin\ resolves the prefix
    # exactly as rakupp.exe does -- link or copy, the engine cannot tell.
    $otherRaku = @()
    foreach ($d in ($env:PATH -split ';')) {
        if ([string]::IsNullOrWhiteSpace($d)) { continue }
        if (Test-SameDir $d $BinDir) { continue }
        foreach ($ext in @('.exe', '.cmd', '.bat')) {
            try {
                $cand = Join-Path ($d.Trim().Trim('"')) ('raku' + $ext)
                if (Test-Path -LiteralPath $cand -PathType Leaf) { $otherRaku += $cand }
            } catch { }
        }
    }

    $wantAlias = $false
    if     ($RakuAlias)   { $wantAlias = $true }
    elseif ($NoRakuAlias) { $wantAlias = $false }
    elseif ($hadAlias)    { $wantAlias = $true }     # an upgrade keeps the name it had
    elseif ($interactive) {
        if ($otherRaku.Count -gt 0) {
            Say "another raku is already on your PATH: $($otherRaku[0])"
            $wantAlias = ((Read-Host 'Install rakupp under the name "raku" as well? [y/N]') -match '^(y|yes)$')
        }
        else {
            $wantAlias = ((Read-Host 'Install rakupp under the name "raku" as well? [Y/n]') -notmatch '^(n|no)$')
        }
    }
    else { Note 'nobody to ask about the name "raku"; pass -RakuAlias to get it' }

    $aliasHow = ''
    if ($wantAlias) {
        # Re-made on every run: an upgrade may replace rakupp.exe with a new
        # file, and a link to the old one would go on serving the old engine.
        if (Test-Path -LiteralPath $Alias) { Remove-Item -LiteralPath $Alias -Force }
        # A hard link is the only one of the three that needs neither a
        # privilege nor a second copy; a symlink needs Developer Mode or
        # elevation, and a copy always works.
        foreach ($how in @('HardLink', 'SymbolicLink', 'Copy')) {
            try {
                if ($how -eq 'Copy') { Copy-Item -LiteralPath $Exe -Destination $Alias -Force }
                else { New-Item -ItemType $how -Path $Alias -Target $Exe -ErrorAction Stop | Out-Null }
                $aliasHow = $how
                break
            } catch { }
        }
        if (-not $aliasHow) { Fail "could not create $Alias" }
        $check = (& $Alias -e 'print 6 * 7' 2>&1 | Out-String).Trim()
        if ($check -ne '42') { Fail "$Alias was created but answered [$check] where 42 was due" }
        Say "installed the name raku ($($aliasHow.ToLowerInvariant()))"
    }

    # ---- PATH --------------------------------------------------------------
    $pathState = 'not changed (-NoPath)'
    if (-not $NoPath) {
        $p    = Get-UserPath
        $have = $false
        foreach ($e in ($p.Raw -split ';')) { if (Test-SameDir $e $BinDir) { $have = $true; break } }
        if ($have) { $pathState = 'already there' }
        else {
            # Append (or prepend) to the RAW string rather than rebuilding it
            # from its parts: the rest of PATH goes back exactly as it was.
            if ([string]::IsNullOrWhiteSpace($p.Raw)) { $new = $BinDir }
            elseif ($Prepend) { $new = $BinDir + ';' + $p.Raw }
            else { $new = $p.Raw.TrimEnd(';') + ';' + $BinDir }
            Set-UserPath $new $p.Kind
            Publish-EnvChange
            if ($Prepend) { $pathState = 'added, first' } else { $pathState = 'added, last' }
        }
        # This session too, so the rest of this console can use it right away.
        $inSession = $false
        foreach ($e in ($env:PATH -split ';')) { if (Test-SameDir $e $BinDir) { $inSession = $true; break } }
        if (-not $inSession) {
            if ($Prepend) { $env:PATH = $BinDir + ';' + $env:PATH }
            else { $env:PATH = $env:PATH.TrimEnd(';') + ';' + $BinDir }
        }
    }

    # ---- what happened -----------------------------------------------------
    Write-Host ''
    Say "$ver"
    Note "prefix    $Dir"
    if ($wantAlias) { Note 'commands  rakupp, raku' } else { Note 'commands  rakupp' }
    Note "PATH      $BinDir -- $pathState"
    if ($wantAlias -and $otherRaku.Count -gt 0) {
        if ($Prepend) { Note "          raku here wins over $($otherRaku[0])" }
        else { Note "          $($otherRaku[0]) still wins over raku here; re-run with -Prepend to swap them" }
    }
    if ($selfCopy) { Note "undo      & `"$selfCopy`" -Uninstall" }
    else { Note 'undo      re-run this script with -Uninstall' }
    Note 'open a new terminal for the PATH change, then: rakupp -e "say 6 * 7"'
}

# Forward what was bound, built from the parameter variables themselves rather
# than from $PSBoundParameters: that automatic is not set for every way this
# script can be invoked, and where it is not, the lookup walks up into the
# CALLER's scope and forwards the caller's arguments instead of ours.
$fwd = @{}
if ($Version)      { $fwd['Version']      = $Version }
if ($Dir)          { $fwd['Dir']          = $Dir }
if ($Archive)      { $fwd['Archive']      = $Archive }
if ($RakuAlias)    { $fwd['RakuAlias']    = $true }
if ($NoRakuAlias)  { $fwd['NoRakuAlias']  = $true }
if ($NoPath)       { $fwd['NoPath']       = $true }
if ($Prepend)      { $fwd['Prepend']      = $true }
if ($SkipChecksum) { $fwd['SkipChecksum'] = $true }
if ($Uninstall)    { $fwd['Uninstall']    = $true }
if ($Yes)          { $fwd['Yes']          = $true }

$code = 0
try { Invoke-RakuppInstall @fwd }
catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    $code = 1
}

# A caller reading $LASTEXITCODE sees THIS run either way. `exit` is only for
# the file case: as a script block, or through `irm | iex`, exiting would close
# the session the user is standing in.
$global:LASTEXITCODE = $code
if ($SelfPath) { exit $code }
