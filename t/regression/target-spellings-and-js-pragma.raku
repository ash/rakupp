# Regression: every backend answers to one idiom, and `use js` is a pragma.
#
# `--cpp` and `--target=js` grew up separately — the second for Rakudo muscle
# memory, which is where `--target=` came from — so a reader had no way to guess
# that one took a key and the other did not, and `-o` worked for exactly one of
# them. Each backend now answers to both spellings, and `--target=raku` names
# the formatter, because emitting Raku is what a Raku target emits.
#
# `use js` is lowercase because it is COMPILER territory, like `strict` and
# `nqp`: there is no distribution named `js` to find, to install, or to claim in
# the ecosystem's module namespace. It declares what the program targets, so the
# interpreter refuses it at that line and names the command that runs it.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}
sub contains($got, $want, $what) {
    @fail.push("$what: {$got.raku} lacks {$want.raku}") unless $got.contains($want)
}

my $rakupp = $*EXECUTABLE.Str;
my $tmp = $*TMPDIR.add("target-spellings-{$*PID}");
$tmp.mkdir;
LEAVE { try { .unlink for $tmp.dir; $tmp.rmdir } }

my $src = $tmp.add('prog.raku');
$src.spurt: "sub greet(\$n) \{ \"hi \$n\" }\nsay greet('world');\n";

sub out(*@args) {
    my $p = run |@args, :out, :err;
    my $o = $p.out.slurp(:close);
    my $e = $p.err.slurp(:close);
    ($p.exitcode, $o, $e);
}

# ---------- each backend answers to both spellings ----------
{
    my ($rc1, $a) = out($rakupp, '--cpp', $src.Str);
    my ($rc2, $b) = out($rakupp, '--target=cpp', $src.Str);
    check $rc1, 0, '--cpp exits 0';
    check $rc2, 0, '--target=cpp exits 0';
    check $a, $b, '--cpp and --target=cpp emit the same C++';
    contains $a, 'greet', 'the C++ carries the program';
}
{
    my ($rc1, $a) = out($rakupp, '--target=js', $src.Str);
    my ($rc2, $b) = out($rakupp, '--js', $src.Str);
    check $rc1, 0, '--target=js exits 0';
    check $rc2, 0, '--js exits 0';
    check $a, $b, '--target=js and --js emit the same JavaScript';
    contains $a, 'u_greet', 'the JavaScript carries the program';
}
{
    my ($rc1, $a) = out($rakupp, '--fmt', $src.Str);
    my ($rc2, $b) = out($rakupp, '--target=raku', $src.Str);
    check $rc1, 0, '--fmt exits 0';
    check $rc2, 0, '--target=raku exits 0';
    check $a, $b, '--fmt and --target=raku emit the same Raku';
}

# ---------- -o writes, for both source backends ----------
{
    my $o = $tmp.add('out.cpp');
    my ($rc) = out($rakupp, '--cpp', $src.Str, '-o', $o.Str);
    check $rc, 0, '--cpp -o exits 0';
    check ($o.e && $o.s > 0), True, '--cpp -o writes the file';
    contains $o.slurp, 'greet', '…and it is the C++';
}
{
    my $o = $tmp.add('out.js');
    my ($rc) = out($rakupp, '--target=cpp', $src.Str, '-o', $o.Str);
    check $rc, 0, '--target=cpp -o exits 0';
    check ($o.e && $o.s > 0), True, '--target=cpp -o writes the file too';
}

# ---------- an unknown target names the ones that exist ----------
{
    my ($rc, $o, $e) = out($rakupp, '--target=bogus', $src.Str);
    check $rc, 4, 'an unknown target exits 4';
    for <parse ast js cpp raku> -> $t {
        contains $e, $t, "the refusal lists '$t'";
    }
}

# ---------- `use js` is a pragma, and the interpreter refuses it ----------
{
    my $js = $tmp.add('browser.raku');
    $js.spurt: "use js;\nsay JS.Math.sqrt(16);\n";

    my ($rc, $o, $e) = out($rakupp, $js.Str);
    check ($rc != 0), True, 'the interpreter refuses a program that targets JavaScript';
    contains $e, 'targets JavaScript', '…saying what the program is';
    contains $e, '--target=js', '…and naming the command that runs it';
    contains $e, $js.basename, '…with this program in the command';
    # NOT the message a missing module gets: there is no module to miss
    check $e.contains('Could not find'), False, 'it is not reported as a missing module';

    # and the same program transpiles
    my $out = $tmp.add('browser.js');
    my ($rc2) = out($rakupp, '--target=js', '--standalone', $js.Str, '-o', $out.Str);
    check $rc2, 0, 'the same program transpiles';
    check $out.e, True, '…and the JavaScript is written';
    contains $out.slurp, 'Math', '…with the interop in it';
}

# `use JS` is the older spelling of the same pragma
{
    my $js = $tmp.add('older.raku');
    $js.spurt: "use JS;\nsay JS.Math.sqrt(16);\n";
    my ($rc, $o, $e) = out($rakupp, $js.Str);
    check ($rc != 0), True, 'the older spelling is refused the same way';
    contains $e, 'targets JavaScript', '…with the same message';
    my $out = $tmp.add('older.js');
    my ($rc2) = out($rakupp, '--target=js', '--standalone', $js.Str, '-o', $out.Str);
    check $rc2, 0, 'and it still transpiles';
}

say @fail ?? "FAIL: {@fail.join('; ')}" !! 'PASS';
exit @fail ?? 1 !! 0;
