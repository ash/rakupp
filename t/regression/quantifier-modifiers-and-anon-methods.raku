# Regression: quantifier modifiers, anonymous `method` values, and quanthash slices.
#   * a quantifier takes a modifier: `?` frugal, `!` greedy, `:` RATCHET (possessive —
#     grab greedily and never give any back). Each also has a colon spelling, so
#     `a*:?` is frugal and `a*:!` greedy; only a BARE `a*:` is possessive. Consuming
#     the colon without looking at what follows turned `xa*:!` into a possessive
#     `a*` followed by a literal `!`.
#   * `method {…}` as a TERM builds a Method, not a Sub: it takes its invocant as the
#     first argument and binds `self`, so `"g".$m("h")` passes "h" to the parameter
#     rather than shifting everything left by one.
#   * `$sh<a b> = False, True` is a LIST assignment (the angle-word subscript is a
#     slice) and each key follows the quanthash rule — False removes, True adds.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# the ratchet modifier
check(so ('bazaar' ~~ /a*: a/),   'False', 'a possessive star gives nothing back');
check(so ('bazaar' ~~ /a* a/),    'True',  'a plain one does');
check(so ('aaa'    ~~ /a+: a/),   'False', 'and so does a possessive plus');
check(so ('abbabbababba' ~~ /^[a|b]*: aba/), 'False', 'outside a group too');
check(so ('abbabbababba' ~~ /^[a|b]*  aba/), 'True',  'the control still matches');
# the colon spellings of frugal and greedy
check(('xaaaay' ~~ /xa*:!/).Str,  'xaaaa', ':! is greedy');
check(('xaaaay' ~~ /xa*:?/).Str,  'x',     ':? is frugal');
check(('xaaaay' ~~ /xa*:!y/).Str, 'xaaaay',':! backtracks like any greedy one');
check(('xaaaay' ~~ /xa?:?y/).defined, 'False', 'a frugal ques cannot reach y');
check(('xay'    ~~ /xa?:?y/).Str, 'xay',   'but it can when one a suffices');
check(('xaaaay' ~~ /xa*:y/).Str,  'xaaaay','a bare colon still reaches y here');
check(('xaaaay' ~~ /xa*:a/).defined, 'False', 'though not when it must give one back');

# an anonymous method
my $m = method ($invocant: $param) { "$invocant/$param" }
check($m.^name, 'Method', 'method {…} as a term is a Method');
check("greeting".$m("hello"), 'greeting/hello', 'its invocant and argument bind separately');
my $n = method ($p) { self ~ '-' ~ $p }
check("g".$n("h"), 'g-h', 'an implicit invocant is `self`');
check(<a b c>.&(my method (List:D:) { self.raku }), '("a", "b", "c")', 'and .& passes it too');
my $s = sub ($a, $b) { "$a+$b" }
check($s.^name, 'Sub', 'sub {…} is still a Sub');
check("x".$s("y"), 'x+y', 'which takes the invocant as its first argument');

# a quanthash slice
my $fruits = <peach apple orange>.SetHash;
$fruits<apple kiwi> = False, True;
check($fruits.keys.sort.gist, '(kiwi orange peach)', 'False removes and True adds');
my $bag = <a a b>.BagHash;
$bag<a b> = 0, 5;
check($bag.kv.sort.gist, '(5 b)', 'a zero removes from a BagHash');
my %plain = :1a, :2b;
%plain<a b> = 7, 8;
check(%plain.raku, '{:a(7), :b(8)}', 'an ordinary Hash slice is unaffected');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
