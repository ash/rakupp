# Conformance (behaviour matrix): three operator rules, each covering several rows
# of https://raku.online/spec/rules/divergences/
#
# 1. A non-numeric STRING operand is X::Str::Numeric, not a silent 0. Ordinary
#    arithmetic already went through numifyStrOrThrow; the bitwise/repeat/approx
#    operators kept their own toInt()/toNum() copy, which answers 0 for "a".
# 2. `+<` / `~<` after a TERM can only be the infix shift. The lexer's follow-set
#    (digit/$/(/-/*) is what keeps `+< foo bar >` a word list, but it also made
#    `1 +< "2"` lex as `+` then a word list and swallow the rest of the line.
#    The term test is deliberately narrow — a bare identifier may be a LISTOP
#    (`is-deeply ~<2>, '2'` is prefix-~ on a word list, and treating it as a shift
#    cost all 119 assertions of S02-literals/allomorphic.t).
# 3. `0..^N` renders as `^N`, for gist and .raku alike — but only for an Int zero:
#    `0.0..^5`, `0..^5e0` and `0..^0.5` all keep the long form.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }
sub throws($code, $what) {
    my $ok = False;
    { $code(); CATCH { default { $ok = True } } }
    @fail.push("$what: did not throw") unless $ok;
}

throws({ "a" +& "b" }, 'a non-numeric string in +&');
throws({ "a" +| "b" }, 'a non-numeric string in +|');
throws({ "a" +^ "b" }, 'a non-numeric string in +^');
throws({ "a" +> "b" }, 'a non-numeric string in +>');
throws({ "a" x  "b" }, 'a non-numeric repeat count');
throws({ "a" xx "b" }, 'a non-numeric list-repeat count');
throws({ "a" =~= "b" }, 'a non-numeric string in =~=');
throws({ +^ "a" },     'a non-numeric string in prefix +^');
throws({ ^ "a" },      'a non-numeric string in prefix ^');

# a NUMERIC string still works — the rule is about non-numbers
check((2 +& 3).gist, '2', 'a numeric operand is unaffected');
check(("2" +& "3").gist, '2', 'and so is a numeric string');
check(("ab" x 2), 'abab', 'a valid repeat count still repeats');

check((1 +< "2").gist, '4', 'a shift after a term takes a string operand');
check((True +< False).gist, '1', 'and a Bool one');
check((1 +< 2).gist, '4', 'the plain form is unchanged');
my @w = +<a b>;
check(@w.gist, '[2]', 'and `+<a b>` is still prefix-+ on a word list');

check((^5).gist,    '^5',     '0..^N gists as ^N');
check((0..^5).gist, '^5',     'however it was written');
check((^5).raku,    '^5',     'and .raku agrees');
check((^0).gist,    '^0',     'including zero');
check((0..5).gist,  '0..5',   'an inclusive range keeps the long form');
check((1..^5).gist, '1..^5',  'and so does a non-zero start');
check((0.0..^5).gist, '0.0..^5', 'a non-Int zero keeps the long form');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
