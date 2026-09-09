# Regression: the ecosystem batch toward 1000 green distributions — engine bugs
# found by putting the blocked cohort of the 2,530-dist sweep back through
# `rakupp test`. Each one gated a dependency that several other dists wait on.
#
#   * `run(:merge)` / `shell(:merge)` — the adverb was not parsed at all, so
#     nothing was captured, the child wrote straight to our own descriptors and
#     `.out.slurp` came back EMPTY (as-cli-arguments, and the eight dists behind it)
#   * a module that will not PARSE is a catchable exception under `require`,
#     which is a runtime call — only `use` is a compile-time abort
#     (Implementation::Loader, and the seven dists behind it)
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# ---- run/shell :merge ---------------------------------------------------
# Every row carries its own marker, so one stream leaking into another cannot
# read as agreement.
my $exe = $*EXECUTABLE.absolute;

my $m = run $exe, '-e', 'say "MERGE-OUT-11"; note "MERGE-ERR-12"', :merge;
ck $m.out.slurp(:close).trim.split("\n").sort.join("|"),
   'MERGE-ERR-12|MERGE-OUT-11',
   ':merge captures BOTH streams through .out';

my $o = run $exe, '-e', 'say "PLAIN-OUT-13"; note "PLAIN-ERR-14"', :out;
ck $o.out.slurp(:close).trim, 'PLAIN-OUT-13',
   'and plain :out still captures stdout ALONE';

my $e = run $exe, '-e', 'note "ONLY-ERR-15"', :err;
ck $e.err.slurp(:close).trim, 'ONLY-ERR-15', ':err is untouched by the merge path';

my $s = shell 'echo SHELL-OUT-16; echo SHELL-ERR-17 1>&2', :merge;
ck $s.out.slurp(:close).trim.split("\n").sort.join("|"),
   'SHELL-ERR-17|SHELL-OUT-16',
   'shell(:merge) merges the same way';

# `:merge` WINS over an explicit `:err` — both streams still arrive on `.out`.
my $b = run $exe, '-e', 'say "SPLIT-OUT-18"; note "SPLIT-ERR-19"', :merge, :err;
ck $b.out.slurp(:close).trim.split("\n").sort.join("|"),
   'SPLIT-ERR-19|SPLIT-OUT-18',
   ':merge wins over an explicit :err';

# ---- a module that will not parse ---------------------------------------
# Written at run time rather than shipped: a deliberately unparseable file in
# the tree is something every other tool in the repo would trip over. The probe
# runs in a CHILD so the module search path can name the temp directory, which
# is also the shape the real caller has — a loader inside a program.
my $dir = $*TMPDIR.add("rakupp-req-probe-{$*PID}");
$dir.mkdir;
$dir.add('ReqBroken21.rakumod').spurt: "class ReqBroken21 \{\n    method m() \{ 1\n";
$dir.add('ReqWhole22.rakumod').spurt:  'class ReqWhole22 { method m() { "WHOLE-22-OK" } }';
my $probe = $dir.add('req-probe.raku');
$probe.spurt: q:to/PROBE/;
my $whole  = try { my \W = (require ::("ReqWhole22")); W.m };
my $broken = try { my \B = (require ::("ReqBroken21")); B.m };
my $threw  = $! ?? 'yes' !! 'no';
say "whole={$whole // 'NONE'} broken={$broken.defined ?? 'LOADED' !! 'no'} threw=$threw alive=STILL-RUNNING-23";
PROBE

my $r = run $*EXECUTABLE.absolute, '-I' ~ $dir.absolute, $probe.absolute, :out, :err;
my $said = $r.out.slurp(:close).trim.lines.first({ .starts-with('whole=') }) // '';
$r.err.slurp(:close);

ck $said, 'whole=WHOLE-22-OK broken=no threw=yes alive=STILL-RUNNING-23',
   'require: a sound module loads, an unparseable one THROWS, the program survives';

# Rakudo leaves a .precomp tree behind, so clear the directory rather than
# naming the three files this test wrote.
sub sweep($d) { for $d.dir { $_.d ?? sweep($_) !! .unlink }; $d.rmdir }
sweep($dir);

# ---- `$a := $b` binds the CONTAINER, not the name -----------------------
# Assignment through either name is seen by both, because they share one
# container; REBINDING the source is not, because `:=` gives that NAME a
# different container and leaves the alias holding the one it was bound to.
# Aliasing a slot BY NAME cannot express the difference, and a rebound source
# dragged its aliases along — which is Hash::int's `push` reading the current
# item as its own "previous" one, and the PDF family sitting behind it.
my $src = 'CELL-71';
my $alias := $src;
ck $alias, 'CELL-71', 'an alias starts out holding what the source holds';
$src = 'CELL-72';
ck $alias, 'CELL-72', 'assigning to the source writes through the shared container';
$alias = 'CELL-73';
ck $src, 'CELL-73', '…and so does assigning to the alias';
$src := 'CELL-74';
ck $alias, 'CELL-73', 'REBINDING the source leaves the alias on the old container';
ck $src,   'CELL-74', '…and gives the source name the new one';

my $bound := 'BOUND-75';
my $kept := $bound;
$bound := 'BOUND-76';
ck $kept, 'BOUND-75', 'a bound source rebound does not drag its alias either';

# The loop shape the whole thing was found in: a value pulled each turn, and
# the previous one kept beside it.
my @pairs;
my $prev;
for <P-81 P-82 P-83> -> $item {
    my $pulled := $item;
    @pairs.push: "{$prev // 'none'}/{$pulled}";
    $prev := $pulled;
}
ck @pairs.join('|'), 'none/P-81|P-81/P-82|P-82/P-83',
   'a kept alias holds the PREVIOUS pull, not the current one';

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
