# Regression: a batch of documentation divergences in the core scalar types.
#   * `Nil[…]` / `Nil{…}` — evalIndex had no Nil arm at all, so both subscripts
#     fell through the whole function to its trailing `return Value::any()`.
#     Nil.AT-POS/AT-KEY are Nil at any depth, and a SLICE answers one Nil per
#     index, so `(Nil)[0,1]` is two of them, not one.
#   * `"5.0" ~~ <5>` — an allomorph RHS is a VT::Int carrying hashKind "IntStr",
#     so it landed in the generic numeric arm, where `'05' ~~ 5` is deliberately
#     True. Allomorph.ACCEPTS is numeric only for a Numeric matchee; against a
#     plain Str it compares the STRING face.
#   * `.defined` on a Junction was in `junctionOwn` ("a Junction is itself a
#     defined object"), which is the wrong question: it autothreads and then
#     COLLAPSES to a plain Bool. It is not an alias for `.Bool` — `(any 0, "")`
#     is True for `.defined` and False for `.Bool`. `.DEFINITE` really does
#     belong to the junction and stays.
#   * `∞/∞` was a LEXER bug, not arithmetic: `∞` is a TERM lexed as an Op, so
#     regexContext answered "a `/` here starts a regex" and swallowed the rest of
#     the file into a regex literal. `*` was already exempt for the same reason.
#   * `Num.new` did not exist; the generic type-object `.new` answered 0 for
#     every argument.
#   * Complex `.sqrt` used std::sqrt(std::complex), whose libc++ form loses a ULP
#     on the real part. Rakudo computes it from the modulus. The method and the
#     sub are two call sites and had to move together.
#   * Complex `.narrow` stopped at the Num instead of recursing, so a whole-number
#     real part never demoted to Int.
#   * `.Str(:superscript)`/`(:subscript)` were ignored. ¹²³ are NOT in the U+2070
#     run, so a `0x2070 + d` table is wrong for exactly those three.
#   * `Date.new(y, m, *)` is the last day of that month; the Whatever numified to
#     0 and tripped the range check.
#   * Supply `.words`/`.lines` concatenate the stream before running the Str
#     method, like `.comb` next to them — applied per MESSAGE, `.words` over
#     `"Hello Word!".comb` gave one "word" per character.
#   * `isa-ok`'s default description: the .isa fast path said "isa Int" and the
#     ancestry fallback said nothing at all. Rakudo's is the same on both.
#   * `.CREATE` allocates WITHOUT running BUILD/TWEAK — delegating to .new would
#     pass `Mu.CREATE.defined` while being semantically wrong.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# Nil subscripts
check((Nil)[100].gist,   'Nil',        'Nil positional subscript');
check((Nil){100}.gist,   'Nil',        'Nil associative subscript');
check((Nil)[0,1].gist,   '(Nil Nil)',  'a slice answers one Nil per index');
check((Nil)[0][2][4].gist, 'Nil',      'at any depth');

# allomorph smartmatch
check(("5.0" ~~ <5>).gist, 'False', 'a plain Str matches an allomorph by its string face');
check(("05"  ~~ <5>).gist, 'False', 'so a numerically-equal string does not match');
check(("5"   ~~ <5>).gist, 'True',  'the identical string does');
check((5.0   ~~ <5>).gist, 'True',  'a Numeric matchee is still numeric');
check((<5.0> ~~ <5>).gist, 'True',  'and so is another allomorph');

# junction .defined
check((none 3, Str).defined.gist, 'False', 'defined autothreads and collapses');
check((any 0, "").defined.gist,   'True',  'it is not an alias for .Bool');
check((any 0, "").Bool.gist,      'False', 'which answers differently');
check((any 3, Str).defined.gist,  'True',  'any with one defined state');
check((all 1, 2).DEFINITE.gist,   'True',  'DEFINITE still belongs to the junction');

# the ∞ lexer fix — the second statement must survive
check((∞/∞).gist, 'NaN', 'infinity over infinity');
check((∞ / ∞).gist, 'NaN', 'with spaces');
check((2 * ∞).gist, 'Inf', 'and the following statement still runs');

# Num.new
check(Num.new(3).gist, '3', 'Num.new takes its argument');
check(Num.new.gist,    '0', 'and defaults to zero');

# Complex sqrt / narrow
check((-3+4i).sqrt.gist, '1+2i', 'complex sqrt from the modulus');
check(sqrt(-3+4i).gist,  '1+2i', 'the sub agrees with the method');
check((3-4i).sqrt.gist,  '2-1i', 'and a negative imaginary part keeps its sign');
check((4.0 + 0i).narrow.raku, '4',  'narrow recurses into the real part');
check((2+0i).narrow.^name,    'Int', 'a whole real part demotes to Int');
check((2.5e0+0i).narrow.^name, 'Num', 'a fractional one stays a Num');
check((2+3i).narrow.^name,    'Complex', 'a nonzero imaginary part stays Complex');

# super/subscript
check(42.Str(:superscript),    '⁴²', 'superscript digits');
check(42.Str(:subscript),      '₄₂', 'subscript digits');
check((-42).Str(:superscript), '⁻⁴²', 'and the sign');
check(123.Str(:superscript),   '¹²³', '1, 2 and 3 are outside the U+2070 run');
check(42.Str,                  '42',  'without an adverb nothing changes');

# Date.new with a Whatever day
check(Date.new(2042, 2, *).Str,  '2042-02-28', 'the last day of February');
check(Date.new(2044, 2, *).Str,  '2044-02-29', 'in a leap year');
check(Date.new(2042, 12, *).Str, '2042-12-31', 'and of December');
check(Date.new(:2042year, :2month, :day(*)).Str, '2042-02-28', 'the named form too');
check(Date.new(2042, 2, 3).Str,  '2042-02-03', 'an ordinary day is unaffected');

# Supply.words / .lines
my @w; Supply.from-list("Hello Word!".comb).words.tap({ @w.push($_) });
check(@w.join('|'), 'Hello|Word!', 'words concatenates the stream first');
my @l; Supply.from-list("a\nb\nc".comb).lines.tap({ @l.push($_) });
check(@l.join('|'), 'a|b|c', 'and so does lines');

# .CREATE allocates without BUILD/TWEAK
check(Mu.CREATE.defined.gist, 'True', 'CREATE makes a defined object');
class RCF { has $.x = 5 }
check(RCF.CREATE.defined.gist,   'True',  'for a user class as well');
check(RCF.CREATE.x.defined.gist, 'False', 'with no BUILD run, so no default applied');
check(RCF.new.x,                 '5',     'while .new still applies it');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
