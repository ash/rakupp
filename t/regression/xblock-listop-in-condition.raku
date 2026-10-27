# A listop before a block in a statement condition, Rakudo's xblock rule.
# The listop takes the block as its argument, as it does anywhere else, so
# the statement's own block has to come after it, and when none does the
# program is refused at compile time: X::Comp::Group, whose sorrow is
# X::Syntax::BlockGobbled ("Function 'bar' needs parens to avoid gobbling
# block") and whose panic is X::Syntax::Missing ("Missing block (apparently
# claimed by 'bar')"). The same in `if`, `unless`, `elsif`, `with`,
# `without`, `while`, `until`, `repeat while`, `for`, `given`, `when` and
# `whenever`.
#
# Here the first block was always the statement's: `if foo { 1 } { say
# "yes" }` called foo with nothing, `if bar { say "t" }` ran its block, and
# `for foo -> $x { }` bound the pointy to the loop. `whenever` parsed its
# supply as a plain expression, so `whenever tick { … }` passed the block to
# tick and died at run time.
#
# A term, a type or a statement prefix before a block is no listop and keeps
# the block for the statement: `if now { }`, `if rand { }`, `for buf8 ->
# \bt { }`, `when $_ ~~ Generic { }` with Generic a class a module exports,
# `for gather { … } { }`.
#
# Contract: exit 0 + last line PASS. Every case is oracle-checked against
# Rakudo 2026.09 — run this file under `rakudo` too.
use MONKEY-SEE-NO-EVAL;
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}

# ---- the listop takes the block, and the statement's own block follows
sub foo(&b) { b() }
sub two(&b, &c) { b() + c() }
my @log;
if foo { 1 } { @log.push: 'if' }
unless foo { 0 } { @log.push: 'unless' }
if 0 { } elsif foo { 1 } { @log.push: 'elsif' }
with foo { 5 } -> $v { @log.push: "with $v" }
without foo { Any } { @log.push: 'without' }
given foo { 7 } { @log.push: "given $_" }
given 3 { when foo { 3 } { @log.push: 'when' } }
if two { 1 }, { 2 } { @log.push: 'two blocks' }
check @log.List, ('if', 'unless', 'elsif', 'with 5', 'without', 'given 7', 'when', 'two blocks'),
    'each statement runs its own block after the one the listop took';

my $n = 0; @log = ();
while foo { $n++ < 2 } { @log.push: "while $n" }
$n = 0;
until foo { $n++ >= 2 } { @log.push: "until $n" }
$n = 0;
repeat while foo { $n < 2 } { $n++ }
@log.push: "repeat $n";
check @log.List, ('while 1', 'while 2', 'until 1', 'until 2', 'repeat 2'), 'and so do the loops';

@log = ();
for foo { (1, 2) } -> $x { @log.push: $x }
for foo { (3, 4) } { @log.push: $_ }
check @log.List, (1, 2, 3, 4), 'for: a pointy or a block after the one the listop took';

sub Cap(&b) { b() }
check (do if Cap { 1 } { 'cap' }), 'cap', 'a capitalized sub (issue #198) the same';

my $wv;
react { whenever foo { Supply.from-list(9) } { $wv = $_ } }
check $wv, 9, 'whenever: the supply a listop made from its block';

# the comma form, which was a call before as well
check (gather for map { $_ * 2 }, 1..3 { take $_ }).List, (2, 4, 6), 'for map { … }, LIST { }';
check (gather for map -> $v { $v + 1 }, 1..2 { take $_ }).List, (2, 3), 'for map -> $v { … }, LIST { }';
check (gather for grep { $_ > 1 }, 1..3 { take $_ }).List, (2, 3), 'for grep { … }, LIST { }';

