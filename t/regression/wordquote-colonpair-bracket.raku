# Issue #231: in a word list with quote protection — `« »`, `<< >>`, qww,
# qqww, Qww, q:ww, Q:ww — a colonpair is CODE, and its value group belongs to
# it: `« :x[1, 2] »` is :x([1, 2]). rakupp ended the word at the first blank,
# so the pair never formed: PDF::Content's
# `my Array enum ColorName « :Aqua[0, 1, 1] … »` died on a type check, and an
# untyped enum or a hash silently took ":a[1," and "2]" for its names.
#
# A pair may start mid-word (`a:b` is ("a", :b)), the next character after
# its value starts a new word (`:x[1,2]y` is (:x([1, 2]), "y")), and its value
# is the ordinary colonpair's — parsed by the same code now — in qww too,
# where bare words do not interpolate. Without quote protection (qw, q:w,
# qq:w, `<…>`) the words still split at blanks. Found on the way, in every
# colonpair: `:x<1/2>` is a RatStr, `:x<<a>>` the word "a", `:x«a :b»` holds a
# pair and splits what it interpolates, and `:!x[1]` / `:42x<a>` are the
# compile errors Rakudo makes them (they indexed the pair at run time).
#
# Contract: exit 0 + last line PASS. Every answer is Rakudo 2026.09's — run
# this file under `rakudo` too.
my @fail;
sub check($what, Mu $got, Mu $want) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }
sub err($code) { try EVAL $code; $! ?? $!.^name !! 'lived' }

# the issue's repro, and the value groups with blanks in them
check '« :x[1, 2] ».elems', « :x[1, 2] ».elems, 1;
check '« :x[1, 2] »', « :x[1, 2] ».raku, ':x([1, 2])';
check '« :x[1, 2] y »', « :x[1, 2] y ».raku, '(:x([1, 2]), "y")';
check '« :a-b[1, 2] »', « :a-b[1, 2] ».key, 'a-b';
check '« :måndag[1, 2] »', « :måndag[1, 2] ».key, 'måndag';
check '« :x<1 2> »', « :x<1 2> ».value.raku, '(IntStr.new(1, "1"), IntStr.new(2, "2"))';
check '« :x(1 + 2) »', « :x(1 + 2) ».value, 3;
check '« :x(1, 2) »', « :x(1, 2) ».value.raku, '(1, 2)';
check '« :x["a b", 2] »', « :x["a b", 2] ».value.raku, '["a b", 2]';
check '« :x[1, "]", 2] »', « :x[1, "]", 2] ».value.raku, '[1, "]", 2]';
check '« :x[1, [2, 3]] »', « :x[1, [2, 3]] ».value.raku, '[1, [2, 3]]';
my $v = 7;
check '« :x[$v, 1] »', « :x[$v, 1] ».value.raku, '[7, 1]';
my @a = 7, 8;
check '« :x[@a, 1] »', « :x[@a, 1] ».value.raku, '[[7, 8], 1]';
check '« :x{ 1 + 1 } » is a Block', « :x{ 1 + 1 } ».value ~~ Block, True;
check '…that runs', « :x{ 1 + 1 } ».value.(), 2;
check '« :x{a => 1} » is a Hash', « :x{a => 1} ».value.raku, '{:a(1)}';
check '« :x<<1 2>> »', « :x<<1 2>> ».value.raku, '(IntStr.new(1, "1"), IntStr.new(2, "2"))';
check '« :x« a b » »', « :x« a b » ».value.raku, '("a", "b")';
check '« :x(«a b») »', « :x(«a b») ».value.raku, '("a", "b")';
check '<< :x[1, 2] >>', << :x[1, 2] >>.raku, ':x([1, 2])';
check '« :x[1,2] » (no blank)', « :x[1,2] ».raku, ':x([1, 2])';
check '« :x[1, 2] » eqv (:x[1, 2])', « :x[1, 2] » eqv (:x[1, 2]), True;
check '« foo[1, 2] » still splits', « foo[1, 2] ».raku, '("foo[1,", "2]")';

# where the pair word ends
check '« :x[1,2]y »', « :x[1,2]y ».raku, '(:x([1, 2]), "y")';
check '« :x[1, 2].raku »', « :x[1, 2].raku ».raku, '(:x([1, 2]), ".raku")';
check '« :x[1, 2],y »', « :x[1, 2],y ».raku, '(:x([1, 2]), ",y")';
check '« :x[1, 2], :y[3, 4] »', « :x[1, 2], :y[3, 4] ».raku, '(:x([1, 2]), ",", :y([3, 4]))';
check '« :x[1, 2]:y[3, 4] »', « :x[1, 2]:y[3, 4] ».raku, '(:x([1, 2]), :y([3, 4]))';
check '« :x[1][0] »', « :x[1][0] ».raku, '(:x([1]), "[0]")';
check '« :x<a>y »', « :x<a>y ».raku, '(:x("a"), "y")';
check '« :x-1[2] »', « :x-1[2] ».raku, '(:x, "-1[2]")';
check '« :x\ y »', « :x\ y ».raku, '(:x, "y")';

