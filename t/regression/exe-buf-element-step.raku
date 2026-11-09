# A COMPILED program steps a Buf's byte as the interpreter does when the
# subscript counts from the end or the Buf is an element (2026-11-09):
# `$b[*-1]--` died "Index out of range. Is: -1", and `@a[0][0]++` with a Buf
# in @a[0] made it an Array, [1]. Codegen took a step to the element's own
# path (bufElemStep for a Buf) only for a variable and a plain subscript;
# a step's target may be `$b[*-1]`, resolved against the elements there are
# (rtStarKey), or an element's element now. Plain arrays step as before.
#
# The program must compile natively, with and without -O, and print what
# the interpreter prints, which is what Rakudo 2026.09 prints.
# Contract: exit 0 + last line PASS.

my $rakupp = $*RAKU.compiler.name eq 'Raku++';
unless $rakupp {
    note 'exe-buf-element-step: not rakupp, nothing to compile';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("exe-buf-element-step-$*PID");
mkdir $work;
my $fails = 0;

sub check(Str $desc, $got, $want) {
    if $got eq $want { say "ok - $desc" }
    else {
        $fails++;
        say "not ok - $desc";
        note "GOT  [{$got}]\nWANT [{$want}]";
    }
}

my $src = $work.add('bufstep.raku');
$src.spurt: q:to/END/;
    my $b = Buf.new(2, 0); $b[*-1]--; say $b.list;
    my @a = Buf.new(5); @a[0][0]++; say @a[0].list;
    my $c = Buf.new(7, 8); say $c[*-1]++; say $c.list; say ++$c[*-2]; say --$c[*-1];
    my %h = k => Buf.new(1); %h<k>[0]++; say %h<k>.list;
    my int @n = 1, 2; @n[*-1]++; say @n;
    my @p = 1, 2; @p[*-1]++; say @p;
    my @q = [1, 2], [3]; @q[0][1]++; say @q;
    my %g = a => [5]; %g<a>[0]--; say %g;
    END

my $want = ('(2 255)', '(6)', '8', '(7 9)', '8', '8', '(2)', '[1 3]', '[1 3]', '[[1 3] [3]]', '{a => [4]}').join('|');

my $i = run($*EXECUTABLE, $src.Str, :out, :err);
check('interpreted', $i.out.slurp(:close).lines.join('|'), $want);
$i.err.slurp(:close);

for ('', '-O') -> $o {
    my $tag = $o ?? "compiled $o" !! 'compiled';
    my $bin = $work.add('bufstep' ~ $o);
    my $b = run($*EXECUTABLE, '--exe', |($o ?? ($o,) !! ()), '-o', $bin.Str, $src.Str, :out, :err);
    my $bout = $b.out.slurp(:close) ~ $b.err.slurp(:close);
    if $b.exitcode != 0 { $fails++; say "not ok - $tag: compiles"; note $bout; next }
    check("$tag: natively, no interpreter bundled", so($bout.contains('Compiled (native)') && !$bout.contains('bundling')), True);
    my $p = run($bin.Str, :out, :err);
    check("$tag: agrees", $p.out.slurp(:close).lines.join('|'), $want);
    $p.err.slurp(:close);
}

for $work.dir { .unlink }
$work.rmdir;
say $fails ?? 'FAIL' !! 'PASS';
