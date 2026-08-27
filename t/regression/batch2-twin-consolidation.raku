# The REVIEW-3.7 cooldown, batch 2: the twins are dead — behaviours that used
# to depend on WHICH copy of an implementation a program reached:
#
#   * `[op]` reduce: Codegen emitted a bare left fold (rtReduce) for every
#     [op], so `[<] 3,1,2` was False interpreted and True COMPILED, and scan
#     forms folded with the literal op string. rtReduce now folds through
#     applyReduce — the one implementation. (The compiled half of this is
#     pinned by tools' own --exe legs; here we pin the interpreted answers
#     the compiled path now shares.)
#   * Z/X had three implementations with two element models: the applyArith
#     copies flattened DEEP, so `(1,0) X (<a b>, <c d>)` answered 8 pairs on
#     that path (which is what compiled code and reduce fold with) and 4 on
#     the others. One zxOp now; one-level everywhere; the endless-Z lazy view
#     reaches every path.
#   * hyper compound assignment had three copies; the parenthesised-list
#     write-back fix lived in one. One implementation (plus a value-only
#     fallback where no interpreter exists).
#   * the `.can`/`.^lookup` existence probe: two verbatim copies of the
#     35-name unsafe-to-probe list, already drifted on invocant guards.
#   * EVAL/EVALFILE "defines symbols" predicate: four spellings, two gaps —
#     $path.EVALFILE slipped the flat-scan guard, .EVAL-as-method slipped
#     Lint's dynamic-names suppression.
#   * exceptionToJson had its own JSON string escaper (byte-different on
#     \b/\f and \u case); IO::Spec carried two byte-identical tmpdirs.
# Contract: exit 0 + last line PASS. (Under Rakudo everything passes except
# the two rakupp-tooling checks: --lint does not exist there, and its tmpdir
# spelling keeps a trailing slash.)
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}

# -- [op] reduce: the one implementation's answers -----------------------------
check(([<] 3, 1, 2), False, '[<] chains pairwise');
check(([<] 1, 2, 3), True, '[<] chains pairwise (ascending)');
check(([\+] 1..5).join(','), '1,3,6,10,15', 'scan form yields partial sums');
check(([R-] 1, 2, 3), 0, 'R-metaop reverses the reduction');
check(([max] 3, 1, 4), 4, 'word-op reduce');
check(([+] ()), 0, 'empty [+] identity');
check(([Z] ()).elems, 0, 'empty [Z] identity');

# -- Z/X: one element model, lazy views everywhere -----------------------------
check(((1,0) X (<a b>, <c d>)).elems, 4, 'X keeps sublists whole (one-level model)');
check(((1,2) Z (3,4)).raku, '((1, 3), (2, 4)).Seq', 'bare Z tuples');
check((1 Z=> 3).raku, '(1 => 3,).Seq', 'Z=> keeps the Int key');
check(((1,2) Z+ (10,20)).join(','), '11,22', 'Z+ pairwise');
check(((2..*) Z* (2..*))[^4].join(','), '4,9,16,25', 'endless Z is a lazy view');
check((<a b> Z, <c d>).raku, '(("a", "c"), ("b", "d")).Seq', 'Z, tuples');

# -- hyper compound assignment: one implementation, write-back included --------
{
    my @a = 1, 2, 3;
    @a <<+=>> 10;
    check(@a.join(','), '11,12,13', 'array hyper compound assign mutates in place');
    my ($t, $y) = 0, 1;
    ($t, $y) »+=« (5, 7);
    check("$t $y", '5 8', 'parenthesised scalars get the write-back');
}

# -- can/^lookup: the shared probe behaves as both sites always did ------------
check("x".can('uc').elems, 1, '.can finds a real method');
check("x".can('nosuchmethodhere').elems, 0, '.can answers empty for a missing one');
check("x".can('say').elems, 0, '.can never probes a side-effectful name');
check(Str.^lookup('parse-base').^name, 'Method', '^lookup on a type object stays curated');
my $probe-file = $*TMPDIR.add("b2-probe-$*PID");
check($probe-file.e, False, 'the probe never creates files as a side effect');

# -- lint: .EVAL as a METHOD suppresses dynamic-name rules ---------------------
{
    my $lint = run($*EXECUTABLE, '--lint', '-e', 'my $c = "say 1"; $c.EVAL; my $unused = 5;', :out, :err);
    my $out = $lint.out.slurp(:close) ~ $lint.err.slurp(:close);
    check($out.contains('unused'), False, '.EVAL (method form) suppresses unused-name lint');
    my $lint2 = run($*EXECUTABLE, '--lint', '-e', 'my $unused = 5;', :out, :err);
    my $out2 = $lint2.out.slurp(:close) ~ $lint2.err.slurp(:close);
    check($out2.contains('unused'), True, '…while a plain unused name still warns');
}

# -- one JSON escaper: exceptionToJson uses JsonLite ---------------------------
{
    my %env = %*ENV;
    %env<RAKU_EXCEPTIONS_HANDLER> = 'JSON';
    my $p = run($*EXECUTABLE, '-e', 'die "q\"q\tt"', :out, :err, :env(%env));
    my $j = $p.out.slurp(:close) ~ $p.err.slurp(:close);
    check($j.contains('q\\"q\\tt'), True, 'exception JSON escapes through the one escaper');
}

# -- one attribute walk: builtin subclasses construct like plain classes ------
{
    class B2D is DateTime { has $.a = 1; has $.b = $!a * 2 }
    my $d = B2D.new(:2000year, a => 5);
    check("a={$d.a} b={$d.b} y={$d.year}", 'a=5 b=10 y=2000',
          'is-DateTime subclass runs the full walk (self + provided-args order)');
    class B2N is DateTime { has $.tag = "made-" ~ self.^name }
    check(B2N.now.tag, 'made-B2N', '.now on a subclass runs defaults with self in scope');
    class B2T is Date { has $.note = "d" }
    check(B2T.today.note, 'd', '.today keeps the subclass and its defaults');
    check(B2T.today.^name, 'B2T', '…and the subclass identity');
}

# -- IO::Spec tmpdir: one implementation, same answer --------------------------
check($*SPEC.tmpdir.Str.chars > 0, True, 'tmpdir answers a path');
check($*SPEC.tmpdir.Str.ends-with('/'), False, 'with trailing slashes trimmed');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
