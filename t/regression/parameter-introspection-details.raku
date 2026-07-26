# Regression: the corners of Parameter introspection, and Match.target.
#   * `.params` SKIPPED the invocant entirely, so `.invocant` could never answer
#     True. `.arity`/`.count` still exclude it, which is why including it is
#     safe: they count what a caller passes, not what the signature lists.
#   * `.usage-name` strips the TWIGIL as well as the sigil, so the dynamic
#     `Str @*l` is usable as plain `l`.
#   * a parameter with no default answers the Code TYPE OBJECT from `.default`,
#     which is the attribute's declared type — not a bare Any.
#   * `.target` on a Match is `.orig` under its Cursor-era name.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# usage-name drops sigil and twigil
my $sig = :(Str $x, Str @*l, Bool);
check($sig.params[0].usage-name, 'x', 'a plain parameter');
check($sig.params[1].usage-name, 'l', 'a dynamic one loses its twigil too');
check($sig.params[2].usage-name, '',  'an anonymous one has no name');
check($sig.params[0].name,       '$x', 'but .name keeps the sigil');

# the invocant is a parameter
my $m = :($i : Str $x is rw, Bool :$is-named);
check($m.params[0].invocant, 'True',  'the invocant reports itself');
check($m.params[1].invocant, 'False', 'and an ordinary parameter does not');
check($m.params[0].name,     '$i',    'it keeps its name');
check($m.params[1].rw,       'True',  'the traits of the others are unaffected');
check($m.params[2].named,    'True',  'and so is namedness');
# arity/count still describe what a CALLER passes
check(:($a, $b).arity, '2', 'arity');
check(:($a, $b).count, '2', 'count');
check(:($a, $b = 1).arity, '1', 'an optional does not count toward arity');

# .default with and without one
my $d = :($a, $b = 12);
check($d.params[0].default.gist, '(Code)', 'no default is the Code type object');
check($d.params[1].default.(),   '12',     'a default is a thunk that yields it');
check($d.params[1].optional,     'True',   'and makes the parameter optional');

# Match.target
my $match = "þor" ~~ /o/;
check($match.target, 'þor', 'target is the original string');
check($match.orig,   'þor', 'and orig agrees');
check($match.Str,    'o',   'while .Str is what matched');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
