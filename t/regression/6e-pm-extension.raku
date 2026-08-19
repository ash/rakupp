# Regression: `.pm` stopped being a module file extension in 6.e.
#
# CompUnit::Repository::FileSystem looks for .rakumod and .pm6 only from 6.e on,
# so a 6.e program cannot load a module written in a .pm file — while a 6.d one
# still can. Phase 3 of docs/dev/plans/6E-PLAN.md.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

my $dir = $*TMPDIR.add("pmext-{$*PID}");
$dir.mkdir;
$dir.add('OldSchool.pm').spurt: 'sub hi is export { say "from .pm" }' ~ "\n";

sub load(Str $pragma) {
    my $p = run($*EXECUTABLE, '-I', ~$dir, '-e', "$pragma use OldSchool; hi", :out, :err);
    ($p.out.slurp(:close) ~ $p.err.slurp(:close)).lines.head // ''
}

check load(''),                     'from .pm',            'a .pm module still loads under 6.d';
check load('use v6.e.PREVIEW;').substr(0, 14), 'Could not find', 'and is not found under 6.e';

$dir.add('OldSchool.pm').unlink;
$dir.rmdir;

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
