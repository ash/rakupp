# Regression: `module M:ver<0.19> { ... }` — name adverbs on class-family
# declarations. Unhandled, the `:` failed the brace check, the package took
# the unit-form branch and swallowed the block up to its first `;`
# ("Confused (got '}')"). This made JSON::Fast 0.19 unparseable — first
# catch of the v2 module campaign (2026-07-22). NB the initial blame was on
# the adjacent Q/\u/ (finding #4) — Q// was innocent.

my $ok = True;

module M:ver<0.19> {
    our sub f() { my $x = 1 + 2; $x }
}
unless M::f() == 3 {
    say 'FAIL: module:ver body misparsed';
    $ok = False;
}

class C:ver<1.2.3>:auth<zef:test>:api<2> {
    method m { my $a = 1; $a + 41 }
}
unless C.new.m == 42 {
    say 'FAIL: class with three name adverbs';
    $ok = False;
}

say 'PASS' if $ok;
