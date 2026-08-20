# Regression: six constructs from the Weekly Challenge sweep that rakupp either
# refused to parse or read as something else.
#
# 1. `only` is the third multiness declarator (`only task(…) {…}`), meaning the
#    routine has exactly one candidate. It parsed as a call to a sub named
#    `only`. Anonymous ones are an error, as with `multi` and `proto`.
# 2. The Unicode ellipsis carries the sequence operator's exclusion markers:
#    `5^…0` is `5^...0`, `1…^3` is `1...^3`. Only the bare `…` was recognised,
#    so a leading `^` failed to parse and a trailing one turned into a range
#    endpoint — `1…^3` quietly became `1 ... ^3`.
# 3. `∅` is the empty Set, a TERM, not an operator.
# 4. The bracketed infix citation `A [op] B` takes any operator's spelling,
#    including word forms (`[max]`, `[div]`) and a zip/cross carrying an inner
#    operator (`[Z=>]`, `[X~]`); only symbolic single tokens were accepted.
# 5. A file's own `sub infix:<…>` declaration is lexed as ONE token even when
#    it is spelled in ASCII operator characters: `sub infix:<%%%>` was lexed
#    as `%%` then `%`, so `5 %%% 2` divided by an empty hash.
# 6. A value that is already the target type is not coerced — `Mu:D(Int) $a`
#    died looking for an Int's `.Mu` method.
# …and BagHash.remove, which takes one off each named key's count.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

# --- 1. only ---
only f($x) { $x * 2 }
check f(3), 6, 'a bare `only` declares a sub';
only sub g($x) { $x + 1 }
check g(3), 4, '…and `only sub` too';
only task( Str:D $a --> Int:D ) { $a.chars }
check task("abc"), 3, '…with a full signature and a return constraint';
class K { only method m { 42 } }
check K.m, 42, '…and `only method` in a class';
{
    my $err = '';
    try { EVAL 'only sub {}'; CATCH { default { $err = .^name } } }
    check ($err eq 'X::Anon::Multi' || $err.contains('Anon')), True,
          'an anonymous `only` routine is still an error';
}
# a sub or variable NAMED `only` is untouched
sub only($x) { $x }
check only(5), 5, 'a routine named `only` still parses as a call';
my $only = 3;
check $only, 3, '…and so does a variable';

# --- 2. the Unicode ellipsis with exclusion markers ---
check (5^…0).join(' '),  '4 3 2 1 0', 'a leading ^ on the ellipsis';
check (1…^3).join(' '),  '1 2',       'a trailing one';
check (1^…^5).join(' '), '2 3 4',     'both';
check (1…3).join(' '),   '1 2 3',     'and the bare form is unchanged';

# --- 3. the empty set ---
check ∅.raku,        'set()', '∅ is the empty Set';
check (bag() ~~ ∅),  True,    '…and an empty bag smartmatches it';
check (set(1) ~~ ∅), False,   '…while a non-empty one does not';

# --- 4. bracketed infix citations ---
check (1 [+] 2),                      3,       'the symbolic form still works';
check (1 [max] 2),                    2,       'a word operator';
check (7 [div] 2),                    3,       '…and another';
check ((1,2) [Z] (3,4)).map(*.join('')).join(' '), '13 24', 'a zip';
check ((1,2) [Z=>] (3,4)).map(*.gist).join(' '), '1 => 3 2 => 4', 'a zip with an inner operator';
check ((1,2) [X~] (3,4)).join(' '),   '13 14 23 24', 'a cross with one';
my %v;
%v<a> [//]= 5;
check %v<a>, 5, 'the metaop-assignment form is unchanged';
my @sub = 1, 2;
check @sub[1], 2, 'and a subscript is not a citation';

# --- 5. a declared ASCII operator lexes whole ---
sub infix:<%%%>($a, $b) { ($a div $b), ($a % $b) }
check (5 %%% 2).join(' '), '2 1', 'a declared %%% is one operator';
check (5 %% 2),  False, '…and the built-in %% still works';
check (5 % 2),   1,     '…as does %';

# --- 6. coercion to a type the value already is ---
sub mu(Mu:D(Int) $a) { $a }
check mu(5), 5, 'Mu:D(Int) accepts an Int without coercing';
sub co(Int(Cool) $a) { $a }
check co("42"), 42, '…and a real coercion still happens';

# --- BagHash.remove ---
my $b = BagHash.new: <a a b>;
$b.remove("a");
check $b.pairs.sort.map({ .key ~ '=' ~ .value }).join(' '), 'a=1 b=1',
      'remove takes ONE off the count';
$b.remove($b.keys);
check $b.pairs.elems, 0, '…and removes the key when it reaches zero';

if @fail { note "FAILED: " ~ @fail.join('; '); say 'FAIL' } else { say 'PASS' }
