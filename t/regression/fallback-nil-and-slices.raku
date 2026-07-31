# Regression: five general fixes, all found by running Terminal::ANSI's own test
# suite (which went 2/8 → 8/8).
#
#  1. `method FALLBACK` — unimplemented. A class may catch every unresolved
#     method, receiving the NAME and then the original arguments.
#  2. A sigilless `constant` is a TERM: `CSI ~ $s` is a concat, not a listop call
#     that swallows the `~` as a prefix ("Undefined routine 'CSI'").
#  3. `@a[1^..3]` — an exclusive START in a literal range subscript was ignored
#     (only `..^` was honoured).
#  4. Slice assignment distributed a DEEP-flattened right-hand side, so
#     `@a[0..2] = ([1,2],[3,4],Nil)` spread [1,2] over two keys. One level now.
#  5. Nil STORED into a container element restores that element's default, the
#     rule scalars already followed.
#
# (A sixth fix is not asserted here: `unit monitor Foo;`, the file-scoped form of
# OO::Monitors' declarator, was never recognised — only the block form — so the
# module's attributes were read with no package open. Testing it needs a separate
# compilation unit AND the OO::Monitors distribution, which this Rakudo does not
# have installed; it is covered by Terminal::ANSI's suite in the module battery.)
# Contract: exit 0 + last line PASS.
my @fail;

# 1. FALLBACK
class F {
    method real() { 'real' }
    method FALLBACK($name, |c) { "$name/{c.elems}" }
}
@fail.push('FALLBACK called')      unless F.new.whatever eq 'whatever/0';
@fail.push('FALLBACK gets args')   unless F.new.foo(1, 2) eq 'foo/2';
@fail.push('real method wins')     unless F.new.real eq 'real';
class G is F {}
@fail.push('FALLBACK inherited')   unless G.new.zap eq 'zap/0';
class H {}
@fail.push('no FALLBACK still dies') unless (try { H.new.nope; 0 } // 1) == 1;

# 2. a constant is a term
constant CSI = "\e[";
sub esc($s) { CSI ~ $s }
@fail.push('constant as term')     unless esc('1m') eq "\e[1m";
our constant OC = 'x';
@fail.push('our constant as term') unless (OC ~ 'y') eq 'xy';
sub listop($x) { "[$x]" }
@fail.push('sub is still a listop') unless (listop ~ 'y') eq '[y]';

# 3. exclusive-start range subscripts
my @r = 1..5;
@fail.push('^..')  unless @r[1^..3].join(',')  eq '3,4';
@fail.push('^..^') unless @r[1^..^4].join(',') eq '3,4';
@fail.push('..^')  unless @r[1..^3].join(',')  eq '2,3';
@fail.push('..')   unless @r[1..3].join(',')   eq '2,3,4';
@fail.push('^n')   unless @r[^3].join(',')     eq '1,2,3';
@fail.push('^.. with *') unless @r[0^..*-1].join(',') eq '2,3,4,5';

# 4. slice assignment is one level deep
my @s;
@s[0..2] = ([1,2], [3,4], Nil);
@fail.push('nested arrays stay nested')
    unless @s[0].elems == 2 && @s[1].elems == 2 && !@s[2].defined;
my @t;
@t[0..1] = (1,2), (3,4);
@fail.push('nested lists stay nested') unless @t[0].elems == 2;
my @u; my @src = 1, 2, 3;
@u[0..2] = @src;
@fail.push('a flat list still distributes') unless @u.join(',') eq '1,2,3';

# 5. Nil restores the element default
my @n = 1, 2;
@n[0] = Nil;
@fail.push('elem = Nil')       unless @n[0].^name eq 'Any';
my Int @ti = 1, 2; @ti[0] = Nil;
@fail.push('typed elem = Nil') unless @ti[0].^name eq 'Int';
my @dd is default(9) = 1, 2; @dd[0] = Nil;
@fail.push('is default elem')  unless @dd[0] == 9;
my %h = a => 1; %h<a> = Nil;
@fail.push('hash value = Nil') unless %h<a>.^name eq 'Any';
my @p; @p.push(Nil); @p.append(Nil); @p.unshift(Nil); @p.prepend(Nil);
@fail.push('push family') unless @p.map(*.^name).unique.join(',') eq 'Any';
my @li = (1, Nil, 3);
@fail.push('list init') unless @li[1].^name eq 'Any';
# …but a bare List is not a container: its Nils survive
@fail.push('bare List keeps Nil') unless (1, Nil, 3)[1].^name eq 'Nil';

# the shape that started it: scrolling a screen buffer by one row inside a region
my @screen;
sub put-at($r, $c, $str) { @screen[$r] //= []; @screen[$r][$c..^($c + $str.chars)] = $str.comb }
put-at($_, 1, "$_") for 1..5;
my ($top, $bot) = 2, 4;
@screen[$top..$bot] = (|@screen[$top^..$bot], Nil);
my $render = join '', (1..5).map: -> $row {
    (@screen[$row][1..1].map: { .defined ?? $_ !! ' ' }).join
};
@fail.push("scroll: '$render'") unless $render eq '134 5';

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
