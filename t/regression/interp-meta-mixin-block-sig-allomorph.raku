# Regression: meta-method interpolation, non-composable mixins, the implicit `$_`
# of a bare block, regex routines, allomorph identity, and `.tree` itemisation.
#   * `"$x.^name()"` — the hand-written postfix chain in the string parser accepted
#     `.[`, `.{`, `.(` and `.IDENT(`, but the `.IDENT` arm tested only isalpha,
#     so `^` ended the chain and `.^name()` was emitted as literal text. One meta
#     sigil (`^`, `?`, `&`) may now sit between the dot and the name. Commit is
#     still gated on a following `(`, so a bare `"$x.^name"` stays literal — which
#     is what Rakudo does too.
#   * `1 but B` for a plain class silently built Int+{B}: the collect walk inside
#     mixinValue accepts any type object as rolish and never consults isRole. An
#     UNREGISTERED type name is deliberately still allowed, because rakupp does
#     not register the built-in roles (Numeric, Positional…) as classes and
#     `1 but Numeric` is legal.
#   * a bare `{ … }` block carries an implicit `$_`: arity 0, count 1. `-> {…}`
#     and `sub {…}` do not — so a pointy block now records that it wrote a
#     signature, even an empty one. A `:( … )` literal is excluded as well, or an
#     empty signature would start accepting a one-argument call.
#   * `my regex R {…}` built a Callable with no marker of its origin, so typeName
#     fell through to "Sub".
#   * an ALLOMORPH is neither of its halves. Quanthash keys, `===` and `eqv` all
#     compared renderings, and `<42>` renders "42" exactly like the Int — so
#     `42 ∈ <42 55 1>` was True. Narrow on purpose: only allomorphs get identity
#     keys, so the set operators that compare against a `set(…)` literal are
#     untouched.
#   * `.tree(N)` built plain lists, so a later `.flat` descended the whole
#     structure. Every node BELOW the root is an item.
# NOT fixed, and backed out during this batch: `42[2]` should throw X::OutOfRange
# rather than answering Nil. Correct in isolation, but rakupp separately makes
# `@p[0]` a scalar where it should be a list, and under a throw
# S03-operators/assign.t dies at line 230 and loses 201 further assertions. The
# reason is recorded at the site in Interpreter.cpp.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# meta-method interpolation
my $ex = 42;
check("$ex.^name(): $ex", 'Int: 42', 'a meta-method interpolates');
check("$ex.^name",        '42.^name', 'without parens it stays literal');
sub rgf($z) { "f($z)" }
check("$ex.&rgf()",       'f(42)',   'and a .& sub call');
my $q = "hi";
check("$q.uc()",          'HI',      'an ordinary method is unaffected');
check("plain $ex",        'plain 42', 'as is a bare variable');

# only roles may be mixed in
class RgB { }
role RgR { }
check((try { 1 but RgB } // 'threw'), 'threw', 'a plain class is not composable');
check($!.^name, 'X::Mixin::NotComposable', 'with the right exception');
check($!.Str, 'Cannot mix in non-composable type RgB into object of type Int', 'and message');
check((1 but RgR).^name,     'Int+{RgR}',     'a role still composes');
check((1 but Numeric).^name, 'Int+{Numeric}', 'and an unregistered built-in role is left alone');

# the implicit $_ of a bare block
check({;}.signature.gist,  '(;; $_? is raw = OUTER::<$_>)', 'a bare block has an implicit $_');
check({;}.signature.arity, '0', 'arity 0');
check({;}.signature.count, '1', 'but count 1');
check((-> {;}).signature.gist,  '()', 'a pointy block wrote an empty signature');
check((sub {;}).signature.gist, '()', 'and so did an anonymous sub');
check(:().gist,                 '()', 'a signature literal is not a block');
check((:($) ~~ :()).gist,   'False', 'so it still binds nothing');

# a named regex is a Regex
my regex RgRx { \N }
check(&RgRx.^name, 'Regex', 'not a Sub');
check((sub {}).^name, 'Sub', 'while a sub still is');

# allomorph identity
check((42 ∈ <42 55 1>).gist,   'False', 'an Int is not the allomorph <42>');
check((<42> ∈ <42 55 1>).gist, 'True',  'but the allomorph is');
check((42 === <42>).gist,      'False', 'nor identical to it');
check((42 eqv <42>).gist,      'False', 'nor eqv');
check((<42> === <42>).gist,    'True',  'two allomorphs are');
check((42 === 42).gist,        'True',  'and two Ints');
check((set(<42>) eqv set(42)).gist, 'False', 'so the sets differ too');
check(set(<42 55>).gist,       'Set(42 55)', 'while the rendering is unchanged');

# .tree itemises below the root
my @floors = ('A', ('B','C', ('E','F','G')));
check(@floors.tree(2).flat.elems, '2', 'tree(2) stops flat at depth 1');
check(@floors.tree(1).flat.elems, '6', 'tree(1) does not');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
