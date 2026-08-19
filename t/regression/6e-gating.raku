# Regression: the 6.e additions exist only for code that asked for 6.e.
#
# Every pair below is the same program run under both revisions. Under 6.d the
# routine, type or syntax is not there at all — as in Rakudo, where these live
# in CORE.e and a 6.d unit never loads it. Phase 2 of docs/dev/plans/6E-PLAN.md.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}
sub like-check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want /$want/") unless $got.contains($want)
}

# chomp, not trim: a Format pads on the LEFT, and trimming the answer would
# quietly turn "  foo" into the thing we are not testing.
sub both(Str $code) {
    my $d = run($*EXECUTABLE, '-e', $code, :out, :err);
    my $e = run($*EXECUTABLE, '-e', "use v6.e.PREVIEW; $code", :out, :err);
    ( ($d.out.slurp(:close) ~ $d.err.slurp(:close)).chomp,
      ($e.out.slurp(:close) ~ $e.err.slurp(:close)).chomp )
}

my ($d, $e);

($d, $e) = both 'say (1,2,3,4,5).snip(* < 3)';
like-check $d, 'No such method',  '.snip is absent under 6.d';
check      $e, '((1 2) (3 4 5))', '.snip works under 6.e';

($d, $e) = both 'say snip(* < 3, 1,2,3,4)';
like-check $d, 'Undefined routine', 'the snip sub is absent under 6.d';
check      $e, '((1 2) (3 4))',     'the snip sub works under 6.e';

($d, $e) = both 'say rotor(2, 1..6)';
like-check $d, 'Undefined routine', 'the rotor sub is absent under 6.d';
check      $e, '((1 2) (3 4) (5 6))', 'the rotor sub works under 6.e';

($d, $e) = both 'my $x = (1,2).snitch; say "kept"';
like-check $d, 'No such method', '.snitch is absent under 6.d';
like-check $e, 'kept',           '.snitch works under 6.e';

($d, $e) = both 'my $f := q:o/%5s/; say $f("foo")';
like-check $d, 'Unrecognized adverb', 'a Format literal does not parse under 6.d';
check      $e, '  foo',               'a Format literal works under 6.e';

($d, $e) = both 'say Format.new("%5s")("hi")';
like-check $d, 'Undeclared name', 'the Format type is absent under 6.d';
check      $e, '   hi',           'the Format type works under 6.e';

($d, $e) = both 'my %h = A => { B => 1, C => 2 }, D => 3; say (%h{**}).raku ~ " " ~ (%h{**}:k).raku';
check $d, 'Any ()',                    '%h{**} is a missing key before 6.e';
check $e, '(1, 2, 3) ("B", "C", "D")', '%h{**} walks to the leaves under 6.e';

($d, $e) = both 'my %h = A => { B => 42 }; say (%h{\'A\';\'B\'}).raku ~ " " ~ (%h{\'A\';\'Z\'}).raku';
check $d, '(42,) (Any,)', 'a one-value hash multislice is a one-element list before 6.e';
check $e, '42 Any',       '…and the value itself from 6.e on';

($d, $e) = both 'my @a = [[1,2],[3,4]],; say (@a[0;1;0]).raku';
check $d, '3', 'an ARRAY multislice answers with the value under 6.d';
check $e, '3', '…and under 6.e too — this one did not change';

($d, $e) = both 'sub f(*@_) { my &c = { @_.elems }; say c() }; f(7,7)';
check $d, '2', 'a block with no arguments sees the enclosing routine\'s @_ before 6.e';
check $e, '0', '…and its own, empty, from 6.e on';

($d, $e) = both 'say unlink("/tmp/no-such-file-abc123").raku';
check $d, '["/tmp/no-such-file-abc123"]', '6.d unlink answers with the paths it removed';
check $e, 'Bool::True',                   '6.e unlink takes one path and answers one Bool';

# --- phase 3: behaviours 6.e changed, which we used to answer one way ---

# .Bool, not `so`: `so` is loose enough to swallow the whole concatenation.
($d, $e) = both 'say (5..1).Bool ~ " " ~ ("b".."a").Bool ~ " " ~ (1..5).Bool';
check $d, 'True True True',  'a Range is always true before 6.e';
check $e, 'False False True', '…and true when it holds something from 6.e on';

($d, $e) = both 'say (-1e0).log ~ " " ~ (-100e0).log10';
check $d, 'NaN NaN',                                  'log of a negative is NaN before 6.e';
check $e, '0+3.141592653589793i 2+1.3643763538418412i', '…and the complex logarithm from 6.e on';

