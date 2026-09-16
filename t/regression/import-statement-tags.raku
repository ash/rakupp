# Regression: `import Mod :tag` has to reach the module.
#
# `need Mod; import Mod :tag;` is how a program loads a module without importing
# and then asks for one selective export set — Math::Trig's own suite checks its
# `:radial` group exactly that way. This engine parsed the statement, then threw
# the list away with the comment that it was "accepted and ignored the way the
# `require` forms accept theirs", and imported only what a plain load had already
# published. Six subs never arrived.
#
# The fix routes a tagged `import` through the loader, which is idempotent for an
# already-loaded module and replays just the import step.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

my $dir = $*TMPDIR.add("imptag-{$*PID}");
$dir.mkdir;
$dir.add('ImpTag.rakumod').spurt: q:to/MOD/;
    unit module ImpTag;
    sub plain()  is export          { 'plain'  }
    sub radial() is export(:radial) { 'radial' }
    sub extra()  is export(:extra)  { 'extra'  }
    MOD

sub run-it(Str $program) {
    my $p = run($*EXECUTABLE, '-I', ~$dir, '-e', $program, :out, :err);
    ($p.out.slurp(:close) ~ $p.err.slurp(:close)).lines.head // ''
}

check run-it('need ImpTag; import ImpTag :radial; say radial()'),
      'radial', 'need + import :tag brings the tagged export in';

check run-it('need ImpTag; import ImpTag :extra; say extra()'),
      'extra', 'a different tag brings a different one';

# The plain `import Mod;` form still works — it is what the statement did before
# and the only spelling the corpus had until now.
check run-it('use ImpTag; import ImpTag; say plain()'),
      'plain', 'a bare import still imports';

# …and `need` alone still imports NOTHING, or the test above proves nothing.
check run-it('need ImpTag; say (try EVAL(q{radial()})) // "absent"'),
      'absent', 'need on its own imports nothing';

# best effort — Rakudo leaves a .precomp tree behind
try { .unlink for $dir.dir.grep(*.f); $dir.rmdir }

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
