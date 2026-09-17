# Regression: three one-assertion Roast losses from the 2026-09-17 ceiling board.
#
#   CONTROL without .resume — a handler whose `when`/`default` matched a warning
#       and did not `.resume` consumes the warning AND leaves the block that
#       declared the CONTROL, as a CATCH leaves its block. We printed the warning
#       anyway and ran on. S32-basics/warn.t plans on the exit: two tests after
#       its first `warn` are never reached, and the plan of 9 counts them out.
#   ~Any — a type object in string context warns "Use of uninitialized value of
#       type Any in string context." and yields ""; it was silent. A type object
#       that declares its own `method Str` still stringifies through it
#       (S24-testing/14-like-unlike.t).
#   no strict; class Foo { $foo = 42 } — the lax variable is the package's, so
#       `$Foo::foo` reads it (S02-names/strict.t); it was never published.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

{
    my @trace;
    { CONTROL { when CX::Warn { @trace.push("caught " ~ .message) } }; warn "x"; @trace.push("after") }
    @trace.push("outside");
    check @trace, ["caught x", "outside"], 'a matched CONTROL without .resume leaves the block';
    @trace = ();
    { CONTROL { when CX::Warn { @trace.push("caught2"); .resume } }; warn "y"; @trace.push("after2") }
    check @trace, ["caught2", "after2"], '.resume carries on after the warn';
    @trace = ();
    { CONTROL { default { @trace.push("dflt") } }; warn "z"; @trace.push("after3") }
    check @trace, ["dflt"], 'a default handler counts as handled';
}

{
    my $caught = '';
    { CONTROL { when CX::Warn { $caught = .message; .resume } }; my $s = ~Any; check $s, '', '~Any is the empty string' }
    check ?$caught.contains('Use of uninitialized value of type Any in string context.'), True, '~Any warns';
    check ~(class { method Str { 'foo' } }), 'foo', 'a type object with its own Str stringifies through it';
}

check EVAL('no strict; class RegrLaxPkg { $lax = 42 }; $RegrLaxPkg::lax'), 42, 'a lax variable in a class body is the package variable';

.say for @fail;
say @fail ?? "FAIL" !! "PASS";
exit +?@fail;
