#!/usr/bin/env rakupp
# Allocations per operation — the malloc census as a tool (V6-PLAN P0).
#
# tools/malloc-census.c counts every malloc, calloc and realloc a process
# makes. This builds it, runs each workload below at N and at 2N operations
# under it, and prints the difference divided by N: the mallocs and bytes ONE
# operation costs, with startup, parsing and the program's fixed work
# cancelled out. A batch that claims "no malloc per call" is checked here.
#
#     ./build/rakupp tools/malloc-census.raku
#     ./build/rakupp tools/malloc-census.raku --only=objects,hash-inc --n=50000
#
# Options:
#     --only=name,name   run just these workloads
#     --n=N              operations in the smaller run (default 100000)
#
# Override the binary via RAKUPP=/path/to/rakupp. macOS only: the counter is
# a DYLD interposer. It counts calls into the system allocator, so work the
# engine serves from its own slab is, correctly, not counted.

my $repo = $*PROGRAM.absolute.IO.parent.parent;
use lib $?FILE.IO.parent.add('lib').Str;
use Gate;

my @only;
my $N = 100_000;
for @*ARGS -> $a {
    if    $a.starts-with('--only=') { @only = $a.substr(7).split(',')>>.trim }
    elsif $a.starts-with('--n=')    { $N = +$a.substr(4) }
    else { note "malloc-census: unknown argument $a"; exit 2 }
}
unless $*DISTRO.name.lc.contains('macos' | 'darwin') {
    note "malloc-census: macOS only (tools/malloc-census.c is a DYLD interposer)";
    exit 2;
}

my %PICK = pick-rakupp($repo);
require-native(%PICK, :tool<malloc-census>,
    :consequence('A translated binary allocates for the translator too.'));
my $RAKUPP = %PICK<path>;
note provenance-line('malloc-census', %PICK);

# Each workload does @N@ operations of one kind, and its setup is the same at
# every N. `objects` is one iteration of tools/bench/objects.raku's loop: two
# `.new`s, three method calls and five attribute reads.
my @work =
    %( :name<objects>,    :note('one objects.raku iteration'),
       :code(q:to/END/) ),
        class Point {
            has $.x; has $.y;
            method len2()    { $.x * $.x + $.y * $.y }
            method shift($d) { Point.new(x => $.x + $d, y => $.y - $d) }
        }
        my $acc = 0;
        for 1 .. @N@ -> $i { my $p = Point.new(x => $i % 100, y => $i % 37); $acc += $p.shift(1).len2 }
        say $acc;
        END
    %( :name<objnew>,     :note('K.new(a => 1, b => 2)'),
       :code('class K { has $.a; has $.b }; my $t; my int $n = 0; while $n < @N@ { $t = K.new(a => 1, b => 2); $n = $n + 1 }; say $t.a') ),
    %( :name<method>,     :note('a plain method call'),
       :code('class K { method m($a) { $a } }; my $k = K.new; my $t = 0; my int $n = 0; while $n < @N@ { $t = $k.m(1); $n = $n + 1 }; say $t') ),
    %( :name<multiwhere>, :note('a call into a where-constrained multi'),
       :code('class K { proto method m(|) {*}; multi method m(Int $x where * > 0) { $x }; multi method m(Int $x) { 0 } }; my $k = K.new; my $t = 0; my int $n = 0; while $n < @N@ { $t = $t + $k.m($n % 3); $n = $n + 1 }; say $t') ),
    %( :name<hash-inc>,   :note('%c{$_ % 1000}++'),
       :code('my %c; for 1 .. @N@ { %c{$_ % 1_000}++ }; say %c.elems') ),
    %( :name<hash-kv>,    :note('one step of for %h.kv -> $k, $v'),
       :code('my %h = (^1000).map({ $_ => $_ }); my $t = 0; for ^(@N@ div 1000) { for %h.kv -> $k, $v { $t += $v } }; say $t') ),
    %( :name<map-block>,  :note('one element of .map({ $_ * 2 })'),
       :code('my $t = 0; $t += $_ for (^@N@).map({ $_ * 2 }); say $t') ),
    %( :name<closure>,    :note('a closure made and called'),
       :code('my $t = 0; for ^@N@ -> $i { my $f = -> $x { $x + $i }; $t += $f(1) }; say $t') );
@work = @work.grep({ .<name> (elem) @only }) if @only;
unless @work {
    note "malloc-census: --only matched no workloads";
    exit 2;
}

my $tmp = $*TMPDIR.add("malloc-census-{$*PID}");
mkdir $tmp;
END { try { .unlink for $tmp.dir; $tmp.rmdir } }

my $lib = $tmp.add('malloc-census.dylib');
{
    my $p = run('cc', '-dynamiclib', '-O2', ~$repo.add('tools/malloc-census.c'), '-o', ~$lib, :out, :err);
    my $err = $p.err.slurp(:close); $p.out.slurp(:close);
    unless $p.exitcode == 0 && $lib.e {
        note "malloc-census: could not build tools/malloc-census.c:\n$err";
        exit 2;
    }
}

# (calls, bytes) for one run, from the census block the interposer writes to
# stderr at exit; Nil when the run failed or wrote no block.
sub census(IO::Path $f --> List) {
    my $p = run($RAKUPP, ~$f, :out, :err, :env(%(|%*ENV, DYLD_INSERT_LIBRARIES => ~$lib)));
    $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    return (Nil, Nil) unless $p.exitcode == 0;
    my $m = $err ~~ / '--- malloc census ---' \s* 'calls' \s+ $<c>=[\d+] \s+ 'bytes' \s+ $<b>=[\d+] /;
    $m ?? (+$<c>, +$<b>) !! (Nil, Nil)
}

say "# per operation: (census at 2N - census at N) / N, N = $N";
say sprintf('%-11s %9s %10s  %s', 'workload', 'mallocs', 'bytes', 'what');
for @work -> %w {
    my @r = ($N, 2 * $N).map: -> $n {
        my $f = $tmp.add("%w<name>-$n.raku");
        $f.spurt(%w<code>.subst('@N@', $n, :g));
        census($f)
    };
    if @r.first({ !.[0].defined }) {
        say sprintf('%-11s %9s %10s  %s', %w<name>, 'n/a', 'n/a', "%w<note> (a run failed)");
        next;
    }
    my $calls = (@r[1][0] - @r[0][0]) / $N;
    my $bytes = (@r[1][1] - @r[0][1]) / $N;
    say sprintf('%-11s %9.2f %10.1f  %s', %w<name>, $calls, $bytes, %w<note>);
}
