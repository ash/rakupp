# The core types whose `new` is Mu's take named arguments only, as Rakudo's
# do: `Str.new(1)` answered "1", and `"Date".new(1)` the empty string, so a
# test probe written as `"$t.new(…)"` printed nothing instead of dying.
# `"x".new(:value<a>)` is Str.new's "a", where it answered "".
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub dies(&c) { try { c(); return 'lived' }; $!.^name ~ ': ' ~ $!.message }

for Str, Any, Cool, Mu, Match, Exception, Capture, Signature -> \T {
    ck dies({ T.new(1) }),
       "X::Constructor::Positional: Default constructor for '{T.^name}' only takes named arguments",
       "{T.^name}.new(1)";
}
ck dies({ "Date".new(1) }),
   "X::Constructor::Positional: Default constructor for 'Str' only takes named arguments",
   'a Str instance';
my $t = "Date";
ck dies({ "[$t.new(1)]" }),
   "X::Constructor::Positional: Default constructor for 'Str' only takes named arguments",
   'interpolated';
ck "x".new(:value<a>), 'a', '"x".new(:value<a>)';
ck Str.new(:value<a>), 'a', 'Str.new(:value<a>)';
ck Str.new, '', 'Str.new';
ck "x".new, '', '"x".new';
ck Any.new.raku, 'Any.new', 'Any.new';

# the types with a `new` of their own still take positionals
ck Int.new(1), 1, 'Int.new(1)';
ck Num.new(1), 1e0, 'Num.new(1)';
ck List.new(1, 2), (1, 2), 'List.new(1, 2)';
ck Version.new(1).raku, 'v1', 'Version.new(1)';
ck Set.new(1), set(1), 'Set.new(1)';

say 'PASS';