# ---- no block after it: the program is refused, naming the listop
sub refused($code, $fn, $desc) {
    my $msg = "Function '$fn' needs parens to avoid gobbling block"
        ~ ($fn ~~ / '::' | <[A..Z]><[a..z]> / ?? " (or perhaps it's a class that's not declared or available in this scope?)" !! '');
    try EVAL $code;
    my $e = $!;
    check $e.^name, 'X::Comp::Group', "$desc: a group";
    return unless $e ~~ X::Comp::Group;
    my $s = $e.sorrows[0];
    check $s ~~ X::Syntax::BlockGobbled, True, "$desc: its sorrow is X::Syntax::BlockGobbled";
    check ~$s.what, $fn, "$desc: naming $fn";
    check $s.message, $msg, "$desc: the sorrow's message";
    check $e.panic ~~ X::Syntax::Missing, True, "$desc: its panic is X::Syntax::Missing";
    check $e.panic.what, "block (apparently claimed by '$fn')", "$desc: the panic's block";
}
refused 'sub bar { 5 }; if bar { say "t" }', 'bar', 'if';
refused 'sub bar { 5 }; unless bar { say "t" }', 'bar', 'unless';
refused 'sub bar { 5 }; if 0 { } elsif bar { say "t" }', 'bar', 'elsif';
refused 'sub bar { 5 }; with bar { say "t" }', 'bar', 'with';
refused 'sub bar { 5 }; without bar { say "t" }', 'bar', 'without';
refused 'sub bar { 5 }; while bar { last }', 'bar', 'while';
refused 'sub bar { 5 }; until bar { last }', 'bar', 'until';
refused 'sub bar { 5 }; repeat while bar { last }', 'bar', 'repeat while';
refused 'sub bar { 5 }; given bar { say "t" }', 'bar', 'given';
refused 'sub bar { 5 }; given 5 { when bar { say "t" } }', 'bar', 'when';
refused 'for lines { .say }', 'lines', 'for';
refused 'sub bar { 1..3 }; for bar -> $x { say $x }', 'bar', 'for with a pointy';
refused 'sub bar { 5 }; if bar -> $x { say $x }', 'bar', 'if with a pointy';
refused 'sub bar { 5 }; if !bar { } else { say "t" }', 'bar', 'under a prefix';
refused 'sub tick { Supply.from-list(1) }; react { whenever tick { .say } }', 'tick', 'whenever';
refused 'sub Tick { Supply.from-list(1) }; react { whenever Tick { .say } }', 'Tick', 'whenever, a capitalized sub';
refused 'sub Bar { 5 }; if Bar { say "t" }', 'Bar', 'if, a capitalized sub';
refused 'if undeclared-thing { say 1 }', 'undeclared-thing', 'an undeclared name';

# ---- a term, a type or a statement prefix is no listop: the block is the statement's
@log = ();
if now { @log.push: 'now' }
if time { @log.push: 'time' }
if rand + 1 { @log.push: 'rand' }
if rand { @log.push: 'rand bare' }
if i { @log.push: 'i' }
if pi { @log.push: 'pi' }
if True { @log.push: 'True' }
constant cc = 1;
if cc { @log.push: 'constant' }
my \sl = 1;
if sl { @log.push: 'sigilless' }
sub term:<tt> { 1 }
if tt { @log.push: 'term' }
enum Sizes <small large>;
given large { when small { @log.push: 'small' }; when large { @log.push: 'enum' } }
check @log.List, ('now', 'time', 'rand', 'rand bare', 'i', 'pi', 'True', 'constant', 'sigilless', 'term', 'enum'),
    'terms keep the block for the statement';

@log = ();
for int, uint64 -> \nt { @log.push: nt.^name }     # roast S32-basics/xxPOS-native.t's shape
for num64 { @log.push: .^name }
check @log.List, ('int', 'uint64', 'num64'), 'a native type keeps it';
sub long(&b) { b() }                                # (NativeCall's `long` is not a type here)
check (do if long { 1 } { 'long' }), 'long', '…unless a sub of the name is declared';

@log = ();
for buf8 -> \bt { @log.push: bt.^name }
for Int { @log.push: .^name }
without Int -> $u { @log.push: "without {$u.^name}" }
class Pt { }
for Pt -> $p { @log.push: $p.^name }
given 5 { when Int { @log.push: 'when Int' } }
check @log.List, (buf8.^name, 'Int', 'without Int', 'Pt', 'when Int'), 'types keep it too';

@log = ();
for gather { take 1 } { @log.push: "gather $_" }
if try { 1 } { @log.push: 'try' }
if do { 1 } { @log.push: 'do' }
if quietly { 1 } { @log.push: 'quietly' }
for lazy gather { take 2 } { @log.push: "lazy $_" }
react { whenever supply { emit 3 } { @log.push: "supply $_" } }
check @log.List, ('gather 1', 'try', 'do', 'quietly', 'lazy 2', 'supply 3'), 'and so do statement prefixes';

@log = ();
sub bar { 5 }
if bar() { @log.push: 'parens' }
if (bar) { @log.push: 'in parens' }
@log.push: 'modifier' if bar;
check @log.List, ('parens', 'in parens', 'modifier'), 'a listop with parens, in parens, or in a modifier';

# a class a `use`d module exports is a type, though only the module declares it
my $dir = $*TMPDIR.add("xblock-{$*PID}");
$dir.mkdir;
$dir.add('XbGen.rakumod').spurt: q:to/END/;
    unit module XbGen;

    class Generic is export { }
    END
for <cold cached> -> $run {
    my $p = run($*EXECUTABLE, '-I', ~$dir, '-e',
        'use XbGen; given Generic.new { when $_ ~~ Generic { say "type" } }', :out, :err);
    my $err = $p.err.slurp(:close);
    check $p.out.slurp(:close).lines.List, ('type',),
        "when \$_ ~~ ImportedClass \{ }, $run run{$err ?? " ($err.lines.head())" !! ''}";
}
for $dir.dir { .d ?? run('rm', '-rf', ~$_) !! .unlink }
$dir.rmdir;

.note for @fail;
say @fail ?? "FAIL ({+@fail})" !! 'PASS';
exit @fail ?? 1 !! 0;
