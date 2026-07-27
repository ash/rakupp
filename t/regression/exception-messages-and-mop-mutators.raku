# Regression: five exception messages, the typed-bind gate, and two MOP entries.
#   * `EVAL 'x', :lang<bar>` threw with the arguments in the wrong slots — a
#     MESSAGE string sat in the payload slot where the exception TYPE belongs.
#     A Str payload only promotes to a real exception when it starts with "X::",
#     so this one surfaced as a bare Str: `.^name` answered Str.
#   * X::NoDispatcher's message was written freehand at all five redispatch
#     sites. Rakudo interpolates $.redispatcher into "… is not in the dynamic
#     scope of a dispatcher"; the attribute is set now too.
#   * X::Str::Match::x carried a placeholder message and no `got` attribute.
#   * the type-check messages rendered the offending value five different ways.
#     Rakudo uses `.raku` — so a Str keeps its quotes, a List separates with
#     commas, a Hash uses the colon-pair form — and elides past 23 characters,
#     keeping 20 plus "...". All five builders share one helper now.
#   * the typed-bind check was gated on the RHS being a literal, but the kind
#     list had only StrLit — and a DOUBLE-quoted "foo" parses to InterpStr. So
#     `my Int $x := 'foo'` threw and `my Int $x := "foo"` silently did not. An
#     InterpStr whose parts are all literal has nothing to interpolate, so the
#     no-double-evaluation reason for the gate does not apply.
#   * `.^mixin` was not in the `.^` dispatcher, and had to answer BEFORE that
#     block collapses an object invocant to its bare type object — otherwise the
#     thing to mix into is already gone.
#   * `.^add_method` on a BUILT-IN type: the whole MOP-mutator surface was gated
#     on the invocant being user-declared. The storage and the reader already
#     existed — builtinExt_ is what `augment class Int {…}` writes to — only the
#     writer was missing.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# typed exceptions
try { EVAL 'boo', lang => "bar" };
check($!.^name, 'X::Eval::NoSuchLang', 'the EVAL :lang failure is typed');
check($!.Str, "No compiler available for language 'bar'", 'with Rakudo\'s message');

try { nextsame };
check($!.^name, 'X::NoDispatcher', 'a stray nextsame');
check($!.Str, 'nextsame is not in the dynamic scope of a dispatcher', 'and its wording');
try { callsame };
check($!.Str, 'callsame is not in the dynamic scope of a dispatcher', 'callsame agrees');
try { nextwith };
check($!.Str, 'nextwith is not in the dynamic scope of a dispatcher', 'and so does nextwith');

try { "foobar".match("o", :x<hello>) };
check($!.^name, 'X::Str::Match::x', 'an invalid :x');
check($!.Str, 'in Str.match, got invalid value of type Str for :x, must be Int or Range',
      'names the type it got');

# how a type-check message renders its value
try { my Int $a = "foo" };
check($!.Str, 'Type check failed in assignment to $a; expected Int but got Str ("foo")',
      'a Str keeps its quotes');
try { my Int $b = "aaaaaaaaaaaaaaaaaaaaaaaaaaaa" };
check($!.Str, 'Type check failed in assignment to $b; expected Int but got Str ("aaaaaaaaaaaaaaaaaaa...)',
      'and elides past 23 characters');
try { my Int $c = (1, 2) };
check($!.Str, 'Type check failed in assignment to $c; expected Int but got List ((1, 2))',
      'a List separates with commas');
try { my Int $d = {a => 1} };
check($!.Str, 'Type check failed in assignment to $d; expected Int but got Hash ({:a(1)})',
      'a Hash uses the colon-pair form');
try { my Int $e = Any };
check($!.Str, 'Type check failed in assignment to $e; expected Int but got Any (Any)',
      'and a type object is bare');

# the bind gate sees both spellings of a string literal
try { my Int $f := "foo" };
check($!.^name, 'X::TypeCheck::Binding', 'a double-quoted RHS is checked');
check($!.Str, 'Type check failed in binding; expected Int but got Str ("foo")', 'with the same rendering');
try { my Int $g := 'foo' };
check($!.Str, 'Type check failed in binding; expected Int but got Str ("foo")', 'as is a single-quoted one');
my Int $ok := 42;
check($ok, '42', 'a well-typed bind still works');

# MOP
class MFoo {}
role MBar {}
check(MFoo.new.^mixin(MBar).gist, 'MFoo+{MBar}.new', '.^mixin sees the instance');
check((MFoo.new but MBar).gist,   'MFoo+{MBar}.new', 'and agrees with `but`');
Int.^add_method('rgdouble', method ($x:) { 2 * $x });
check(21.rgdouble, '42', '.^add_method works on a built-in type');
check(3.rgdouble,  '6',  'for every instance of it');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
