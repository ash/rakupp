# Regression: a Pair and a List accepted writes that Raku refuses, and a Pair
# was not Associative at all. Found working through Crane (issue #69), whose
# whole traversal dispatches on `Associative:D` and whose immutability tests
# check that a write into one of these FAILS.
#
# Four things were wrong, and every one of them was silent:
#
#   * `(a => 1)<a> = 9` REPLACED the Pair with a fresh Hash and accepted the
#     write. Rakudo raises X::Assignment::RO for it whether the pair holds a
#     literal or a container.
#   * `(a => 1)<a>:delete` reached a `hash()` that is null for a Pair and took
#     the process down with it. Both a Pair and a List answer X::AdHoc.
#   * `(a => 1) ~~ Associative` was False, so a Pair in a path missed every
#     candidate of Crane's `at`/`in` family and came back X::Multi::NoMatch.
#   * `my $r := $p<key>` autovivified an empty Hash over the Pair — through the
#     binding, so it emptied the ORIGINAL — and then bound a slot of it. Crane
#     walks exactly that way (`my $root := $container; $root := $root{$step}`)
#     and lost the structure it was reading.
#
# The refusals are reported through a flag on the resolved lvalue rather than
# thrown from lvalue() itself: a throw there is swallowed whole by `return-rw`'s
# not-an-lvalue fallback, and Crane's `set` then wrote into a copy and reported
# success.
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

# --- a Pair does Associative ------------------------------------------------
my $p = (a => 1);
check ($p ~~ Associative),   True,  'a Pair does Associative';
check (Pair ~~ Associative), True,  '…and so does the type object';
check ($p ~~ Iterable),      False, 'but not Iterable';
sub takes-assoc(Associative:D $x) { 'bound' }
check takes-assoc($p), 'bound', 'a Pair binds to an Associative:D parameter';
multi sub which(Associative, $) { 'assoc' }
multi sub which(Any, $)         { 'any' }
check which(Pair, 1), 'assoc', 'the Pair TYPE dispatches as Associative';

# --- reading a Pair by key still works --------------------------------------
check $p<a>,  1,   'reading a Pair by its key';
check $p<zz>, Nil, 'reading a key it does not have';

# --- …but writing one does not ----------------------------------------------
throws { my $q = (a => 1);  $q<a> = 9 },  'X::Assignment::RO', 'index-assign into a literal Pair';
my $cont = 5;
throws { my $q = (a => $cont); $q<a> = 9 }, 'X::Assignment::RO', '…and into a container-backed one';
throws { my %h = :a(:b(1)); %h<a><b> = 9 }, 'X::Assignment::RO', '…and through a nested one';
my $keep = (a => 1);
try { $keep<a> = 9 };
check $keep, (a => 1), 'the refused write left the Pair alone';

# --- nor deleting from one, or from a List ----------------------------------
throws { my $q = (a => 1); $q<a>:delete }, 'X::AdHoc', ':delete on a Pair';
throws { my $l = (1, 2, 3); $l[0]:delete }, 'X::AdHoc', ':delete on a List';
# the WORDING is asserted by real code — Crane matches the payload with
# `Can not remove [values|elements] from a (\w+)` to rethrow its own error
my $pmsg = (try { my $q = (a => 1); $q<a>:delete; '' }) // ($! ~~ X::AdHoc ?? ($!.payload // $!.message) !! '');
check ($pmsg ~~ / 'Can not remove values from a Pair' /).Bool, True, 'the Pair payload names the type';
my $lmsg = (try { my $l = (1, 2, 3); $l[0]:delete; '' }) // ($! ~~ X::AdHoc ?? ($!.payload // $!.message) !! '');
check ($lmsg ~~ / 'Can not remove elements from a List' /).Bool, True, 'the List payload names the type';

# --- binding through a Pair reaches its value and leaves the Pair intact -----
my $n = (pair => (is => 1));
my $bound := $n<pair>;
check $bound, (is => 1), 'binding a Pair element reaches the value';
check $n, (pair => (is => 1)), '…and does not empty the Pair';
# …including when the base is itself a bound name, which is how Crane walks
my $root := $n;
$root := $root<pair>;
check $root, (is => 1), 'rebinding through a bound Pair walks one level down';
check $n, (pair => (is => 1)), '…and still leaves the original alone';

if @fail {
    note $_ for @fail;
    die "{+@fail} check(s) failed";
}
say 'PASS';