# pairs that begin inside a word, and chains
check '« a:b »', « a:b ».raku, '("a", :b)';
check '« a:b[1, 2] »', « a:b[1, 2] ».raku, '("a", :b([1, 2]))';
check '« x:y[1, 2]z »', « x:y[1, 2]z ».raku, '("x", :y([1, 2]), "z")';
check '« a:x(1)b »', « a:x(1)b ».raku, '("a", :x(1), "b")';
check '« :x:y »', « :x:y ».raku, '(:x, :y)';
check '« :x:!y:3z »', « :x:!y:3z ».raku, '(:x, :!y, :z(3))';
check '« a=:b »', « a=:b ».raku, '("a=", :b)';
check '« [:b] »', « [:b] ».raku, '("[", :b, "]")';
check '« "a":b »', « "a":b ».raku, '("a", :b)';
check '« a{$v}:b »', « a{$v}:b ».raku, '("a", IntStr.new(7, "7"), :b)';
check '« Foo::Bar »', « Foo::Bar ».raku, '("Foo:", :Bar)';
check '« :Foo::Bar »', « :Foo::Bar ».raku, '(:Foo, ":", :Bar)';
check '« ::x »', « ::x ».raku, '(":", :x)';
check '« a:42b »', « a:42b ».raku, '("a", :b(42))';
my $x = 5;
check '« :$x »', « :$x ».raku, ':x(5)';
check '« a:$v »', « a:$v ».raku, '("a", :v(7))';
my %h = a => 1;
check '« :%h<a> »: a sigil takes no group', « :%h<a> ».raku, '(:h({:a(1)}), "<a>")';
check '« :$x(1) »', « :$x(1) ».raku, '(:x(5), "(1)")';

# …and what stays text
check '« http://example.com »', « http://example.com ».raku, '"http://example.com"';
check '« 12:30 »', « 12:30 ».raku, '"12:30"';
check '« a\:b »', « a\:b ».raku, '"a:b"';
check '« a\\:b »', « a\\:b ».raku, '("a\\\\", :b)';
check '« a:: »', « a:: ».raku, '"a::"';
check '« :x [1, 2] »: a blank, no value', « :x [1, 2] ».raku, '(:x, "[1,", "2]")';
my $s = ":x[1, 2]";
check '« $s »: syntax, not interpolation', « $s ».raku, '(":x[1,", "2]")';
check '« a$v:b » names a variable', err('my $v = 7; « a$v:b »'), 'X::Undeclared';
check 'qqww« a$v:b » too', err('my $v = 7; qqww« a$v:b »'), 'X::Undeclared';

# the other quote-protected forms, and the ones without protection
check 'qww« :x[1, 2] »', qww« :x[1, 2] ».raku, ':x([1, 2])';
check 'Qww« :x[1, 2] »', Qww« :x[1, 2] ».raku, ':x([1, 2])';
check 'qqww« :x[1, 2] »', qqww« :x[1, 2] ».raku, ':x([1, 2])';
check 'qq:ww« :x[1, 2] »', qq:ww« :x[1, 2] ».raku, ':x([1, 2])';
check 'Q:ww« :x[1, 2] »', Q:ww« :x[1, 2] ».raku, ':x([1, 2])';
check 'q:ww/:x[1, 2]/', q:ww/:x[1, 2]/.raku, ':x([1, 2])';
check 'qww/:x[1, 2] y/', qww/:x[1, 2] y/.raku, '(:x([1, 2]), "y")';
check 'qww[ :x[1, 2] y ]', qww[ :x[1, 2] y ].raku, '(:x([1, 2]), "y")';
check 'qww{ :x[1, 2] y }', qww{ :x[1, 2] y }.raku, '(:x([1, 2]), "y")';
check 'qww< :x<1 2> y >', qww< :x<1 2> y >.raku, '(:x((IntStr.new(1, "1"), IntStr.new(2, "2"))), "y")';
check 'qww« a$v » keeps $v', qww« a$v ».raku, '("a\$v",)';
check 'qww« :x[$v, 1] » is code', qww« :x[$v, 1] ».raku, ':x([7, 1])';
check 'qww« :x{1+1} » too', qww« :x{1+1} ».value.(), 2;
check 'qww« :x<<$v 2>> »', qww« :x<<$v 2>> ».raku, ':x((IntStr.new(7, "7"), IntStr.new(2, "2")))';
check 'qww« :x<$v 2> »', qww« :x<$v 2> ».raku, ':x(("\$v", IntStr.new(2, "2")))';
check 'qww« :$x »', qww« :$x ».raku, ':x(5)';
check 'qww« a:b »', qww« a:b ».raku, '("a", :b)';
check 'qww« :x:y »', qww« :x:y ».raku, '(:x, :y)';
check "qww« 'a':b »", qww« 'a':b ».raku, '("a", :b)';
check 'qww« a$v:b »: no interpolation, so a pair', qww« a$v:b ».raku, '("a\$v", :b)';
check 'Qww« a\:b »: no escapes either', Qww« a\:b ».raku, '("a\\\\", :b)';
check 'qqww« :x[1] $_ »', (qqww« :x[1] $_ » given "p q").raku, '(:x([1]), "p", "q")';
my $text = qqww«
  :a[1,
     2]
  b
