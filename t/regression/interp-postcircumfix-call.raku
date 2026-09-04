# `"$c(3)"` — a postcircumfix call in an interpolated string.
#
# Left open by the issue #65 fix: the lexer copied the group, but the parser's
# postfix chain accepted `[…]`, `{…}`, `<…>` and `.name(…)` only, so `"$c(3)"`
# left `(3)` as text and stringified the Callable. Rakudo applies the postfix to
# every sigil — `"@a(0)"` and `"%h(0)"` reach CALL-ME on an Array/Hash and die
# there — so the chain now commits on `(` like any other subscript.
#
# Prose in parentheses is unaffected for the ordinary reason: `(see note)` is not
# an argument list, so it fails to parse and falls back to literal text. Rakudo
# rejects those lines at compile time instead; that leniency is rakupp's own and
# is not asserted here.
#
# Contract: exit 0 + last line PASS. Every expectation here is Rakudo's.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

my $c = sub ($n) { $n + 1 };
check("$c(3)", '4', 'a sub is called');
check("$c(3) and $c(4)", '4 and 5', 'twice in one string');
check("$c.(3)", '4', 'the dotted form still works');

my $b = -> { 42 };
check("$b()", '42', 'an empty argument list');

my @f = (sub ($n) { $n * 2 }, sub ($n) { $n * 3 });
check("@f[1](5)", '15', 'a call on a subscripted element');

my $m = sub ($n) { $n.succ };
check("$m("a")", 'b', 'a quoted argument — issue #65 territory');

# a space breaks the postfix, here as everywhere in a chain
my $n = 'Bob';
check("$n (see note)", 'Bob (see note)', 'a space before the paren is text');

# calling a non-Callable dies, as in Rakudo (the wording is ours, so only the
# fact of the throw is asserted)
my $notcallable = 'x';
my $died = False;
try { my $ = "$notcallable(0)"; CATCH { default { $died = True } } }
check($died, True, 'calling a Str dies');

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
