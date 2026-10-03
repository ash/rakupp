# Regression: use-ok loads a module and imports NOTHING into the caller —
# issue #124.
#
# Rakudo's use-ok is `EVAL "use $code"`: the imports land in the EVAL's scope.
# rakupp imported into the test file, so a module exporting `multi sub MAIN`
# had that MAIN RUN with the test's arguments after the plan — a CLI's
# t/00-load.t started the application. Checked in a child, with an argument the
# MAIN would answer, and with a plain exported sub that must stay invisible.
#
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

my $dir = $*TMPDIR.add("useok-noimport-{$*PID}");
$dir.mkdir;
$dir.add('UseOkMain.rakumod').spurt: q:to/M/;
    unit module UseOkMain;
    multi sub MAIN("version") is export { say "MAIN RAN: version" }
    multi sub MAIN(*@a) is export { say "MAIN RAN: default" }
    sub helper is export { 'helper' }
    M

sub child(Str $code, *@args) {
    my $p = run $*EXECUTABLE, '-I', ~$dir, '-e', $code, |@args, :out, :err;
    ($p.out.slurp(:close) ~ $p.err.slurp(:close)).lines.grep(none /^ '#'/).join("|")
}

my $out = child('use Test; plan 1; use-ok "UseOkMain"', 'version');
ck $out.contains('MAIN RAN'), False, 'an exported MAIN is not run (with an argument)';
ck so $out ~~ /^ '1..1|ok 1 '/, True, 'the test itself passes';
ck child('use Test; plan 1; use-ok "UseOkMain"').contains('MAIN RAN'), False, '…nor without one';
ck child('use Test; use-ok "UseOkMain"; done-testing; say (try ::("&helper")) // "no helper"').contains('no helper'),
   True, 'an exported sub is not visible afterwards';
ck child('use Test; use-ok "UseOkMain"; use UseOkMain; say helper(); done-testing').contains('helper'),
   True, 'a real `use` still imports';

try { $dir.add('UseOkMain.rakumod').unlink; $dir.rmdir }

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
