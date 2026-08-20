# Regression: three from the Weekly Challenge sweep, all about telling two
# similar-looking spellings apart.
#
# 1. `>>[0]` is a hyper SUBSCRIPT; `>>[+]<<` is a hyper infix citing an
#    operator. rakupp read `@a>>[0]>>.Str` as the citation form — a hyper whose
#    operator was the number 0 — and answered "Unsupported operator '0'".
#
# 2. `>>=><<` is a hyper over the fat arrow. The inner scan cannot cross `>`,
#    so only the `<<=>>>` spelling was recognised; and once lexed, the metaop
#    took additive precedence (the fat arrow arrives as text, not as its own
#    token kind) and outranked the `..` on its left.
#
# 3. An explicit `m//` ALWAYS matches against `$_`, even where a bare `/…/`
#    would be the Regex object: `my $m = m/b/` is a Match, `(m:g/b/).elems`
#    counts the matches. rakupp treated both spellings alike in value,
#    argument and invocant position, so an adverbed bare match answered a
#    Regex and every method on it was "No such method for invocant of type
#    'Regex'".
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

# --- 1. hyper subscript vs hyper citation ---
my @m = [1,2], [3,4];
check @m>>[0].join(' '),        '1 3', 'a hyper subscript';
check @m>>[0]>>.Str.join(' '),  '1 3', '…chained into a hyper method call';
check (@m>>[0])>>.Str.join(' '), '1 3', '…and the parenthesised spelling';
my @one = 1, 2;
check @one>>[*-1].join(' '),    '1 2', 'a Whatever index still works';
my @a = 1, 2;
my @b = 3, 4;
check (@a >>[+]<< @b).join(' '),              '4 6', 'the operator citation still works';
check (@a >>[&infix:<+>]<< @b).join(' '),     '4 6', '…and the Callable one';

# --- 2. the hyper fat arrow ---
check (('a','b','c') >>=><< (3,2,1)).map(*.gist).join(' '), 'a => 3 b => 2 c => 1',
      'a hyper fat arrow over two lists';
check (('a'..'c' >>=><< (3...1))).map(*.gist).join(' '), 'a => 3 b => 2 c => 1',
      '…with a Range on the left, which the precedence has to keep whole';
check ((1,2) <<=>>> (3,4)).map(*.gist).join(' '), '1 => 3 2 => 4',
      '…and the other marker spelling';

# --- 3. m// always matches ---
$_ = "abcabc";
check (m/b/).Str,     'b', 'a bare m// in a call argument matches';
check (m:g/b/).elems, 2,   '…and an adverbed one counts its matches';
check (m:ex/b/).elems, 2,  '…exhaustively';
my $m = m/b/;
check $m.^name, 'Match', 'assigning m// stores the MATCH';
my $rx = /b/;
check $rx.^name, 'Regex', '…while a bare /…/ in the same place is the Regex';
check rx/b/.^name, 'Regex', '…and rx// always is';
check ("abc" ~~ m/b/).Str, 'b', 'the smartmatch form is unchanged';
check "xbx".subst(/b/, 'Y'), 'xYx', '…and a regex argument is still a Regex';

if @fail { note "FAILED: " ~ @fail.join('; '); say 'FAIL' } else { say 'PASS' }
