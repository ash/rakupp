# Regression: an import is LEXICAL. rakupp published every routine a module
# loaded into GLOBAL as well — the exported ones after importing them into the
# `use`'s own scope, and the rest of the module's routines too — so `{ use Foo }`
# left `foo()` callable after the block, a dependency's exports reached the
# program, and an EVAL parsed operators nobody imported (roast
# S11-modules/lexical.t, importing.t, S10-packages/export.t,
# S06-operator-overloading/imported-subs.t). What GLOBAL does get stays: a
# package-less module's `our sub`, and an exported code variable. A `require` stubs its
# package at compile time, and a `require "File"` in a method keeps what the file
# declares to that method (S11-modules/require.t).
#
# Every expectation below was checked against Rakudo 2026.08.

use lib $?FILE.IO.parent.add('lib').Str;
use MONKEY-SEE-NO-EVAL;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

{
    use RakuppLexA;
    ck(lexa-exported(), 'exported:helper', 'an import works in its block');
}
ck((try EVAL 'lexa-exported()') // 'gone', 'gone', '…and ends with it');
ck((try EVAL 'lexa-helper()') // 'gone', 'gone', 'a module keeps its unexported subs');

use RakuppLexB;
ck(lexb(), 'exported:helper', 'a module calls what it imported');
ck((try EVAL 'lexa-exported()') // 'gone', 'gone', '…which stays its own');

use RakuppLexG;
ck(lexg-global(), 'global', "a package-less module's our sub is GLOBAL's");

use RakuppLexV;
ck(lexv-code(), 'code-var', 'an exported code variable is imported');

# a class a BLOCK's `use` brought in is that block's
{
    use RakuppLexClass;
    ck(RakuppLexClass.^name, 'RakuppLexClass', 'a class used in a block is there');
}
ck((try EVAL 'RakuppLexClass.^name') // 'gone', 'gone', '…and gone after it');

# a `require` names its package at compile time, and what a `require "File"`
# inside a method brings in stays in that method
my $req-late = (require RakuppReqLate);
my $req-stub;
BEGIN try EVAL '$req-stub = RakuppReqLate';
ck($req-stub.gist, '(RakuppReqLate)', 'a require stubs its package at compile time');
{
    require "RakuppReqOuter.rakumod";
    ck(::('RakuppReqOuter').load, True, 'a file required in a method is there');
    ck(::('RakuppReqInner') ~~ Failure, True, '…and only there');
}

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
