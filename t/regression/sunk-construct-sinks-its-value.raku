# Regression: a sunk `if`, `given` or CATCH sinks its block's value, and so
# does a tap or a whenever (issue #60).
#
# A block's last statement is not sunk: its value is the block's. A
# construct that runs the block hands that value on — and when the
# construct itself was sunk, nothing sank the value. Every map being eager,
# that showed only on a lazy value: a gather, or a map whose block
# `return`s, handed back from a call, never ran. `f() if $c;`, the tail of
# an `if`/`else`/`with`/`given` block, a matched `when` or `default` (in a
# given, a loop or a CATCH) and the block of a `.tap` or a `whenever` all
# dropped it. Value positions still keep the value as it is. Every answer
# below is Rakudo 2026.09's.

my @log;
sub h($t) { gather { @log.push($t); take 1 } }

sub ck(&code, @want, $desc) {
    @log = ();
    code();
    return if @log eqv @want;
    say "FAIL: $desc — got {@log.raku}, want {@want.raku}";
    exit 1;
}

ck { h('a') if True },                      ['a'], '`STMT if COND`';
ck { h('z') unless False },                 ['z'], '`STMT unless COND`';
ck { h('m') with 1 },                       ['m'], '`STMT with X`';
ck { if True { h('i') }; 1 },               ['i'], 'an `if` block';
ck { if False { } elsif True { h('f') }; 1 }, ['f'], 'an `elsif` block';
ck { if False { } else { h('e') }; 1 },     ['e'], 'an `else` block';
ck { unless False { h('u') }; 1 },          ['u'], 'an `unless` block';
ck { with 1 { h('w') }; 1 },                ['w'], 'a `with` block';
ck { without Any { h('o') }; 1 },           ['o'], 'a `without` block';
ck { given 1 { h('g') }; 1 },               ['g'], 'a `given` block';
ck { given 1 { when 1 { h('W') } }; 1 },    ['W'], 'a matched `when`';
ck { given 1 { default { h('D') } }; 1 },   ['D'], 'a `default`';
ck { for 1, 2 { when 1 { h('F') } }; 1 },   ['F'], 'a `when` in a loop';
ck { try { die 'x'; CATCH { when X::AdHoc { h('X') } } }; 1 }, ['X'], 'a `when` in a CATCH';
ck { try { die 'x'; CATCH { default { h('c') } } }; 1 },       ['c'], 'a `default` in a CATCH';
ck { supply { emit 1; emit 2 }.tap({ h('t') }); 1 },          ['t', 't'], 'a `supply` block\'s tap';
ck { Supply.from-list(1, 2).tap({ h('l') }); 1 },              ['l', 'l'], 'a from-list tap';
ck { my $s = Supplier.new; $s.Supply.tap({ h('s') }); $s.emit(1); 1 }, ['s'], 'a Supplier\'s tap';
ck { react { whenever Supply.from-list(1) { h('r') } }; 1 },   ['r'], 'a whenever';

# …and where the value is kept, it is kept as it is: read later, not sunk
ck { my $x = do given 1 { when 1 { h('k') }; default { 0 } }; @log.push('|'); $x.elems }, ['|', 'k'],
   'a `given` in value position';
ck { my $y = do if True { h('j') }; @log.push('|'); $y.elems }, ['|', 'j'],
   'an `if` in value position';
sub q { if True { h('q') } }
ck { my $r = q(); @log.push('|'); $r.elems }, ['|', 'q'], 'an `if` a routine returns';

# a Failure a sunk block hands on detonates; one still in a variable does not
my $died = False;
try { if True { 'abc'.Int }; CATCH { default { $died = True } } }
die 'FAIL: a sunk `if` lets its Failure go' unless $died;
my $g = 'abc'.Int;
if True { $g }
die 'FAIL: a variable a sunk `if` names was sunk' unless $g ~~ Failure;

say 'PASS';