»;
check 'a pair over two lines', $text.List.raku, '(:a([1, 2]), "b")';
check 'q:w« :x[1, 2] » splits', q:w« :x[1, 2] ».raku, '(":x[1,", "2]")';
check 'qq:w« :x[1, 2] » splits', qq:w« :x[1, 2] ».raku, '(":x[1,", "2]")';
check 'qw/:x[1, 2]/ splits', qw/:x[1, 2]/.raku, '(":x[1,", "2]")';
check 'qqw/:x[1, 2]/ splits', qqw/:x[1, 2]/.raku, '(":x[1,", "2]")';
check '<:x[1, 2]> splits', <:x[1, 2]>.raku, '(":x[1,", "2]")';

# a lone pair is that Pair, a positional one; a lone word stays as it was
check 'qww« :x »', qww« :x ».raku, ':x';
check 'qqww« :x »', qqww« :x ».raku, ':x';
check '« :x »', « :x ».raku, ':x';
check '« :x[1, 2] ».WHAT', « :x[1, 2] ».WHAT, Pair;
check 'qww« a »', qww« a ».raku, '("a",)';
check 'qqww« a »', qqww« a ».raku, '("a",)';
check '« a »', « a ».raku, '"a"';
sub f(*@p, *%n) { "{@p.raku} | {%n.raku}" }
check 'f(« :x[1, 2] »)', f(« :x[1, 2] »), '[:x([1, 2])] | {}';
check 'f(qww« :x[1] »)', f(qww« :x[1] »), '[:x([1])] | {}';
check 'f(« :x(1) :y(2) »)', f(« :x(1) :y(2) »), '[:x(1), :y(2)] | {}';

# a «…» subscript is such a list too; a word list split at its blanks
# (`:x[1,` and `2]`) holds no pair
my %sub = a => 1, b => 2, "a:b" => 3;
check '%sub« a b »', %sub« a b ».raku, '(1, 2)';
check '%sub« a:b »', %sub« a:b ».raku, '(1, Any)';
check '%sub.« a :b »', %sub.« a :b ».raku, '(1, Any)';
check '%sub« :x[1, 2] »', %sub« :x[1, 2] ».raku, 'Any';
check '<<:x>>', <<:x>>.raku, ':x';
check '«:x(1)»', «:x(1)».raku, ':x(1)';

# enums and hashes from word-quoted pairs
enum E1 « :a[1, 2] :b[3, 4] »;
check 'enum E1 keys', E1.enums.keys.sort.join(','), 'a,b';
check 'enum E1 a', E1.enums<a>.raku, '[1, 2]';
my Array enum ColorName «
    :Aqua[0, 1, 1]      :Black[0, 0, 0]
    :Gray[.5, .5, .5]
   »;
check 'PDF::Content ColorName', ColorName.enums.keys.sort.join(','), 'Aqua,Black,Gray';
check '…Gray', Gray.value.raku, '[0.5, 0.5, 0.5]';
enum Loglevels <<:TRACE(1) DEBUG INFO>>;
check 'Log::Async levels', (TRACE, DEBUG, INFO)».value, (1, 2, 3);
my %hh = « :x[1, 2] :y[3, 4] »;
check 'hash keys', %hh.keys.sort.join(','), 'x,y';
check 'hash x', %hh<x>.raku, '$[1, 2]';

# every colonpair: allomorphs, «…» values, and the refused arguments
check ':x<1/2> is a RatStr', (:x<1/2>).raku, ':x(RatStr.new(0.5, "1/2"))';
check '« :x<1/2> »', « :x<1/2> ».value.WHAT, RatStr;
check ':x<1+2i> is a ComplexStr', (:x<1+2i>).value.WHAT, ComplexStr;
check ':x<<1/2>>', (:x<<1/2>>).raku, ':x(RatStr.new(0.5, "1/2"))';
check ':x<<a>>', (:x<<a>>).raku, ':x("a")';
check ':x«a :b»', (:x«a :b»).raku, ':x(("a", :b))';
my $pq = "p q";
check ':x<<a $pq>> splits $pq', (:x<<a $pq >>).raku, ':x(("a", "p", "q"))';
check '« :!x[1, 2] »', err('« :!x[1, 2] »'), 'X::Syntax::NegatedPair';
check ':!x[1, 2]', err(':!x[1, 2]'), 'X::Syntax::NegatedPair';
check ':!x<a>', err(':!x<a>'), 'X::Syntax::NegatedPair';
check 'qww« :!x{1} »', err('qww« :!x{1} »'), 'X::Syntax::NegatedPair';
check '« :42name[1, 2] »', err('« :42name[1, 2] »'), 'X::Comp::AdHoc';
check ':42x<a>', err(':42x<a>'), 'X::Comp::AdHoc';
try EVAL ':42x(1)';
check '…worded as Rakudo', $!.message, 'Extra argument not allowed; pair already has argument of 42';

.say for @fail;
say @fail ?? "FAIL ({+@fail})" !! 'PASS';
exit @fail ?? 1 !! 0;
