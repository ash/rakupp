# Issue #138: `/"x {NAME} y"/` — a `{ … }` inside a double-quoted regex atom.
#
#   The block is qq interpolation, evaluated by the pass that also reads `$var`
#   atoms. That pass ran only when the pattern held a `$`, so `{$name}`
#   matched and `{NAME}` (a constant, a sigilless name, `{1+2}`) reached the
#   engine unevaluated and never matched. `s///` has a pass of its own that
#   read every `{` as code, so it missed both spellings; a value spliced in
#   was read again (`$`, `{`, `@` in it); and a regex value reached through
#   `<$r>` or `.subst-mutate` looked for its variables in the caller's scope.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

constant NAME = 'abc';
my $name = 'abc';

# the issue's rows
check so('x abc y' ~~ /"x {NAME} y"/),      True, 'constant';
check so('x abc y' ~~ /"x {$name} y"/),     True, 'variable';
check so('x abc y' ~~ /"x {~NAME} y"/),     True, 'prefix ~';
check so('x abc y' ~~ /"x {NAME.Str} y"/),  True, '.Str';
check so('x abc y' ~~ /"x {(NAME)} y"/),    True, 'parenthesized';
check so('x 3 y'   ~~ /"x {1+2} y"/),       True, 'an expression';
check so('x abd y' ~~ /"x {NAME} y"/),      False, 'a different text does not match';
my \sl = 'abc';
check so('x abc y' ~~ /"x {sl} y"/),        True, 'sigilless';

# every entry point
check 'x abc y'.match(/"x {NAME} y"/).Str,           'x abc y', '.match';
check 'x abc y x abc y'.comb(/"x {NAME} y"/).elems,  2,         '.comb';
check 'x abc y'.subst(/"{NAME}"/, 'Z'),              'x Z y',   '.subst';
check 'x abc y'.split(/"{NAME}"/).List,              ('x ', ' y'), '.split';
check so('ABC' ~~ m:i/"{NAME}"/),                    True,      'm:i';
check so('xabc' ~~ / x "{NAME}" | y /),              True,      'in an alternation';
check ('abc', 'def').grep(/"{NAME}"/).elems,         1,         'grep';
my $rx = rx/"{NAME}"/;
check so('abc' ~~ $rx),                              True,      'rx// in a variable';
my regex R { "x {NAME} y" }
check so('x abc y' ~~ /<R>/),                        True,      'my regex';
grammar G { token TOP { "x {NAME} y" } }
check so(G.parse('x abc y')),                        True,      'grammar token';

# s/// — neither spelling matched
my $s;
$s = 'x abc y'; $s ~~ s/"{NAME}"/W/;      check $s, 'x W y',     's/// constant';
$s = 'x abc y'; $s ~~ s/"{$name}"/W/;     check $s, 'x W y',     's/// variable';
$s = 'x abc y abc'; $s ~~ s:g/"{NAME}"/W/; check $s, 'x W y W',  's:g///';
$s = 'x ABC y'; $s ~~ s:i/"{NAME}"/W/;    check $s, 'x W y',     's:i///';
$s = 'x abc y'; check (S/"{NAME}"/W/ given $s), 'x W y',         'S///';
$s = "it's abc"; $s ~~ s/"it's {'abc'}"/W/; check $s, 'W',       "an apostrophe in the quoted span";

# the value is text: nothing in it is read again
my $w = 'Q';
my $dv = 'a$w';
check so('a$w' ~~ /"{$dv}"/),               True, 'a $ in the value';
my $bv = 'a{1+1}b';
check so('a{1+1}b' ~~ /"{$bv}"/),           True, 'a { in the value';
my @a = <q r>;
check so('p@a' ~~ /"{'p@a'}"/),             True, 'an @ in the value';
check so('a}b' ~~ /"{'a}b'}"/),             True, 'a } inside the block';

# a regex value reads its names where it was written
sub f1 { my constant N = 'abc'; rx/"{N}"/ }
check so('abc' ~~ f1()),                    True,  'returned rx//';
check 'xabcx'.subst(f1(), 'W'),             'xWx', 'returned rx// in .subst';
$s = 'xabcx'; $s.subst-mutate(f1(), 'W');   check $s, 'xWx', 'returned rx// in .subst-mutate';
my $r = f1();
check so('abc' ~~ /^ <$r> $/),              True,  '<$r> over a block';
check so('abc' ~~ /^ $r $/),                True,  '$r spliced';
sub g { my $x = 'abc'; rx/$x/ }
my $rg = g();
check so('abc' ~~ /^ <$rg> $/),             True,  '<$r> over a variable';

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
