# Regression: routine introspection, `<->` blocks, Capture slipping, and two
# exception messages.
#   * `.yada` (a `{ ... }` / `{ !!! }` body) had no answer at all. `stmtIsStub`
#     existed but was a lambda LOCAL to the class-declaration case, so only
#     methods were ever asked. It is now file-scope and both the named-sub and
#     the anonymous-sub Callable builders set the flag.
#   * a statement-level `my method m(Int:D: $x) {…}` is a SubDecl with isMethod,
#     but neither the parser's statement branch nor makeCand carried the flag, so
#     callCallable never bound `self` and the invocant param found nothing —
#     `self` in the body died, and `&m.^name` answered Sub.
#   * `<->` (doubly-pointy) parsed exactly like `->` in expression position and
#     never marked its params `is rw`, so the swap assigned to copies. The
#     for-loop form got this right; the block and `whenever` forms did not.
#   * `|$capture` slipped the Capture's NAMED parts as positionals: a Capture is
#     an Array carrying hashKind "Capture" and took the plain Array branch, which
#     pushes every element verbatim. `min |\(1,7,3, by => {…})` then compared
#     four values instead of three with a `:by`.
#   * the assignment type-check message rendered the value with a bare `.gist`
#     while the BINDING message quoted and elided it — two builders that had
#     diverged. They now share one helper.
#   * `is required("reason")` dropped the reason: the parser's generic trait-
#     argument skip ate the parenthesised text. Note Rakudo's exact shape — a
#     comma and a NEWLINE before "but" when a reason is present, a comma and a
#     space when it is not.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# .yada
check((sub f() { ... }).yada.gist, 'True',  'an anonymous stub');
check((sub g() { 1 }).yada.gist,   'False', 'a real body is not a stub');
sub h() { ... }
check(&h.yada.gist,                'True',  'and a named one');
check((sub k() { !!! }).yada.gist, 'True',  '!!! counts too');

# a statement-level `my method`
my method m2(Int:D:) { 1 }
check(&m2.^name, 'Method', 'it is a Method, not a Sub');
my method m(Int:D: $b) { self.^name }
check(1.&m(<a>), 'Int', 'and self binds from the invocant');

# `<->` marks its parameters rw
my $swap = <-> $a, $b { ($a, $b) = ($b, $a) };
my ($p, $q) = (2, 4);
$swap($p, $q);
check("$p $q", '4 2', 'a doubly-pointy block writes back');
my $keep = -> $a, $b { ($a, $b) = ($b, $a) };
my ($r, $s) = (2, 4);
try { $keep($r, $s) };
check("$r $s", '2 4', 'a singly-pointy one still does not');

# |$capture slips nameds as nameds
my $y = \(1, 7, 3, by => { 1 / $_ });
check(min(|$y), '7', 'the :by named survives the slip');
sub t(*@p, *%n) { @p.raku ~ ' ' ~ %n.raku }
check(t(|\(1, 2, :x(3))), '[1, 2] {:x(3)}', 'positionals and nameds land in the right places');
check(t(|(1, 2)),          '[1, 2] {}',      'a plain list is unaffected');

# the two type-check messages agree
my $i of Int = 42;
try { $i = "forty plus two" }
check($!.Str, 'Type check failed in assignment to $i; expected Int but got Str ("forty plus two")',
      'the assignment message quotes a Str');

# is required("reason")
class RQ  { has $.a is required("it is a good idea") }
class RQ2 { has $.a is required }
try { RQ.new }
check($!.Str, "The attribute '\$!a' is required because it is a good idea,\nbut you did not provide a value for it.",
      'the reason appears, with a newline before "but"');
try { RQ2.new }
check($!.Str, "The attribute '\$!a' is required, but you did not provide a value for it.",
      'and the bare form is unchanged');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
