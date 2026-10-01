# Regression: the "Use of uninitialized value" warning named no container and
# said nowhere — Rakudo's names the variable or element the value was read from
# (`$u`, `@a[2]`, `%h{'k'}`, `element` for an operand of `~`) and ends with the
# routine and line, as `warn` does. Numeric contexts (`$u + 1`, `$u.Numeric`,
# `abs($u)`) did not warn at all, `Int ~ "x"` parsed as the coercion
# `Int(~"x")`, and `Int + 1` answered 1 where a numeric type object is no number.
# Expectations are Rakudo 2026.09's stderr, verbatim.
# Contract: exit 0 + last line PASS.
my @fail;
my constant M = 'Methods .^name, .raku, .gist, or .say can be used to stringify it to something meaningful.';
sub str-warn($what, $where = 'block <unit>') {
    "Use of uninitialized value {$what ?? "$what " !! ''}of type Any in string context.\n{M}\n  in $where at -e line 1\n"
}
sub num-warn($what, $type = 'Any') {
    "Use of uninitialized value {$what ?? "$what " !! ''}of type $type in numeric context\n  in block <unit> at -e line 1\n"
}
sub stderr-of($code) {
    my $p = run $*EXECUTABLE, '-e', $code, :out, :err;
    $p.out.slurp(:close);
    $p.err.slurp(:close)
}
for Q{my $u; my $s = "a $u b"}                    => str-warn('$u'),
    Q{my $u; my $s = ~$u}                         => str-warn('$u'),
    Q{my $u; my $s = $u.Str}                      => str-warn('$u'),
    Q{my @a; my $s = "x @a[2] y"}                 => str-warn('@a[2]'),
    Q{my %h; my $s = "x %h<k> y"}                 => str-warn(Q{%h{'k'}}),
    Q{sub f($x) { "$x" }; f(Any)}                 => str-warn('', 'sub f'),
    Q{sub f($x is rw) { "$x" }; my $v; f($v)}     => str-warn('$v', 'sub f'),
    Q{my $u; my $s = $u ~ "x"}                    => str-warn('element'),
    Q{my @a = 1, Any; my $s = ~@a}                => str-warn('@a'),
    Q{my $u; my $b = $u eq "a"}                   => str-warn('$u'),
    Q{my $u; my $n = $u + 1}                      => num-warn('$u'),
    Q{my $u; my $n = $u div 2}                    => num-warn(''),
    Q{my $u; my $n = abs($u)}                     => num-warn('$u'),
    Q{my $u; my $n = $u.Numeric}                  => num-warn('$u'),
    Q{my $i; $i += 1; my $l; $l ~= "a"; $i++}     => '' -> (:key($code), :value($want)) {
    my $got = stderr-of($code);
    @fail.push("$code\n  got:  {$got.raku}\n  want: {$want.raku}") unless $got eq $want;
}
# `Int ~ "x"` is the infix on the type object, not `Int(~"x")`
my $cat = run $*EXECUTABLE, '-e', 'print Int ~ "x"', :out, :err;
@fail.push("Int ~ \"x\" printed {$cat.out.slurp(:close).raku}") unless $cat.out.slurp(:close) eq 'x';
$cat.err.slurp(:close);
# …and a numeric type object is no number
my $die = run $*EXECUTABLE, '-e', 'my $x = Int + 1; CATCH { default { print .^name } }', :out, :err;
my $name = $die.out.slurp(:close); $die.err.slurp(:close);
@fail.push("Int + 1 gave $name") unless $name eq 'X::Numeric::Uninitialized';

say @fail ?? "FAIL:\n" ~ @fail.join("\n") !! "PASS";
exit @fail ?? 1 !! 0;
