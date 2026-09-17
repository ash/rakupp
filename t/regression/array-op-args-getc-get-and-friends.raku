# Regression: five one-assertion Roast losses from the 2026-09-17 ceiling board,
# each a small primitive answering something silently wrong.
#
#   pop() / shift() / push() / unshift() with no argument answered Any; Rakudo
#     refuses at compile time (X::TypeCheck::Argument). pop(@a, 10) and
#     @a.pop(10) were accepted; `push @a, a => 52` pushed the Pair where the
#     named argument has no parameter to bind to. (S32-array/{pop,shift,push,
#     unshift}.t)
#   (-1) ** 4553535345364535345634543533 was 1: a big exponent fell through
#     to a double pow, where the odd number is an even-looking 4.55e27.
#     (S32-num/power.t)
#   %h.append onto a LIST value left it a List; Rakudo's slot becomes an
#     Array. (S32-hash/push.t)
#   $fh.getc then $fh.get re-read the line from its first character: the two
#     readers kept separate cursors. (integration/advent2010-day03.t)
#   class GLOBAL::evo declared a class NAMED "GLOBAL::evo". (S12-class/
#     magical-vars.t)
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }
sub threw(&c, $type, $what) {
    my $r = try { c() };
    return @fail.push("$what: returned {$r.raku} instead of throwing") unless $!;
    @fail.push("$what: threw {$!.^name}, not $type.^name()") unless $! ~~ $type;
}
threw { EVAL 'pop()' },     X::TypeCheck::Argument, 'pop()';
threw { EVAL 'shift()' },   X::TypeCheck::Argument, 'shift()';
threw { EVAL 'push()' },    X::TypeCheck::Argument, 'push()';
threw { EVAL 'unshift()' }, X::TypeCheck::Argument, 'unshift()';
my @a = 1 .. 5;
threw { EVAL 'pop(@a, 10)' },   Exception, 'pop(@a, 10)';
threw { EVAL '@a.pop(10)' },    Exception, '@a.pop(10)';
threw { EVAL 'shift(@a, 10)' }, Exception, 'shift(@a, 10)';
threw { EVAL '@a.shift(10)' },  Exception, '@a.shift(10)';
threw { my @b; push    @b, a => 52 }, Exception, 'push @b, a => 52';
threw { my @b; append  @b, a => 52 }, Exception, 'append @b, a => 52';
threw { my @b; unshift @b, a => 52 }, Exception, 'unshift @b, a => 52';
threw { my @b; prepend @b, a => 52 }, Exception, 'prepend @b, a => 52';
check pop(@a), 5, 'pop(@a) still pops';
check shift(@a), 1, 'shift(@a) still shifts';
{ my @b = 1; push @b, 2; unshift @b, 0; check @b, [0, 1, 2], 'push/unshift still work' }
{ my @b; push([]); check @b, [], 'push([]) lives' }

my $odd  = 4553535345364535345634543533;
my $even = 4553535345364535345634543534;
check (-1) ** $odd,  -1, '(-1) ** big odd';
check (-1) ** $even,  1, '(-1) ** big even';
check   1  ** $odd,   1, '1 ** big';
check   0  ** $odd,   0, '0 ** big';

{ my %h = :b(2, 3); %h.append(%(:b<Y>)); check %h<b>, [2, 3, 'Y'], 'append onto a List value'; check %h<b>.^name, 'Array', 'the slot is an Array' }

{
    my $f = $*TMPDIR.add("getc-get-$*PID.txt");
    $f.spurt("TODO line\nsecond\n");
    my $fh = $f.open;
    check $fh.getc, 'T', 'getc';
    check $fh.get, 'ODO line', 'get after getc continues on the same line';
    check $fh.getc, 's', 'getc after get continues on the next line';
    check $fh.get, 'econd', 'get after that';
    $fh.close; $f.unlink;
}

EVAL 'class GLOBAL::RegrGlobalEvo { method who { self.WHAT.^name } }';
check EVAL('RegrGlobalEvo.new.who'), 'RegrGlobalEvo', 'class GLOBAL::X is named X';

.say for @fail;
say @fail ?? "FAIL" !! "PASS";
exit +?@fail;
