# Regression: the Grand Review, batch C2 — the method-dispatch ladder and the
# builtins beside it (docs/dev/findings/REVIEW-GRAND.md). Every case is what
# Rakudo answers; each one used to be a silent wrong answer here.

my $ok = True;
sub check($got, $want, $label) {
    unless $got eqv $want { note "FAIL: $label — {$got.raku} vs {$want.raku}"; $ok = False }
}
sub dies(&code) { my $lived = False; try { code(); $lived = True }; !$lived }

# 1. Rounding a Num past 2**63 is exact (the cast saturated at 9223372036854775807).
check(1e19.floor,       10000000000000000000,  '1e19.floor');
check(1e19.ceiling,     10000000000000000000,  '1e19.ceiling');
check(1e19.round,       10000000000000000000,  '1e19.round');
check(1e19.truncate,    10000000000000000000,  '1e19.truncate');
check((-1e19).floor,   -10000000000000000000,  '(-1e19).floor');
check(floor(1e19),      10000000000000000000,  'floor(1e19)');
check(ceiling(-1e19),  -10000000000000000000,  'ceiling(-1e19)');
check(round(1e19),      10000000000000000000,  'round(1e19)');
check(1e19.UInt,        10000000000000000000,  '1e19.UInt');
check((2**70).UInt,     2**70,                 'a big Int .UInt keeps its value');
check((-(2**70)).UInt.defined, False,          'a negative big Int .UInt is a Failure');
check(1e19.Rat.nude,   (10000000000000000000, 1), '1e19.Rat is that integer over 1');
check(2.5.round,        3,                     '2.5.round is still 3');

# 2. A Complex is Numeric already.
check((3+4i).Numeric,   3+4i,                  'Complex.Numeric is itself');

# 3. unique/repeated compare with === (1, "1" and 1e0 are three values), as squish does.
check((1, "1", 1e0).unique.elems,         3,     'unique keeps 1, "1", 1e0 apart');
check((1, "1", 1, "1").repeated.List,    (1, "1"), 'repeated: each value against its own kind');

# 4. chop and indices count graphemes, as substr/comb/flip do.
check("ax\x[301]".chop,                   "a",    'chop removes the whole grapheme');
check("abcd".chop(2),                     "ab",   'chop(2)');
check("x\x[301]ab".indices("a").List,    (1,),   'indices counts graphemes');

# 5. The trim family knows Unicode White_Space.
check("\x[A0]a\x[A0]".trim,               "a",        'trim strips NBSP');
check("\x[0C]a\x[0B]".trim-leading,       "a\x[0B]",  'trim-leading strips a form feed');
check(" a\x[2003]".trim-trailing,         " a",       'trim-trailing strips an em space');
check("  ".trim,                          "",         'trim of only spaces');

# 6. Out-of-range arguments are refused, not wrapped or clamped.
check(dies({ 255.base(37) }),             True,   '.base(37) is out of range');
check(dies({ 255.base(1) }),              True,   '.base(1) is out of range');
check(255.base(16),                       "FF",   '.base(16) still works');
check(dies({ "abc".substr(-1) }),         True,   'substr(-1) is out of range');
check(dies({ "abc".substr(1, -1) }),      True,   'substr(1, -1) is out of range');
check("abc".substr(*-1),                  "c",    'substr(*-1) is the way');
check(dies({ Hash.new(1, 2, 3) }),        True,   'Hash.new with an odd count dies');
check(dies({ (1, 2, 3).hash }),           True,   '.hash with an odd count dies');
check(dies({ 5.to-posix }),               True,   '5.to-posix is no method');

# 7. `created` is the birth time, not another name for `modified`.
{
    my $f = $*TMPDIR.add("rakupp-c2-{$*PID}");
    $f.spurt("x");
    check(abs($f.modified.Int - time) < 5,   True, '.modified.Int is raw POSIX, as Rakudo answers');
    sleep 1.2;
    $f.spurt("y", :append);
    check($f.created < $f.modified,          True, '.created stays put when the file is appended to');
    check(abs($f.created - $f.accessed) < 5, True, '.created is a time of this file');
    $f.unlink;
}

# 8. The IO::Path mutators answer a Failure on error (a quiet False let the program run on).
check(dies({ "/nonexistent-dir-c2-{$*PID}/x".IO.spurt("x") }),   True, 'spurt into a missing directory fails');
check(dies({ "/nonexistent-dir-c2-{$*PID}".IO.rmdir }),          True, 'rmdir of a missing directory fails');
check(dies({ "/nonexistent-c2-{$*PID}".IO.chmod(0o644) }),       True, 'chmod of a missing file fails');
{
    my $f = $*TMPDIR.add("rakupp-c2b-{$*PID}");
    $f.spurt("a");
    check(dies({ $f.spurt("b", :createonly) }),                     True, 'spurt :createonly on an existing file fails');
    check($f.slurp,                                                 "a",  '…and leaves the file alone');
    check(dies({ spurt($f, "b", :createonly) }),                    True, 'the sub form too');
    $f.unlink;
}
{
    my $f = $*TMPDIR.add("rakupp-c2c-{$*PID}");
    $f.spurt("x");
    check(dies({ $f.Str.unlink }),   True, 'a bare Str has no unlink');
    check($f.e,                      True, '…so the file is still there');
    check(dies({ $f.Str.spurt("y") }), True, 'a bare Str has no spurt');
    check(dies({ "rakupp-c2-nodir-{$*PID}".mkdir }), True, 'a bare Str has no mkdir');
    $f.unlink;
}
{
    my $fh = open "/nonexistent-c2-{$*PID}";
    check($fh.defined,   False,     'open of a missing file is a Failure, not a throw');
    check($fh.^name,     'Failure', '…a Failure');
}

# 9. `$fh.put` writes to the handle (the universal arm printed the HANDLE to stdout).
{
    my $f = $*TMPDIR.add("rakupp-c2d-{$*PID}");
    my $fh = open $f, :w;
    $fh.put("line");
    $fh.close;
    check($f.slurp, "line\n", '$fh.put writes the line');
    $f.unlink;
}

# 10. $*KERNEL answers this machine, not a constant.
check($*KERNEL.version.^name,   'Version',  '$*KERNEL.version is a Version');
{
    my $uname = (run 'uname', '-m', :out).out.slurp(:close).trim;
    check($*KERNEL.archname.contains($uname), True, '$*KERNEL.archname names the machine');
}

# 11. The Test helpers judge what they claim to (a nested run, so this file stays plain).
sub tap(Str $code) {
    my $p = run $*EXECUTABLE, '-e', "use Test; $code", :out, :err;
    $p.out.slurp(:close).lines.grep(/^ 'ok' | ^ 'not ok' /).map({ .starts-with('ok') }).List
}
# (throws-like's TYPE and matcher checks: batch T, a policy decision — see REVIEW-GRAND.)
check(tap('is-approx 1, 1.5, 1; is-approx 1e6, 1e6 + 2; is-approx 1e6, 1e6 + 0.5; done-testing'),
      (True, False, True), 'is-approx: a positional tolerance is absolute; the default is relative 1e-6');
check(tap('like "abc", "b"; done-testing').elems, 0, 'like refuses a Str matcher');

if $ok { say "PASS" } else { say "FAIL"; exit 1 }
