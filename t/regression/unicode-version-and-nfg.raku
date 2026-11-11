# Regression: `Unicode.version` and `Unicode.NFG` were X::Method::NotFound,
# and so was `Unicode.new`: a program could not ask which Unicode version the
# engine's tables come from (issue #252). Rakudo answers a Version and a Bool,
# on the class and on an instance. Raku++'s tables are Unicode 18.0 (the UCD
# files tools/gen-unicode.raku reads); Rakudo 2026.09's are 17.0.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

check(Unicode.version ~~ Version, True, 'Unicode.version is a Version');
check(Unicode.version.Str, '18.0', 'the tables are Unicode 18.0');
check(Unicode.version >= v15, True, 'it compares as a version');
check(Unicode.NFG ~~ Bool, True, 'Unicode.NFG is a Bool');
check(Unicode.NFG, True, 'strings are NFG');

my $u = Unicode.new;
check($u ~~ Unicode, True, 'Unicode.new is a Unicode');
check($u.version.Str, '18.0', 'an instance knows the version');
check($u.NFG, True, 'and NFG');
check($u.raku, 'Unicode.new(version => v18.0, NFG => Bool::True)', '.raku');
check($u.gist, 'Unicode.new(version => v18.0, NFG => Bool::True)', '.gist');
check(Unicode.new(version => v1, NFG => False).version.Str, '1', 'the constructor takes its attributes');

if @fail { .note for @fail; say "FAIL"; exit 1 }
say "PASS";
