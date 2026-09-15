# `::?CLASS` as the type of a NAMED parameter. The signature parser read the
# colon that opens the named as the INVOCANT marker — legal after `::?CLASS`,
# which is how `(::?CLASS:U: Bar $b)` is written — so `:$copy` became a required
# POSITIONAL. CSS::Properties' `submethod TWEAK(… ::?CLASS :$copy, …)` then
# refused every `.new` with "Too few positionals passed", and with another named
# declared ahead of it the signature would not even parse.
use Test;
plan 4;

class C {
    has $.v;
    submethod TWEAK(::?CLASS :$copy, Str :$units) { }
}
is C.new(v => 1).v, 1,                  'a ::?CLASS named does not make a positional';

class D {
    has $.v;
    submethod TWEAK(Str :$style, ::?CLASS :$copy, :module($)) { }
}
is D.new(v => 2).v, 2,                  '…even after another named parameter';

class E {
    method m(::?CLASS :$copy) { $copy.defined ?? 'got' !! 'none' }
}
is E.m, 'none',                         'the named is optional, as any named is';
is E.m(copy => E.new), 'got',           '…and binds when passed by name';
