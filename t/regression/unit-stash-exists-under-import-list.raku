# Regression: inside a module's `sub EXPORT`, `UNIT::{'&name'}:exists` was
# False and `UNIT::{'&name'}:p` empty when the importer passed an import
# list — while a plain `UNIT::{'&name'}` found the sub and `UNIT::.keys`
# listed it. The adverb forms looked for the unit's scope as the frame
# right under the global one, which a module's file scope is not when it
# is loaded for an import list; they now stop at the module's file scope.
# `UNIT::{'&EXPORT'}:exists` was False either way (issue #245).
# Every line answers the same under Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.
use lib $?FILE.IO.parent.add('lib').Str;
use RakuppUnitExistsProbe <rakupp-uep-greet>;   # (the list on its first load)

my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

my %r = uep-report-list();
my $how = 'with an import list';
check %r<names>, ('rakupp-uep-greet',), "the list, $how";
check %r<plain>, True, "a plain lookup, $how";
check %r<exists>, True, ":exists, $how";
check %r<angle>, True, "<…>:exists, $how";
check %r<pair>, 1, ":p, $how";
check %r<export>, True, "&EXPORT:exists, $how";
check %r<missing>, False, "a missing name, $how";
check %r<keys>, ('&rakupp-uep-greet',), "UNIT::.keys, $how";

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
