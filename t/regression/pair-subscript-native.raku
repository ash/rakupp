# Regression: what pair-and-list-are-immutable.raku checks, in a program the
# native backend compiles. That file declares an `Associative:D` parameter, so
# `--exe` bundles it with the interpreter and it never reached the native
# subscript and argument paths, where three things were wrong:
#
#   * `$p<a>` on a Pair read Nil: rtIndexGet had no Pair case.
#   * `f($x, (a => 1))` passed the parenthesized Pair as a NAMED argument;
#     the parens make it positional, as `f('a' => 1)` is.
#   * `$p<a> = 9` replaced the Pair with a Hash and accepted the write, where
#     Rakudo raises X::Assignment::RO.
#
# Runs under both engines: Rakudo passes every check natively.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
sub throws(&c, $type, $desc) {
    my $got = (try { c(); 'no throw' }) // $!.^name;
    @fail.push("$desc: got $got, want $type") unless $got eq $type;
}
sub args($a, $b?, *%n) { ($a, $b, %n.elems) }

my $p = (a => 1);
check $p<a>,      1,   'reading a Pair by its key';
check $p{'a'},    1,   '…with a computed key';
check $p<zz>,     Nil, 'a key it does not have';

check args($p, (a => 1)),   ($p, (a => 1), 0), 'a parenthesized Pair is positional';
check args($p, (:a(1))),    ($p, (a => 1), 0), '…in colon form too';
check args($p, 'a' => 1),   ($p, (a => 1), 0), 'a quoted key is positional';
check args($p, a => 1),     ($p, Any, 1),      'a bare one is named';

throws { my $q = (a => 1); $q<a> = 9 },     'X::Assignment::RO', 'index-assign into a Pair';
throws { my $q = (a => 1); $q<zz> = 9 },    'X::Assignment::RO', '…under a key it does not have';
throws { my %h = :a(:b(1)); %h<a><b> = 9 }, 'X::Assignment::RO', '…and through a nested one';
my $keep = (a => 1);
try { $keep<a> = 9 };
check $keep, (a => 1), 'the refused write left the Pair alone';

my $n = (pair => (is => 1));
my $bound := $n<pair>;
check $bound, (is => 1),        'binding a Pair element reaches the value';
check $n, (pair => (is => 1)),  '…and does not empty the Pair';

if @fail { .say for @fail; say "{+@fail} check(s) failed"; exit 1 }
say 'PASS';
