# Regression: the cluster of general fixes that made `zef install <path>` work
# END-TO-END from the CLI under rakupp (each verified against Rakudo):
#   1. a `given` whose `when`s all miss evaluates to False (not Any) — a
#      non-matching `when` is its smartmatch result. zef's license filter keeps
#      candidates via `.?chars` on that result (5 for "False", truthy).
#   2. element mutation through a read-only accessor chain: `$c.dist.metainfo<k> = v`
#      mutates the hash the RO attr holds (only direct `$c.ro = v` dies).
#   3. `gather LABEL: for … { next LABEL }` — a labeled loop after gather.
#   4. dynamic `require ::($name)` loads the module at runtime and yields its
#      type; a failed require throws (so `try require` gives Nil). zef probes
#      every plugin with `(try require ::($ = $module)) ~~ Nil`.
#   5. `next()`/`last()` call form is the control op itself; composes as
#      `next() R, DEBUG(...) if COND` (zef's plugin-skip idiom) — used to skip
#      unconditionally at statement level and die in expression position.
#   6. Any.hash is an empty Hash (`$dist.meta<files>.hash.keys` with no files).
# Contract: exit 0 + last line PASS.
my @fail;

# 1. given with no matching when => False
my %empty;
my $r = do given %empty {
    when .<blacklist>.?chars { 'bl' }
    when .<whitelist>.?chars && 0 { 'wl' }
}
@fail.push('given-nomatch') unless $r === False && $r.?chars == 5;
@fail.push('given-match') unless (do given 5 { when 5 { 'hit' } }) eq 'hit';

# 2. mutation through an RO accessor chain
class D2 { has %.metainfo is rw; }
class C2 { has $.dist; }
my $c = C2.new(dist => D2.new);
$c.dist.metainfo<marked> = 1;
@fail.push('chain-mutate') unless $c.dist.metainfo<marked> == 1;
my $died = False;
try { $c.dist = D2.new; CATCH { default { $died = True } } }
@fail.push('ro-still-dies') unless $died;

# 3. labeled loop after gather
my @g = gather L: for 1..3 -> $x { for 1..2 { take $x; next L } }
@fail.push('gather-label') unless @g eqv [1, 2, 3];

# 4. dynamic require
@fail.push('require-fail-nil') unless (try require ::('No::Such::Module::Here')) ~~ Nil;

# 5. next()/last() call form + the R, idiom
my @kept = gather for 1..4 -> $x {
    next() R, $ = "note" if $x %% 2;
    take $x;
}
@fail.push('next-paren-idiom') unless @kept eqv [1, 3];
my @stopped = gather for 1..5 -> $x { last() if $x == 3; take $x }
@fail.push('last-paren') unless @stopped eqv [1, 2];

# 6. Any.hash
my $undef;
@fail.push('any-hash') unless $undef.hash.keys.elems == 0;

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
