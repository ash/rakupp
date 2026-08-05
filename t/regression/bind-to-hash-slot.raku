# Regression: `$x := %h{key}` binds the hash SLOT, so a later `$x = v` writes
# into the hash — including a walk that rebinds one level at a time and
# autovivifies as it goes. Config's `.set` is written exactly this way.
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my %h = a => 1;
my $s := %h<a>;
$s = 2;
ck %h<a>, 2, 'assigning through the binding writes into the hash';

my %n = a => { b => 'old' };
my $index := %n;
for <a b> { $index := $index{$_} }
$index = 'new';
ck %n<a><b>, 'new', 'a rebinding walk reaches the nested slot';

my %fresh;
my $i2 := %fresh;
for <x y z> { $i2 := $i2{$_} }
$i2 = 'v';
ck %fresh<x><y><z>, 'v', 'and autovivifies the whole path';

# reading through the binding sees later writes to the hash
my %r = k => 1;
my $b := %r<k>;
%r<k> = 9;
ck $b, 9, 'the binding reads the current slot, not a copy';

# a binding taken from a hash that is itself shared storage
my %outer = inner => {};
my %alias := %outer;
my $slot := %alias<inner>;
$slot = 'set';
ck %outer<inner>, 'set', 'through an aliased hash too';

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
