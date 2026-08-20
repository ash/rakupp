# Regression: two from the Weekly Challenge sweep, both about a character that
# closes something.
#
# 1. `<?before $>` is the END ANCHOR inside a lookahead. rakupp read the `$` as
#    the start of a variable interpolation — `$>` — so the assertion never
#    matched, and `s:g/ <alpha> <?before <upper> || ')' || '(' || $> /…/` left
#    the last element of a chemical formula untouched. With a space (`$ >`) it
#    always worked, which is what made it look like a spacing quirk.
#
# 2. A SLICE of a Match: `($str ~~ /\((.+)\)(\d+)/)[0,1]` reads two captures at
#    once. Only the single-index form was handled, so the slice answered Nil and
#    both captures came out empty.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

# --- 1. the end anchor inside an assertion ---
check ("ab" ~~ / <alpha> <?before $> /).Str, 'b', 'a lone $ closes as an anchor';
check ("ab" ~~ / <alpha> <?before <upper> || $> /).Str, 'b', '…as one alternative of several';
check ("ab" ~~ / <alpha> <?before $ > /).Str, 'b', '…and the spaced spelling still works';
check ("ab" ~~ / . <!before $> /).Str, 'a', '…negated, too';
my $s = "NaCl3Mg";
$s ~~ s:g/ <alpha> <?before <upper> || ')' || '(' || $> /{ $/ ~ 1 }/;
check $s, 'Na1Cl3Mg1', 'the substitution that found it';
# an actual variable interpolation is untouched
my $x = "b";
check ("ab" ~~ / a $x /).Str, 'ab', 'a real $var still interpolates';
check ("a>b" ~~ / a $ /).defined, False, '…and $ still means end of string';

# --- 2. Match slices ---
my $m = "ab" ~~ /(a)(b)/;
check $m[0,1]>>.Str.join(''),  'ab', 'a two-index slice';
check $m[1,0]>>.Str.join(''),  'ba', '…in the order asked for';
check $m[0..1]>>.Str.join(''), 'ab', '…from a Range';
check $m[*]>>.Str.join(''),    'ab', '…and the whole-list form';
check $m[0].Str,               'a',  'a single index is unchanged';
check ($m[5] // 'none'),       'none', 'an index past the end is Nil';
my ($k, $v) = ("(N2O)3" ~~ /\((.+)\)(\d+)/)[0,1];
check "$k/$v", 'N2O/3', 'the idiom that found it';

if @fail { note "FAILED: " ~ @fail.join('; '); say 'FAIL' } else { say 'PASS' }
