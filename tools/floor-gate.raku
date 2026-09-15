#!/usr/bin/env raku
# The Linux floor gate: what the packaged release NEEDS from the machine that
# runs it, read back from the binaries and held to the numbers the docs
# promise (docs/guide/COMPILERS.md, "What runs where").
#
# Why a gate: the floor is whatever glibc the BUILD machine had, and nothing
# had ever looked. v3.26.0 was built on a ubuntu-24.04 runner and shipped
# needing GLIBC_2.38 (the `__isoc23_strtol` family and `fmod`), so it would
# not start on Ubuntu 22.04, Debian 12 or RHEL 9 while INSTALL.md said "no
# dependencies" (issue #82). The release now builds in a manylinux 2.28
# container; this is what keeps it there when the runner image or the
# container's toolchain rotates.
#
# What it reads, from the install layout (bin/ + lib/):
#   bin/rakupp        needs glibc <= GLIBC-FLOOR, carries its libstdc++
#   lib/librakupp.so  needs glibc <= GLIBC-FLOOR, libstdc++ <= GLIBCXX-FLOOR
#                     (built from the same sources with the same headers as
#                     the runtime archive, so this is the archive's floor too)
#   lib/*.a           reference no __isoc23_* symbol (the glibc 2.38 tell)
#   --exe --static    compiles against the packaged archives, runs, and needs
#                     no libstdc++.so / libgcc_s.so; the default link is the
#                     control, so the check can fail
#
# Usage:  rakupp tools/floor-gate.raku dist/rakupp
# Contract: exit 0 + last line PASS. Linux only; elsewhere it says so and
# passes (release.yml runs it on the Linux legs alone).
#
# FLOOR_GATE_TOOLS=x86_64-w64-mingw32-  reads a layout from ANOTHER machine
# with prefixed binutils (a downloaded release archive on a Mac, say) and
# skips the compile checks, which need the layout's own rakupp to run.

constant GLIBC-FLOOR   = '2.28';    # manylinux_2_28: RHEL 8, Debian 10, Ubuntu 18.10 (2018) and newer
constant GLIBCXX-FLOOR = '3.4.29';  # GCC 11's libstdc++, the container's gcc-toolset-11

my $dist  = @*ARGS[0] // 'dist/rakupp';
my $tools = %*ENV<FLOOR_GATE_TOOLS> // '';
my @fail;

sub check($ok, Str $what) {
    say ($ok ?? 'ok   ' !! 'FAIL ') ~ $what;
    @fail.push($what) unless $ok;
}
sub sh(*@cmd) {
    my $p = run |@cmd, :out, :err;
    my $o = $p.out.slurp(:close);
    my $e = $p.err.slurp(:close);
    ($o, $p.exitcode, $e)
}
# 3.4.29 -> a number that orders like a version
sub vkey(Str $v) {
    my @p = $v.split('.').map(+*);
    (@p[0] // 0) * 1_000_000 + (@p[1] // 0) * 1_000 + (@p[2] // 0)
}
sub top(@matches) {
    my @v = @matches.map({ .[0].Str }).unique;
    @v ?? @v.sort({ vkey($_) }).tail !! ''
}
sub glibc-of(Str $t)   { top($t.match(/ 'GLIBC_'   (\d+ '.' \d+) /, :g)) }
sub glibcxx-of(Str $t) { top($t.match(/ 'GLIBCXX_' (\d+ '.' \d+ '.' \d+) /, :g)) }
sub needed-of(Str $readelf-d) {
    $readelf-d.lines.grep(*.contains('NEEDED')).map({ .match(/ '[' (<-[\]]>+) ']' /)[0].Str })
}

unless $tools || $*KERNEL.name eq 'linux' {
    note "not Linux ({$*KERNEL.name}), nothing to gate";
    say 'PASS';
    exit 0;
}

my $objdump = "{$tools}objdump";
my $readelf = "{$tools}readelf";
my $nm      = "{$tools}nm";
for $objdump, $readelf, $nm -> $t {
    my ($o, $rc, $e) = sh('sh', '-c', "command -v $t");
    if $rc != 0 {
        say "FAIL: $t not found (binutils)";
        exit 1;
    }
}
say "floor gate on $dist: glibc <= {GLIBC-FLOOR}, libstdc++ <= GLIBCXX_{GLIBCXX-FLOOR}";