($d, $e) = both 'say 6.pick(3).elems';
check $d, '1', 'Int.pick is Any.pick on a one-item list before 6.e';
check $e, '3', '…and short for (^6).pick from 6.e on';

($d, $e) = both 'subset Even of Int where * %% 2; say Even.^ver';
check $d, '6.d', 'a subset reports the revision it was declared under (6.d)';
check $e, '6.e', '…and 6.e when that is the one';

($d, $e) = both 'say "Hello World".contains("world", :smartcase) ~ " " ~ "hello world".contains("World", :smartcase)';
check $d, 'False False', ':smartcase is ignored before 6.e';
check $e, 'True False',  '…and from 6.e folds case only for a needle that carries none';

($d, $e) = both 'say "Hello World".index("world", :smartcase) ~ " " ~ "Hello".substr-eq("hell", 0, :smartcase)';
check $d, ' False', ':smartcase reaches index and substr-eq too (6.d: no match)';
check $e, '6 True', '…and finds them from 6.e on';

($d, $e) = both 'say "abcdefg".comb(2 => 1, :partial)';
like-check $d, 'Cannot resolve caller', 'comb with a Pair is not a call before 6.e';
check      $e, '(ab de g)',             '…and is size => gap from 6.e on';

# Deliberate divergence: Rakudo's own :smartcase candidate for .indices never
# gets dispatched to — "hello Hello".indices("hello", :smartcase) is (0,) there,
# while :i on the same call gives both positions. We do what the adverb means.
my $ind = run($*EXECUTABLE, '-e',
              'use v6.e.PREVIEW; say "Hello hello".indices("hello", :smartcase).raku',
              :out).out.slurp(:close).chomp;
check $ind, '(0, 6)', '.indices honours :smartcase (Rakudo drops it — their dispatch bug)';

($d, $e) = both 'my %h{Str}; say %h<nope>.WHAT.^name ~ " " ~ %h.of.^name';
check $d, 'Any Any', 'an object hash with no value type takes Any before 6.e';
check $e, 'Mu Mu',   '…and Mu from 6.e on';

($d, $e) = both 'my @a = 1,2,3; @a.splice(1,1,$[8,9]); say @a.raku';
check $d, '[1, 8, 9, 3]',   'every splice replacement flattens before 6.e';
check $e, '[1, [8, 9], 3]', '…and an itemized one goes in whole from 6.e on';

($d, $e) = both 'say Date.new(2026,1,1).DateTime(:timezone(3600)).timezone';
check $d, '0',    'Date.DateTime drops :timezone before 6.e';
check $e, '3600', '…and honours it from 6.e on';

($d, $e) = both 'my $r = MY::<$nosuchvar>; say $r.^name ~ " " ~ $r.defined';
check $d, 'Any False',     'a pseudo-package miss is Nil before 6.e (and never a throw)';
check $e, 'Failure False', '…and a Failure from 6.e on';

($d, $e) = both 'my $*dyn = 42; sub f { say LEXICAL::<$*dyn> }; f';
check      $d, '42', 'LEXICAL:: hands back a dynamic before 6.e';
like-check $e, 'Cannot access', '…and refuses it from 6.e on, a dynamic not being lexical';

# .^name on the call, not on a variable: assigning Nil to a $ resets it to the
# container default, so `my $r = G.parse(…)` reads Any in both engines and would
# hide the difference this pins.
($d, $e) = both 'grammar G { token TOP { \d+ } }; say G.parse("abc").^name';
check $d, 'Nil',     'a failed parse is Nil before 6.e';
check $e, 'Failure', '…and a Failure carrying X::Syntax::Confused from 6.e on';

($d, $e) = both 'say //42 ~ " " ~ //Any';
check $e, 'True False', 'prefix // is .defined from 6.e on';

($d, $e) = both 'say (1,2,3).map({ $_ == 2 ?? next(42) !! $_ }).List';
check $d, '(1 3)',    'next drops its value before 6.e';
check $e, '(1 42 3)', '…and supplies it from 6.e on';

($d, $e) = both 'say (1,2,3).map({ $_ == 2 ?? last(99) !! $_ }).List';
check $d, '(1)',    'last drops its value before 6.e';
check $e, '(1 99)', '…and supplies it from 6.e on';

($d, $e) = both 'say so "abc" ~~ /<|f> abc/';
check      $d, 'True', 'an unknown regex boundary is a silent no-op before 6.e';
like-check $e, 'Unrecognized regex boundary', '…and a compile error from 6.e on';

($d, $e) = both 'say so "abc" ~~ /<|w> abc/';
check $d, 'True', 'the real boundaries keep working (6.d)';
check $e, 'True', 'the real boundaries keep working (6.e)';

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
