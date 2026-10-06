# Issue #132: "\r\n" is ONE character, and every string method must agree.
#
#   * `.chars`, `.comb` and `.index` already treated CR LF as one grapheme, but
#     `.contains`, `.split`, literal `.subst` and `.match` found "\r" or "\n"
#     inside it. Their shared helper, atGraphemeBoundary, took any ASCII byte to
#     start a character: true except for the LF of a CR LF (GB3), and except
#     after a Prepend such as U+0600, which takes the next base into its
#     cluster (GB9b).
#   * `.starts-with` / `.ends-with` compared bytes with no boundary check at all.
#   * `.trans` stepped through the string a byte at a time.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

my $s = "a\r\nb";
check $s.chars,                     3,                 '.chars';
check $s.index("\n"),               Nil,               '.index("\n")';
check $s.contains("\r"),            False,             '.contains("\r")';
check $s.contains("\n"),            False,             '.contains("\n")';
check $s.contains("\r\n"),          True,              '.contains("\r\n")';
check $s.starts-with("a\r"),        False,             '.starts-with("a\r")';
check $s.ends-with("\nb"),          False,             '.ends-with("\nb")';
check $s.split("\n").List,          ("a\r\nb",),       '.split("\n")';
check $s.split("\r\n").List,        ("a", "b"),        '.split("\r\n")';
check $s.subst("\r", "", :g),       "a\r\nb",          '.subst("\r", "", :g)';
check $s.subst("\r\n", "\n", :g),   "a\nb",            '.subst("\r\n", "\n", :g)';
check $s.trans("\r" => ""),         "a\r\nb",          '.trans("\r" => "")';
check $s.trans("\r\n" => "|"),      "a|b",             '.trans("\r\n" => "|")';
check "a\nb".trans("\n" => "\r\n"), "a\r\nb",          '.trans("\n" => "\r\n")';
check $s.match("\n").Bool,          False,             '.match("\n")';
check "a\r\n".ends-with("\r\n"),    True,              'ends with a whole CR LF';
check "ab\r".ends-with("\r"),       True,              'a lone CR is a character';

# a Prepend joins the base after it: "\x[600]a" is one character
my $p = "\x[600]ab";
check $p.chars,                     2,                 'Prepend: .chars';
check $p.contains("a"),             False,             'Prepend: .contains';
check $p.split("a").List,           ("\x[600]ab",),    'Prepend: .split';
check $p.contains("b"),             True,              'Prepend: the next character is found';

# a combining mark is no suffix or prefix on its own
check "q\x[301]".ends-with("\x[301]"), False,          'mark: .ends-with';
check "q\x[301]x".starts-with("q"),    False,          'mark: .starts-with';
check "q\x[301]x".trans("q" => "Q"),   "q\x[301]x",    'mark: .trans';

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
