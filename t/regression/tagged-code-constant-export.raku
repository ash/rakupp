# Regression: a code constant exported under a tag comes in by that tag
# (issue #60).
#
# The import took a routine's tags from its `sub` declaration alone. An
# exported `constant &sp is export(:shortcuts) = &drop-spaces` had none on
# record, so it was taken for a plain `is export`: `use Mod :shortcuts`
# withheld it ("Undefined routine 'sp'") and a bare `use Mod` let it in.
# FunctionalParsers exports its shortcuts and extra parsers this way, and
# its EBNF parsers `use FunctionalParsers :shortcuts`. A code constant or
# `our &var` is now held to its own tags, as a sub is. Every answer below
# is Rakudo 2026.09's.
use lib $?FILE.IO.parent.add('lib').Str;

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

{
    use RakuppTaggedCodeConst :shortcuts;
    ck sp(1), 'ds(1)', ':shortcuts brings in its constant';
    ck tagged-sub(), 'tagged sub', '…and its sub';
    ck always(), 'always', 'MANDATORY comes in whatever was asked';
    ck MY::<&pFive>:exists, False, 'another tag stays out';
    ck MY::<&plain>:exists, False, 'and so does the DEFAULT set';
}
{
    use RakuppTaggedCodeConst :extra;
    ck pFive(), 5, ':extra brings in its constant';
    ck &pFive(), 5, '…called through its sigil too';
    ck counter(), 'counter', '…and its `our &` variable';
    ck MY::<&sp>:exists, False, ':shortcuts stays out';
}
{
    use RakuppTaggedCodeConst;
    ck plain(), 'plain', 'a bare use brings in the DEFAULT set';
    ck always(), 'always', '…and MANDATORY';
    ck MY::<&sp>:exists, False, 'but no tagged constant';
    ck MY::<&pFive>:exists, False, 'none at all';
    ck MY::<&tagged-sub>:exists, False, 'nor a tagged sub';
}
{
    use RakuppTaggedCodeConst :ALL;
    ck sp(2), 'ds(2)', ':ALL brings in every tag';
    ck pFive(), 5, '…all of them';
    ck plain(), 'plain', '…and the DEFAULT set';
}

say 'PASS';
