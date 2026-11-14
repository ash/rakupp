# `use v6.e` without `.PREVIEW` is refused, as in Rakudo (rakudo#2783): 6.e is
# a preview, so a bare `use v6.e` is X::Language::ModRequired, and a revision
# or modifier no compiler exists for is X::Language::Unsupported. Both were
# accepted.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}
sub refused($code) { try EVAL $code; $! ?? $!.^name.subst(/'+{' .* $/, '') !! 'ok' }

check refused('use v6.e; 1'), 'X::Language::ModRequired', 'use v6.e (rakudo#2783)';
check refused('use v6e; 1'), 'X::Language::ModRequired', 'use v6e';
check refused('use v6.f; 1'), 'X::Language::Unsupported', 'a revision with no compiler';
check refused('use v6.e.PREVIEW.foo; 1'), 'X::Language::Unsupported', 'an unknown modifier';
check refused('use v6.c.PREVIEW; 1'), 'X::Language::Unsupported', '6.c has no preview';
check refused('use v6.d; 1'), 'ok', 'use v6.d';
check refused('use v6.d.PREVIEW; 1'), 'ok', 'use v6.d.PREVIEW';
check refused('use v6; 1'), 'ok', 'use v6';
try EVAL 'use v6.e; 1';
check $!.message, 'Raku v6.e requires PREVIEW modifier', 'the message';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
