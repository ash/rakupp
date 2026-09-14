# Regression: `use if;` loads, and `use Foo:if(EXPR)` skips the load when
# EXPR is false. The ecosystem's `if` dist exists for that one colonpair, and
# rakupp reads the pair itself — so the dist has nothing to add, and loading
# it anyway ran its actions-only slang into the slang refusal:
#
#     `use if`: it changes only the actions (`statement-control:sym<use>`),
#     which rakupp cannot apply
#
# That refused the dist's own suite, so `rakupp install if` failed, and with
# it Crypt::Random (whose first line is `use if;`) and UUID::V4 behind it.
#
# Both engines run this: under Rakudo the dist is real and does the work;
# under Raku++ the name is answered before any file is looked for. The
# expectations were checked against Rakudo with the dist installed.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# `use if;` — accepted, loads nothing observable
ck((try { EVAL 'use if; 1' }) // 'refused', 1, 'use if; is accepted');

# a false :if skips the load entirely: a module that does not exist is fine
ck((try { EVAL 'use if; use No::Such::Module::Here:if(0); 2' }) // 'refused', 2,
   ':if(0) skips a module that does not exist');
ck((try { EVAL 'use if; use No::Such::Module::Here:if($*RAKU.version ~~ v7); 3' }) // 'refused', 3,
   'the condition is a real expression');

# a true :if still loads — and still fails for a module that is not there
my $r = try { EVAL 'use if; use No::Such::Module::Here:if(1); 4' };
ck($r.defined, False, ':if(1) does try to load');

# …and a module that IS there arrives with its exports
ck((try { EVAL 'use if; use Test:if($*RAKU.version ~~ v6.*); &ok.defined' }) // 'refused', True,
   ':if(True) loads the module and imports');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
