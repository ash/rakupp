# A class's FALLBACK does not take a method Any has, as Rakudo finds Any's
# first: `W.new.first` and `W.new.elems` answered "fb:first" and "fb:elems",
# where Rakudo treats the object as a one-item list (W.new, 1). And `.throw`,
# `.rethrow`, `.resume` and `.backtrace`, which are Exception's, go to the
# FALLBACK of an object that is no Exception: `W.new.throw` threw the object.
# An Exception subclass still throws.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

class W { method FALLBACK($n, |) { "fb:$n" } }
my $w = W.new;
ck $w.first.^name, 'W', '.first';
ck $w.elems, 1, '.elems';
ck $w.map({ 1 }).List, (1,), '.map';
ck $w.list.elems, 1, '.list';
ck $w.head.^name, 'W', '.head';
ck $w.grep(*.defined).elems, 1, '.grep';
ck $w.so, True, '.so';
ck $w.nope, 'fb:nope', 'a method Any has not';
ck $w.message, 'fb:message', '.message';
ck W.nope, 'fb:nope', 'on the type object';
ck (try $w.throw) // $!.^name, 'fb:throw', '.throw';
ck ($w.rethrow, $w.resume, $w.backtrace), ('fb:rethrow', 'fb:resume', 'fb:backtrace'), 'the rest of Exception’s';
ck $w.nope(1, 2), 'fb:nope', 'with arguments';
class E is Exception { method FALLBACK($n, |) { "e:$n" } }
ck (try E.new.throw) // $!.^name, 'E', 'an Exception throws';
ck E.new.foo, 'e:foo', 'and falls back';

say 'PASS';