# ---- bin/rakupp -----------------------------------------------------------
my $bin = "$dist/bin/rakupp";
{
    check $bin.IO.e, "$bin exists";
    my ($dyn, $rc, $e) = sh($objdump, '-T', $bin);
    check $rc == 0, "$objdump reads bin/rakupp";
    my $g = glibc-of($dyn);
    say "  bin/rakupp needs glibc {$g || '?'}";
    check $g && vkey($g) <= vkey(GLIBC-FLOOR), "bin/rakupp: glibc floor within {GLIBC-FLOOR}";
    my ($d, $rc2, $e2) = sh($readelf, '-d', $bin);
    my @needed = needed-of($d);
    say "  bin/rakupp NEEDED: {@needed.join(' ')}";
    check @needed.elems > 0 && !@needed.grep(*.starts-with('libstdc++')),
          'bin/rakupp carries its libstdc++ (no libstdc++.so in NEEDED)';
}

# ---- lib/librakupp.so -----------------------------------------------------
{
    my @so = dir("$dist/lib").grep({ .basename.starts-with('librakupp.so') }).sort;
    check ?@so, 'lib/ holds librakupp.so (RAKUPP_BUILD_SHARED=ON)';
    for @so.head -> $so {                 # one name is enough: the others are the same file
        my ($dyn, $rc, $e) = sh($objdump, '-T', $so.Str);
        my $g = glibc-of($dyn);
        my $x = glibcxx-of($dyn);
        say "  {$so.basename} needs glibc {$g || '?'}, libstdc++ GLIBCXX {$x || '(none: static)'}";
        check $g && vkey($g) <= vkey(GLIBC-FLOOR), "{$so.basename}: glibc floor within {GLIBC-FLOOR}";
        check !$x || vkey($x) <= vkey(GLIBCXX-FLOOR),
              "{$so.basename}: libstdc++ floor within GLIBCXX_{GLIBCXX-FLOOR} (GCC 11)";
    }
}

# ---- lib/*.a --------------------------------------------------------------
{
    my @a = dir("$dist/lib").grep({ .Str.ends-with('.a') }).sort;
    check ?@a, 'lib/ holds the runtime archives';
    my $iso = 0;
    for @a -> $a {
        my ($u, $rc, $e) = sh($nm, '-u', $a.Str);
        $iso += $u.lines.grep(*.contains('__isoc23_')).elems;
    }
    say "  {@a.elems} archives, $iso references to __isoc23_* (the glibc 2.38 tell)";
    check $iso == 0, 'the runtime archives reference no __isoc23_* symbol';
}

# ---- a --static program, against the packaged archives --------------------
if $tools {
    say '  (FLOOR_GATE_TOOLS is set: a foreign layout, the compile checks are skipped)';
}
else {
    my $st = $*TMPDIR.add("floor-static-$*PID").Str;
    my $dy = $*TMPDIR.add("floor-default-$*PID").Str;
    my ($o, $rc, $e) = sh($bin, '--exe', '--static', '-q', '-e', 'say 6 * 7', '-o', $st);
    check $rc == 0, "--exe --static compiles against the packaged archives{$rc ?? ': ' ~ $e.trim !! ''}";
    if $rc == 0 {
        ($o, $rc, $e) = sh($st);
        check $o eq "42\n", 'the --static program runs';
        my ($d, $rc2, $e2) = sh($readelf, '-d', $st);
        my @needed = needed-of($d);
        my ($dyn, $rc3, $e3) = sh($objdump, '-T', $st);
        say "  --static program NEEDED: {@needed.join(' ')}; glibc {glibc-of($dyn) || '?'} (this machine's)";
        check @needed.elems > 0 && !@needed.grep({ .starts-with('libstdc++') || .starts-with('libgcc_s') }),
              'the --static program needs no libstdc++.so or libgcc_s.so';
    }
    # the control: the default link DOES need libstdc++.so, so the check above can fail
    ($o, $rc, $e) = sh($bin, '--exe', '-q', '-e', 'say 6 * 7', '-o', $dy);
    check $rc == 0, '--exe (default link) compiles against the packaged archives';
    if $rc == 0 {
        my ($d, $rc2, $e2) = sh($readelf, '-d', $dy);
        my @needed = needed-of($d);
        say "  default program NEEDED: {@needed.join(' ')}";
        check ?@needed.grep(*.starts-with('libstdc++')), 'control: the default link needs libstdc++.so';
    }
    try unlink $st;
    try unlink $dy;
}

if @fail {
    say "FAIL: {@fail.elems} check(s)";
    say "  - $_" for @fail;
    exit 1;
}
say 'PASS';
