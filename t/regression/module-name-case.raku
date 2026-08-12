# Regression: on a case-insensitive filesystem (APFS, NTFS) the module
# resolver's plain ifstream open let `use Config` load a stray `config.raku`
# from the search path — a demo grammar named config.raku silently SHADOWED
# the installed ecosystem Config dist ("No such method 'read' for invocant
# of type 'Config'" from showcase/modinfo was the symptom). Module names are
# case-sensitive: both resolver loops now compare the actual directory entry
# byte-exact (realpath does NOT case-correct on APFS; readdir does not lie).
#
# The contract is portable: on a case-SENSITIVE filesystem the mismatched
# file is simply never found, so "must not resolve" holds everywhere — and
# Rakudo refuses it too (its resolution is name-exact via metadata).
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check(Bool() $cond, $desc) {
    @fail.push($desc) unless $cond;
}

my $tmp = $*TMPDIR.add("case-test-$*PID");
my $lib = $tmp.add('lib');
$lib.mkdir;
LEAVE { run 'rm', '-rf', $tmp.Str }

$lib.add('casemod.rakumod').spurt('unit module CaseMod; sub cm() is export { "lower" }');
$lib.add('CaseGood.rakumod').spurt('unit module CaseGood; sub cg() is export { "good" }');

my $exe = $*EXECUTABLE.absolute;

my $wrong = run $exe, '-I', $lib.Str, '-e', 'use CaseMod; say cm()', :out, :err;
$wrong.out.slurp(:close);
$wrong.err.slurp(:close);
check $wrong.exitcode != 0,
      'a case-mismatched module file does not satisfy a use';

my $right = run $exe, '-I', $lib.Str, '-e', 'use CaseGood; say cg()', :out, :err;
my $out = $right.out.slurp(:close);
$right.err.slurp(:close);
check $right.exitcode == 0 && $out eq "good\n",
      'the exactly-named module still loads';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
