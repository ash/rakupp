# Regression: the IO::Spec path grammar, and a run of value-identity corners.
#   * `catdir`/`catfile` take `*@parts`, so ONE list argument spreads into N parts.
#     rakupp stringified it instead, and a Value::toStr of an Array joins with a
#     SPACE — so `<foo/bar ber raku>` became one segment "foo/bar ber raku".
#   * `splitdir`/`splitpath` answer a List, not an Array (Value::array leaves
#     isList false, and isList is what makes .raku print parens).
#   * `IO::Spec::*.split` answers an IO::Path::Parts; rakupp already had the class
#     and the three split bodies just never tagged their hash with it.
#   * a trailing "."/".." joins the DIRNAME only once a separator has matched —
#     Perl's File::Spec regex requires one — so a bare "." splits as ("", "", ".").
#   * `.Numeric`/`.Real` on an already-numeric value answer THEMSELVES; going
#     through toNum() forced Int and Rat to Num.
#   * `.chomp` removes a logical newline: "\n", "\r\n" or a lone "\r".
#   * a Range's `.WHICH` is its gist, exclusion markers and all.
#   * a 1-ary `.sort` block is a KEY EXTRACTOR, run once per element — calling it
#     inside the comparator ran it O(n log n) times and made the documented
#     `(0..0x1FFFF).sort(*.uniname.chars)` take 49 seconds against Rakudo's 1.2.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# catdir / catfile flatten one level
check(IO::Spec::Unix.catdir(<foo/bar ber raku>), 'foo/bar/ber/raku', 'catdir spreads a list argument');
check(IO::Spec::Unix.catfile(<a b>, 'c'),        'a/b/c',            'and so does catfile');
check(IO::Spec::Win32.catdir(<a b>, 'c'),        'a\b\c',            'Win32 too');
check(IO::Spec::Unix.catdir('x', 'y'),           'x/y',              'plain arguments are unaffected');

# splitdir / splitpath are Lists
check(IO::Spec::Unix.splitdir('a/b').raku,   '("a", "b")',        'splitdir is a List');
check(IO::Spec::Unix.splitpath('a/b').raku,  '("", "a/", "b")',   'and so is splitpath');
check(IO::Spec::Unix.splitdir('a/b').^name,  'List',              'by name too');

# the trailing-dot rule needs a separator
check(IO::Spec::Unix.splitpath('.').raku,     '("", "", ".")',   'a bare . is the file');
check(IO::Spec::Unix.splitpath('..').raku,    '("", "", "..")',  'and a bare ..');
check(IO::Spec::Unix.splitpath('foo/.').raku, '("", "foo/.", "")', 'but with a separator it joins the dir');

# split answers an IO::Path::Parts
check(IO::Spec::Unix.split('/a/b').^name, 'IO::Path::Parts', 'split is an IO::Path::Parts');
check(IO::Spec::Unix.split('/a/b').raku,  'IO::Path::Parts.new("","/a","b")', 'which renders as its constructor');
# ('\\' is ONE backslash in a Raku single-quoted string, so the doubled form the
# renderer must produce is spelled explicitly rather than escaped inline)
check(IO::Spec::Win32.split('C:\a\b').raku,
      'IO::Path::Parts.new("C:","' ~ '\\' x 2 ~ 'a","b")',
      'and escapes a backslash like any Str literal');

# numeric identity
check((-4/3).Real.^name, 'Rat', '.Real keeps a Rat a Rat');
check(3.Numeric.^name,   'Int', 'and an Int an Int');
check((1e0).Real.^name,  'Num', 'a Num stays a Num');
check(('a'..'d').is-int.gist, 'False', 'a Str range is not integer-bounded');
check((1..5).is-int.gist,     'True',  'a numeric one is');
check("foo".UInt.^name, 'Failure', 'a non-numeric string UInts to a Failure');
check("7".UInt,         '7',       'a numeric one does not');

# chomp takes any logical newline
check("def\r\n".chomp, 'def', 'chomp removes CRLF');
check("foo\r".chomp,   'foo', 'and a lone CR');
check("x\n".chomp,     'x',   'and a plain LF');
check("keep".chomp,    'keep','and leaves the rest alone');

# Range identity, junction control flow, Pair.freeze
check((1..2).WHICH,     'Range|1..2',   'a Range identifies by its gist');
check((1^..^5).WHICH,   'Range|1^..^5', 'exclusion markers included');
sub jr { (1|2|3).return }
check(jr().gist, 'any(1, 2, 3)', 'return does not autothread a junction');
my $v = 'B'; my $p = a => $v; $p.freeze; $v = 'C';
check($p.gist, 'a => B', 'freeze snapshots the value');

# a 1-ary sort block is a key extractor
check(<bb a ccc>.sort(*.chars).gist, '(a bb ccc)', 'sort by an extracted key');
check((1..5).sort({ -$_ }).gist,     '(5 4 3 2 1)', 'and a negating one');
check((1..5).sort({ $^a <=> $^b }).gist, '(1 2 3 4 5)', 'a 2-ary block is still a comparator');
my $calls = 0;
my @sorted = <dd a ccc bb>.sort({ $calls++; .chars });
check(@sorted.gist, '[a dd bb ccc]', 'the sort is correct and stable');
check($calls, '4', 'and the key ran once per element');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
