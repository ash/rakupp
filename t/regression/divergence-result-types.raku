# Regression: result TYPES that diverged from Rakudo (from raku-spec's
# DIVERGENCES.md, which probes `$r.^name ~ ' | ' ~ $r.gist` on both engines).
# Each line here was a row in that report; all now agree with Rakudo.
# Contract: exit 0 + last line PASS.
my @fail;
sub tn($v, $want, $what) { @fail.push("$what ({$v.^name})") unless $v.^name eq $want }

# cmp/<=>/leg/unicmp/coll yield Order, not Int
tn(1 <=> 2,        'Order', 'cmp-numeric');
tn('a' cmp 'b',    'Order', 'cmp');
tn(1 leg 2,        'Order', 'leg');
tn('a' unicmp 'b', 'Order', 'unicmp');
@fail.push('order-value') unless (1 <=> 2) === Order::Less && (2 <=> 2) === Order::Same;
@fail.push('order-numeric') unless (1 <=> 2).Int == -1;          # still Int-backed
@fail.push('order-sort') unless (3, 1, 2).sort({ $^a <=> $^b }).List eqv (1, 2, 3);

# a Junction's METAMODEL answers for the junction itself (no autothreading)
# (checked inline: passing a Junction to a sub AUTOTHREADS the call, so `tn` would
#  see the eigenstates rather than the junction)
@fail.push("all-junction ({(1 & 2).^name})")    unless (1 & 2).^name    eq 'Junction';
@fail.push("any-junction ({(1 | 2).^name})")    unless (1 | 2).^name    eq 'Junction';
@fail.push("one-junction ({(1 ^ 2).^name})")    unless (1 ^ 2).^name    eq 'Junction';
@fail.push("none-junction ({none(1,2).^name})") unless none(1, 2).^name eq 'Junction';
@fail.push('junction-autothread') unless (1 & 2).succ.gist eq 'all(2, 3)';
@fail.push('junction-collapse') unless so (1 | 2) == 2;

# X/Z (and their X<op>/Z<op> forms) and .grep are lazy: Seq, not List
tn((1 X 2),            'Seq', 'cross');
tn((1 Z 2),            'Seq', 'zip');
tn((1 X+ 2),           'Seq', 'cross-op');
tn((1 Z+ 2),           'Seq', 'zip-op');
tn((1, 2).grep(*.so), 'Seq', 'grep-method');
@fail.push('cross-value') unless (1 X 2).flat.List eqv (1, 2);
@fail.push('zipop-value') unless ((1, 2) Z* (3, 4)).List eqv (3, 8);

# a numeric STRING enters the exact numeric tower (Int/Rat), not Num
tn(1 * "2",   'Int', 'str-mul');
tn(1 + "2",   'Int', 'str-add');
tn(1 - "2",   'Int', 'str-sub');
tn(1 / "2",   'Rat', 'str-div');
tn("3" * "4", 'Int', 'str-str-mul');
tn("1.5" + 1, 'Rat', 'str-rat');
@fail.push('str-num-values') unless 1 * "2" == 2 && 1 / "2" == 0.5 && "1.5" + 1 == 2.5;
tn(2 ** "0.5", 'Num', 'str-irrational');   # genuinely Num, unchanged

# prefix | on a scalar is a one-element Slip
my $s1 = |(1);   tn($s1, 'Slip', 'slip-int');
my $s2 = |("a"); tn($s2, 'Slip', 'slip-str');
my $s3 = |(1, 2); tn($s3, 'Slip', 'slip-list');
sub two($a, $b) { "$a-$b" }
@fail.push('slip-splices-call') unless two(|(1, 2)) eq '1-2';
@fail.push('slip-splices-list') unless (1, |(2, 3), 4) eqv (1, 2, 3, 4);

# `xx` is lazy; notandthen yields Empty (a Slip); a LIST numifies to its element count
@fail.push("xx ({(1 xx 2).^name})")            unless (1 xx 2).^name eq 'Seq';
@fail.push("xx-str ({('a' xx 2).^name})")      unless ('a' xx 2).^name eq 'Seq';
@fail.push("notandthen ({(1 notandthen 2).^name})") unless (1 notandthen 2).^name eq 'Slip';
tn((1, 2) * (3, 4, 5), 'Int', 'list-mul');
tn((1, 2) + (3, 4, 5), 'Int', 'list-add');
tn((1, 2) / (3, 4, 5), 'Rat', 'list-div');
@fail.push('list-numify-value') unless (1, 2) + (3, 4, 5) == 5 && (1, 2) * (3, 4, 5) == 6;
@fail.push('xx-flattens') unless [1 xx 3].elems == 3;

# a Junction's own methods vs autothreaded ones (Rakudo parity)
@fail.push('junction-defined') unless (1 & 2).defined === True;      # the Junction is defined
@fail.push('junction-Str')     unless (1 & 2).Str.^name eq 'Junction';  # .Str autothreads
@fail.push('junction-succ')    unless (1 & 2).succ.gist eq 'all(2, 3)';

# a slurpy sequence generator sees EVERY element produced so far
@fail.push('slurpy-generator') unless (1, 2, sub { [*] @_[*-1], @_ + 1 } ... 720).join(' ') eq '1 2 6 24 120 720';
@fail.push('fib-generator')    unless (1, 1, { $^a + $^b } ... 34).join(' ') eq '1 1 2 3 5 8 13 21 34';

# A NON-NUMERIC string is an error in numeric context, not a silent 0.
# `+"a"` is a quiet Failure (Rakudo); USING it throws.
my $f = +"a";
@fail.push("plus-failure ({$f.^name})") unless $f.^name eq 'Failure';
@fail.push('failure-undefined') if $f.defined;
sub dies(&c) { my $d = False; { c(); CATCH { default { $d = True } } }; $d }
# NB: the Failure only detonates when the value is USED, so each probe sinks it
@fail.push('str-add-dies')  unless dies { ("a" + 1).Str };
@fail.push('str-cmp-dies')  unless dies { ("a" == "b").Str };
@fail.push('str-lt-dies')   unless dies { ("a" < "b").Str };
@fail.push('using-failure-dies') unless dies { (+"a").Str };
# …while the STRING operators and genuine numbers are untouched
@fail.push('str-eq-ok')  if "a" eq "b";
@fail.push('str-lt-str') unless "a" lt "b";
@fail.push('str-cmp-ok') unless ("a" cmp "b") === Order::Less;
@fail.push('empty-str')  unless ("" + 1) == 1 && (+"") == 0;
@fail.push('num-str')    unless (+"5") == 5 && ("5" == 5);
@fail.push('complex-str') unless (+"1+2i").^name eq 'Complex';

# $*KERNEL.bits is an Int (it used to fall through to the kernel NAME)
@fail.push("kernel-bits ({$*KERNEL.bits})") unless $*KERNEL.bits == 32 || $*KERNEL.bits == 64;

# a reduction metaop's `(…)` call form is bounded by its parens: a comma after
# the `)` belongs to the enclosing list (this held for [+] but not [\+] / [R-])
sub firstarg($a, $b) { $a }
@fail.push('tri-reduce-parens') unless firstarg([\+](1..4), 'desc').List eqv (1, 3, 6, 10);
@fail.push('tri-reduce-type')   unless ([\+] 1, 2, 3).^name eq 'Seq';
@fail.push('plain-reduce')      unless [+](1..4) == 10;

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
