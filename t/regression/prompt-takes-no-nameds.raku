# Regression: `prompt` accepted named arguments and silently discarded them,
# and for a while it accepted `:hidden` and acted on it.
#
# Both were wrong, for different reasons. Rakudo's `prompt` has exactly two
# signatures, `()` and `($msg)`, so ANY named argument is a caller error there;
# here they were dropped on the floor, and `prompt(:hidden)` with no message
# even printed the pair — "hidden<TAB>True" — as the prompt string.
#
# Then `:hidden` was briefly implemented ON `prompt` itself. That is the part
# worth writing down: it worked, and it was still wrong. An adverb no other
# Raku accepts makes the engine a dialect — the program runs here and dies on
# the next Raku, and nobody finds out until it is ported. So the engine keeps
# the CAPABILITY, as the `rakupp-prompt-hidden` primitive, and the ADVERB
# belongs to a module (Prompt::Hidden) that probes for the primitive and falls
# back to `stty` elsewhere. One spelling, one meaning, every engine.
#
# Every case runs in a CHILD with stdin under our control: this file's own
# stdin belongs to the harness, and a `prompt` that is wrongly accepted would
# sit there reading it.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

sub run-it(Str $code, Str $in = "") {
    my $p = run($*EXECUTABLE, '-e', $code, :in, :out, :err);
    try { $p.in.print($in); $p.in.close; True }
    my $out = (try $p.out.slurp(:close)) // '';
    my $err = (try $p.err.slurp(:close)) // '';
    ((try $p.exitcode) // -1, $out, $err)
}

# --- `prompt` takes no named arguments -------------------------------------
for <hidden other> -> $named {
    my ($rc, $out, $err) = run-it("prompt('x: ', :$named); say 'RAN'");
    check $rc != 0,               True,  "prompt refuses :$named";
    check $out.contains('RAN'),   False, "…and the program does not run on";
    check $err.contains($named),  True,  "…and the message names :$named";
}

# --- the message is still positional, and still read ------------------------
my ($rc, $out, $err) = run-it('my $x = prompt("m: "); say "[$x]"', "plain-4e1a\n");
check $out, "m: [plain-4e1a]\n", 'a plain prompt still prints its message and reads';

# `prompt` returns the allomorph; the hidden primitive returns a plain Str,
# because a numeric secret is not a number.
($rc, $out, $err) = run-it('say prompt("n: ").^name', "1234\n");
check $out, "n: IntStr\n", 'prompt still allomorphs a numeric line';

# --- the capability lives in the primitive, not the adverb ------------------
($rc, $out, $err) = run-it(
    'my &g = &::("rakupp-prompt-hidden"); my $x = g("pw: "); say "[$x] ", $x.^name',
    "s3cret-9f2b\n");
check $out, "pw: [s3cret-9f2b] Str\n", 'rakupp-prompt-hidden reads, and returns a Str';

($rc, $out, $err) = run-it('say (try &::("rakupp-prompt-hidden")) ~~ Callable');
check $out, "True\n", 'and it is probeable by name, which is how a module finds it';

if @fail { die "FAIL:\n" ~ @fail.join("\n") }
say 'PASS';
