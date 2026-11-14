# Regression (found by the module battery, Data::Dump): when multi dispatch
# checks an unsupplied named's `where` against its default, the defaults it
# evaluates for that read the CANDIDATE's lexicals, not the caller's.
#
# Before the fix they were evaluated in the caller's scope. A default reading
# a lexical the caller cannot see, like Data::Dump's module-level `my
# %defaults` behind `Bool :$color? = %defaults<color>`, died there, and the
# candidate was dropped: every `Dump(…)` was X::Multi::NoMatch. Broken since
# 7e584de8 (v5.3.0), which began evaluating the earlier parameters' defaults
# for that check.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Str $what, $got, $want) {
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

my &g;
{
    my %defaults = color => True, indent => 2;
    multi f($x, Bool :$color? = %defaults<color>, Int :$indent = %defaults<indent>,
            :%overrides where { !$_.values.grep: * !~~ Sub } = {}) { "$x $color $indent" }
    &g = &f;
}
check 'the defaults, from outside their scope', g(1), '1 True 2';
check 'a named given', g(1, :!color), '1 False 2';
check 'the where still checks a given value', (try g(1, overrides => { a => 1 })) // 'no match', 'no match';
check '…and passes a good one', g(1, overrides => { a => sub {} }), '1 True 2';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
