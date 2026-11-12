# Regression: shapes `--exe` compiled into a binary that answered differently
# from the interpreter — or died where it does not — and now refuses, so the
# program bundles with the interpreter and means what it says. Found by
# t/exe/parity.raku, which compares every compiled program with its
# interpreted run.
#
#   [R//]= , &[R~~], [=>]-reduce   an operator the run-time dispatch does not
#                       know went out as applyArith(op) and died "Unsupported
#                       operator"
#   `**`                a HyperWhatever compiled as a plain Whatever
#   do { sub f {…}; f() }, a class body's `sub helper` called from a method,
#   `&rule-name`, a `:filter(&f)` parameter, a `:&for` parameter
#                       a call the backend resolved to none of its own went
#                       to the built-in table: "Undefined routine"
#   temp / let          had no native form ("Undefined routine 'temp'"); in a
#                       routine compiled on its own, handing the statement to
#                       the interpreter would restore it at once, so the whole
#                       routine is interpreted
#   multi f(Mu) / multi f(Any)
#                       the native dispatcher tried candidates by how many
#                       typed parameters they have, then declaration order, so
#                       Mu was taken for every argument
#
# t/exe/parity.raku compiles this file and compares the binary with the
# interpreter. Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

# operators
my %h;
%h<x> [R//]= 5;
ck %h<x>, 5, '[R//]= into an element';
my &rsm = &[R~~];
ck rsm(Int, 3), True, '&[R~~] as a value';
ck (1, 2, 3).reduce(&[=>]).gist, '1 => 2 => 3', '&[=>] reduces right-associatively';

# HyperWhatever
my $hw = **;
ck $hw.^name, 'HyperWhatever', '`**` is a HyperWhatever';

# routines the backend could not reach
ck (do { sub f { 5 }; f() }), 5, 'a sub declared in a do block';
class C { sub helper($x) { $x + 1 }; method go { helper(1) } }
ck C.new.go, 2, "a class body's sub, from a method";
my rule two-words { ab cd }
ck so('ab cd' ~~ &two-words), True, 'a `my rule` through &name';
sub named(:filter(&f)) { f() }
ck named(filter => { 7 }), 7, 'an aliased `:filter(&f)` parameter';
sub kw(:&for) { for(2) }
ck kw(for => -> $x { "f$x" }), 'f2', 'a `:&for` parameter called';

# temp and let
our $t = 1;
sub tmp($c) { temp $t = 5 if $c; $t }
ck (tmp(True), $t), (5, 1), '`temp` in a routine is undone at its exit';
sub tmp2 { temp $t; $t = 7; $t }
ck (tmp2(), $t), (7, 1), '…and a bare `temp $x`';
my $l = 1;
sub lt { let $l = 9; fail 'undo' }
my $ = lt();
ck $l, 1, '`let` is undone when the block fails';

# multi candidates the native order got wrong
multi m(Mu $x)  { 'Mu' }
multi m(Any $x) { 'Any' }
ck (m(42), m(Int), m(1 | 2)), ('Any', 'Any', 'Mu'), 'Any is narrower than Mu';
multi n(Int $x, $y) { 'first' }
multi n($x, Int $y) { 'second' }
ck (try n('a', 1)), 'second', 'a tie on typed-parameter count is not declaration order';

say @fail ?? "FAIL\n" ~ @fail.join("\n") !! 'PASS';
