# Regression: two battery-found bugs from the v3.20.1 cooldown (2026-08-27).
#
# 1. A predicate block may ANSWER a Regex — `{ .defined && /re/ }` returns the
#    `&&` right side as the object — and the consumer (.first/.grep/toggle,
#    a sequence endpoint) boolifies that answer by MATCHING it against the
#    element, setting the CALLER's $/. The v3.20.0 regex-literal rework read
#    the Regex value's plain truth instead: always-true, $/ never written.
#    HTTP::Tiny's multipart-boundary `~$/` came back "" (dist test t/responses.t),
#    and Log::Async's t/14-frame.rakutest picked wrong frames. Oracle: Rakudo
#    2026.08 — `.first({ /re/ })` returns the element and sets $/.
#
# 2. `temp $x = … if/unless COND;` — the statement-modifier branch runs in the
#    ENCLOSING scope (the parser flattens it), so its temp belongs to the
#    enclosing block and must restore at THAT block's exit. execBlock drained
#    its own temp marks even for these scope-sharing pseudo-blocks, restoring
#    the temp the moment the one-line branch finished. Data::Dump's
#    `temp $colorizor = sub { '' } unless $color` un-tempted itself instantly
#    and ANSI color leaked into plain output (9/9 -> 1/9).

my $ok = True;

# --- 1a: .first with a regex-answering predicate, Str invocant path ---------
my %h = content-type => 'multipart/byteranges; boundary=3d6b6a416f9b5';
sub read-it() {
    with %h<content-type>.first: { .defined && / 'boundary=' '"'? <( <-["]>+ )> / } {
        return ~$/;
    }
    '';
}
unless read-it() eq '3d6b6a416f9b5' {
    say "FAIL: .first-answered regex did not set caller \$/ (got '{read-it()}')";
    $ok = False;
}

# --- 1b: list invocant, and the truth itself (a non-matching regex answer) --
my @w = <alpha beta gamma>;
my $hit = @w.first({ / e t a $ / });
unless $hit eq 'beta' && ~$/ eq 'eta' {
    say "FAIL: list .first regex-answer (got '{$hit // 'Nil'}', \$/='{~$/}')";
    $ok = False;
}
unless @w.first({ / zz / }) === Nil {
    say 'FAIL: a never-matching regex answer must be FALSE, not truthy-object';
    $ok = False;
}

# --- 1c: grep goes through the same consumer ---------------------------------
my @g = <ab cd ad>.grep({ / a . / });
unless @g.join(',') eq 'ab,ad' {
    say "FAIL: .grep regex-answer filtered wrong: {@g.raku}";
    $ok = False;
}

# --- 2: temp/let under a statement modifier ----------------------------------
my $f = 'orig';
sub g1(Bool :$c) {
    temp $f = 'tmp' unless $c;
    $f;                     # still inside the enclosing block: temp holds
}
unless g1(:c(False)) eq 'tmp' {
    say "FAIL: temp under `unless` modifier did not take effect in-scope";
    $ok = False;
}
unless $f eq 'orig' {
    say "FAIL: temp under `unless` modifier did not restore at routine exit";
    $ok = False;
}
sub g2(Bool :$c) {
    temp $f = 'tmp2' if !$c;
    $f;
}
unless g2(:c(False)) eq 'tmp2' && $f eq 'orig' {
    say "FAIL: temp under `if` modifier (got in='{g2(:c(False))}', after='$f')";
    $ok = False;
}
# the block form still restores at the BLOCK's own exit (Rakudo semantics)
sub g3() {
    if True { temp $f = 'tmp3'; }
    $f;                     # block left -> already restored
}
unless g3() eq 'orig' {
    say "FAIL: block-form temp must restore at the block exit";
    $ok = False;
}

say $ok ?? 'PASS' !! 'FAIL';
exit $ok ?? 0 !! 1;
