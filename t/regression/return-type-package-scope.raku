# Regression: a bare `-->` type names the enclosing package's own class first.
#
# `unit class CSV::Table` declares a `class Line` of its own AND uses
# Text::Utils, which exports a Line. `sub process-header(… --> Line)` means
# CSV::Table::Line — Rakudo resolves it lexically, innermost first. This engine
# resolved it through a flat, global, first-wins alias table, so the IMPORT won
# and every call to the sub died on its own return value:
#
#     Type check failed for return value; expected Text::Utils::Line
#                                         but got CSV::Table::Line
#
# The same file got it right in TERM position — `Line.new` built the right one —
# so the sub was constructing exactly the object its own signature then rejected.
# Fixed by resolving the declared type while the package prefix is still known,
# at declaration time rather than at the call.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

my $dir = $*TMPDIR.add("retpkg-{$*PID}");
$dir.mkdir;
$dir.add('RetExp.rakumod').spurt: q:to/MOD/;
    unit module RetExp;
    class Line is export { method whose() { 'module' } }
    sub helper() is export(:helper) { 'helped' }
    MOD
# The consumer's `use` is SELECTIVE, exactly as CSV::Table's is — so Rakudo
# never has the module's Line in scope and the local one is not a redeclaration.
$dir.add('RetUse.rakumod').spurt: q:to/MOD/;
    unit class RetUse;
    use RetExp :helper;
    class Line { method whose() { 'unit' } }
    sub make(--> Line) is export { Line.new }
    method which()    { Line.^name }
    method mine(--> Line) { Line.new }
    MOD

sub run-it(Str $program) {
    my $p = run($*EXECUTABLE, '-I', ~$dir, '-e', $program, :out, :err);
    ($p.out.slurp(:close) ~ $p.err.slurp(:close)).lines.head // ''
}

check run-it('use RetUse; say make().whose'),
      'unit', 'a sub returns its own package\'s class, not the imported one';
check run-it('use RetUse; say RetUse.new.mine.whose'),
      'unit', 'and so does a method';
check run-it('use RetUse; say RetUse.new.which'),
      'RetUse::Line', 'term position agreed all along';

# The import is still reachable under its own name — this narrows resolution,
# it does not hide anything.
check run-it('use RetExp; say Line.new.whose'),
      'module', 'the imported class is still there for a file that wants it';

# best effort — Rakudo leaves a .precomp tree behind
try { .unlink for $dir.dir.grep(*.f); $dir.rmdir }

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
