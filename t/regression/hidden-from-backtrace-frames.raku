# `is hidden-from-backtrace` hides the routine's frame from the backtrace,
# as on Rakudo: it was accepted and dropped, so a die in a hidden private
# method printed it, and every frame that led there (DBIish raises its errors
# through two such methods). The frame stays in `.backtrace.list`, with
# `.is-hidden` True, and in `.full` and (here) `--ll-exception`; the routine answers
# `.is-hidden-from-backtrace` True, and one without the trait has no such
# method. (A method's frame here is named `C::go` where Rakudo writes `go`;
# that difference is ours on purpose, BACKTRACE-PLAN.md, and is left out.)
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

class C {
    method !inner() is hidden-from-backtrace { die "boom" }
    method go { self!inner }
}
sub s() is hidden-from-backtrace { C.new.go }
sub plain() { s() }
sub names($text) { $text.lines.grep(/^ \s* 'in ' /).map({ .words[1, 2].join(' ').subst(/'C::'/, '') }).List }

try plain();
ck names($!.backtrace.Str), ('method go', 'sub plain', 'block <unit>'), 'the printed frames';
# (Rakudo's .full has its setting's frames too: `method throw`, `sub die`, `code`)
ck names($!.backtrace.full).grep(none 'code at', 'method throw', 'sub die').List,
   ('method inner', 'method go', 'sub s', 'sub plain', 'block <unit>'), '.full';
ck $!.backtrace.list.grep({ !.is-setting && .subname }).map({ .subname => .is-hidden }).List,   # (and a nameless one)
   (inner => True, go => False, s => True, plain => False, '<unit>' => False), '.list and .is-hidden';
ck &s.?is-hidden-from-backtrace, True, 'the routine says so';
ck &plain.?is-hidden-from-backtrace, Nil, 'one without the trait has no such method';
ck C.^find_method('go').?is-hidden-from-backtrace, Nil, '…a method neither';

my $f = $*TMPDIR.add("hidden-bt-{$*PID}.raku");
$f.spurt: q:to/R/;
    class C {
        method !inner() is hidden-from-backtrace { die "boom" }
        method go { self!inner }
    }
    sub s() is hidden-from-backtrace { C.new.go }
    s();
    R
LEAVE $f.unlink if $f.e;
my $p = run $*EXECUTABLE, $f, :err;
ck names($p.err.slurp(:close)), ('method go', 'block <unit>'), 'on stderr';
if $*RAKU.compiler.name eq 'Raku++' {   # (Rakudo's --ll-exception is the VM's, in a form of its own)
    $p = run $*EXECUTABLE, '--ll-exception', $f, :err;
    ck so(names($p.err.slurp(:close)).first('sub s')), True, '--ll-exception shows them';
}

say 'PASS';
