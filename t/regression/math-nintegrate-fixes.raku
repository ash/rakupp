# Math::NIntegrate (antononcube) blocked on one parse error and ran two of its
# test files wrong. Each is a general bug:
#
#   * a quote inside a CHARACTER CLASS inside a regex inside a regex's code
#     block — `<!{ $0.Str ~~ / 'Rule' <["']>? $/ }>` — opened a string in the
#     scanner that finds the end of the outer regex, so it ran to the end of
#     the file ("Missing block"). All three scanners (token/rule/regex
#     declarations, rx{…}/m{…}, and bare /…/) had it.
#   * `:0dimension`: after a pair's `:` the value is `\d+` and the rest is the
#     key, but `0d` was read as a decimal radix prefix ("Malformed radix
#     number"). Likewise `:0xab` is xab => 0 and `:0e5` is e5 => 0.
#   * `{0 <= $^x < 0.3 ?? 0 !! 1}`: a placeholder inside a CHAINED comparison
#     was not collected, so the block had arity 0 and $^x was undefined.
#   * `<x -Inf Inf>` — the module's own spelling of an infinite range — left
#     `-Inf` and `Inf` as Str: an angle word that spells Inf (with a sign) or
#     NaN is a NumStr, as val() already said, and a leading U+2212 is a minus.
#   * `{ $^x + $^y }(1)` bound $^y to Any instead of dying "Too few
#     positionals"; `{ $^x }(1, 2)` did not die "Too many"; and `@_` / `%_`
#     beside placeholders in a bare block got every argument, not the rest.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

# a quoted character inside a code block's regex's character class
grammar Spec {
    token symbol { <[\w \-]>+ }
    rule strategy { (<symbol>) <!{ $0.Str ~~ / [ 'Rule' | '-rule' ] <["']>? $/ }>}
    token hashy { \w+ <?{ "a#b" ~~ / <-[#"]>+ / }> }
    token setop { \w+ <?{ "a" ~~ / <[a]+["]> / }> }
}
check Spec.parse('global-adaptive', :rule<strategy>).so, True, 'rule: strategy name accepted';
check Spec.parse('gauss-rule', :rule<strategy>).so, False, 'rule: rule name refused';
check Spec.parse('x', :rule<hashy>).so, True, 'token: # and " inside a class';
check Spec.parse('x', :rule<setop>).so, True, 'token: class set operation';
check so("ab" ~~ / a <?{ '"' ~~ / <["']> / }> b /), True, 'bare /…/ scanner';
check so("ab" ~~ rx{ a <?{ '"' ~~ / <["']> / }> b }), True, 'rx{…} scanner';
check so("ab" ~~ / a <!{ '"' ~~ / <["']> / }> b /), False, 'negated assertion';

# numeric colon pairs
check (:0dimension), (dimension => 0), ':0dimension';
check (:0xab), (xab => 0), ':0xab';
check (:0bits), (bits => 0), ':0bits';
check (:0e5), (e5 => 0), ':0e5';
check (:0x10), (x10 => 0), ':0x10';
check (:1_000th), (_000th => 1), ':1_000th';
check (:0out-buffer), ('out-buffer' => 0), ':0out-buffer';
check (:12days), (days => 12), ':12days';
check :2<101>, 5, ':2<101> is still a radix number';
check 0x1f, 31, '0x1f is still hex';
sub takes(:$dimension) { $dimension }
check takes(:0dimension), 0, ':0dimension as a named argument';

# Inf / NaN / U+2212 in angle words
check <x -Inf Inf>.map(*.^name).List, <Str NumStr NumStr>, '<x -Inf Inf> types';
check <-Inf>.Num, -Inf, '<-Inf> value';
check <-Inf>.Str, '-Inf', '<-Inf> string';
check <+Inf>.Str, '+Inf', '<+Inf> keeps its spelling';
check <NaN>.^name, 'NumStr', '<NaN>';
check <-NaN>.^name, 'Str', '<-NaN> stays a word';
check <inf>.^name, 'Str', '<inf> stays a word';
check <−Inf>.Num, -Inf, '<−Inf> with U+2212';
my $minus5 = <−5>;
check $minus5.raku, 'IntStr.new(-5, "−5")', '<−5> with U+2212';
check (:x<Inf>).value.^name, 'NumStr', ':x<Inf>';
my Num $n = <Inf>;
check ($n ~~ Num, $n.Num), (True, Inf), 'a NumStr Inf assigns to Num';

# placeholders inside a chained comparison
my &step = {0 <= $^x < 0.3 ?? 0 !! 1};
check &step.arity, 1, 'chained comparison: arity';
check (step(0.1), step(0.5)), (0, 1), 'chained comparison: values';
check {$^a < $^b < $^c}(1, 2, 3), True, 'chained comparison: three placeholders';

# placeholder arity is enforced both ways
sub dies-with(&code) { try { code(); return 'lived' }; $!.message }
check dies-with({ { $^x + $^y }(1) }),
      'Too few positionals passed; expected 2 arguments but got 1', 'too few';
check dies-with({ { $^x }(1, 2) }),
      'Too many positionals passed; expected 1 argument but got 2', 'too many';
check dies-with({ { $^x }() }),
      'Too few positionals passed; expected 1 argument but got 0', 'none';
check { $^a + @_.sum }(1, 2, 3), 6, '@_ takes what the placeholders leave';
check { $^a, $^b; @_.elems }(1, 2, 3, 4), 2, '@_ beside two placeholders';
check { $^a; @_ }(1), [], '@_ empty when nothing is left';
check { $^a; %_ }(1, :x(2)), {x => 2}, '%_ beside a placeholder';
check (1..4).map({ $^a + $^b }).List, (3, 7), 'map by two still works';
check (1, 2, 3).reduce({ $^a + $^b }), 6, 'reduce still works';

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
