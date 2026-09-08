# Regression: `@array` inside a regex's CODE was rewritten as if it were pattern
# (2026-09-08, found through Cro::HTTP::Router answering 404).
#
# A bare `@arr` in a pattern interpolates as an LTM alternation of its elements,
# and that rewrite is textual — but it ran over the whole pattern, including the
# regions that are Raku code: `{…}` blocks, `<?{…}>` / `<!{…}>` assertions,
# `<{…}>`, `**{…}`, and `:my …;` declarations. Code there saw `[ 'x' | 'y' ]`,
# which as Raku is an array of ONE junction, so `@arr.elems` was 1 and
# `@arr[1]` was Nil. Cro compiles each route's signature-bind check into
# `<?{ my $han = @handlers[N]; $han.signature.ACCEPTS($cap) … }>`; that read Nil,
# threw inside the assertion, and every route with a captured segment 404'd
# while the argument-less root route kept working.
#
# Both halves are checked here: code must see the real array, and a pattern must
# still interpolate one. Passes under Rakudo too.
# Contract: exit 0 + last line PASS.

my @fail;
sub check($got, $want, $what) {
    return if $got eqv $want;
    @fail.push($what);
    note "# $what: got {$got.raku}, want {$want.raku}";
}

my @arr = 1, 2, 3;
my $seen-elems;
my @seen-copy;

# --- code regions read the ARRAY ---
check(('x' ~~ / x <?{ @arr.elems == 3 }> /).so,      True,  'assertion sees .elems');
check(('x' ~~ / x <?{ @arr[1] == 2 }> /).so,         True,  'assertion indexes the array');
check(('x' ~~ / x <!{ @arr.elems == 9 }> /).so,      True,  'negated assertion sees .elems');
'x' ~~ / x { $seen-elems = @arr.elems } /;
check($seen-elems,                                   3,     'code block sees .elems');
'x' ~~ / x :my @copy = @arr; { @seen-copy = @copy } /;
check(@seen-copy.List,                               (1, 2, 3), ':my declaration copies the array');
# a quantifier bound is code as well
check(('aaa' ~~ / ^ a ** {@arr[2]} $ /).so,          True,  '**{…} bound reads an element');

# --- and a pattern still interpolates one ---
my @words = <cat catalog dog>;
check(('catalog' ~~ / ^ @words $ /).Str,             'catalog', 'bare @arr still interpolates, longest-first');
check(('dog' ~~ / ^ @words $ /).Str,                 'dog',     'bare @arr matches any element');
check(('cat' ~~ / ^ <@words> $ /).so,                True,      '<@arr> assertion form still works');
# a literal brace in a character class must not swallow the interpolation after it
check(('{dog' ~~ / ^ <[{]> @words $ /).so,           True,      'an unmatched { stays literal');

say @fail ?? "FAIL: {@fail.join('; ')}" !! 'PASS';
exit @fail ?? 1 !! 0;
