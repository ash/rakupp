# Regression: `for @a.values { $_ = 9 }` left @a alone.
#
# `@a.values` and `@a.list` are VIEWS of the array. Rakudo hands the loop each
# element's own container, so a write through the topic lands in @a exactly as
# `for @a` does. Ours copies the elements out, so the view had to be recognised
# by name and the real storage iterated instead — valuesAliasSource already did
# that for `%h.values`, and had no array arm, so an Array invocant fell through
# to the ordinary by-value path and the write went nowhere.
#
# The same gate took a second bug with it: the hash arm bound its value
# straight into the slot, bypassing the readonly mark, so `for %h.values -> $v
# { $v = 9 }` edited the hash where Rakudo refuses it. Aliasing is what `$_`
# and `<->` do; a plain `-> $v` is a readonly parameter either way.
#
# Contract: exit 0 + last line PASS. Runs under Rakudo unchanged, so it doubles
# as the oracle that says these are Rakudo's answers and not ours — every
# expectation below was checked against Rakudo 2026.08, 2026-09-20.
use MONKEY-SEE-NO-EVAL;
my @fail;

sub check($desc, $got, $want) {
    @fail.push("$desc: got {$got.gist}, want {$want.gist}") unless $got eqv $want;
}

# the view writes through, for both spellings and both call forms
my @v = 1, 2, 3;
for @v.values { $_ = 9 }
check('@a.values', @v, [9, 9, 9]);

my @l = 1, 2, 3;
for @l.list { $_ = 9 }
check('@a.list', @l, [9, 9, 9]);

my @s = 1, 2, 3;
for values(@s) { $_ = 9 }
check('values(@a)', @s, [9, 9, 9]);

my @sl = 1, 2, 3;
for list(@sl) { $_ = 9 }
check('list(@a)', @sl, [9, 9, 9]);

# a view of ONE array is the array; `list(@a, @b)` is a new list of its own,
# and Rakudo refuses the write rather than sending it anywhere
my @nested = [1, 2], [3, 4];
for @nested.list { $_ = 9 }
check('.list over a nested array', @nested, [9, 9]);

# …through a scalar holding the array, and through a parameter
my $r = [1, 2, 3];
for $r.values { $_ = 9 }
check('$r.values', $r, [9, 9, 9]);

sub via-param(@x) { for @x.values { $_ = 9 } }
my @p = 1, 2, 3;
via-param(@p);
check('@param.values', @p, [9, 9, 9]);

# the statement-modifier form reaches the same storage
my @m = 1, 2, 3;
$_ = 9 for @m.values;
check('modifier form', @m, [9, 9, 9]);

# any mutation, not just assignment
my @inc = 1, 2, 3;
for @inc.values { $_++ }
check('$_++ through the view', @inc, [2, 3, 4]);

my @cat = <a b>;
for @cat.values { $_ ~= '!' }
check('~= through the view', @cat, ['a!', 'b!']);

# `<->` and `is rw` alias too — and a plain `-> $v` is READONLY, view or not
my @arrow = 1, 2, 3;
for @arrow.values <-> $x { $x = 9 }
check('<-> over the view', @arrow, [9, 9, 9]);

my @rwt = 1, 2, 3;
for @rwt.values -> $x is rw { $x = 9 }
check('is rw over the view', @rwt, [9, 9, 9]);

sub dies-ro($desc, $code) {
    my $msg = '';
    { EVAL $code; CATCH { default { $msg = .Str } } }
    @fail.push("$desc: no die") unless $msg;
    @fail.push("$desc: wrong message ($msg)")
        if $msg && !$msg.contains('Cannot assign to a readonly variable or a value');
}
dies-ro('-> $v over @a.values', 'my @a = 1,2,3; for @a.values -> $v { $v = 9 }');
dies-ro('-> $v over %h.values', 'my %h = a => 1; for %h.values -> $v { $v = 9 }');

# the hash view still aliases for the two spellings that may
my %hv = a => 1, b => 2;
for %hv.values { $_ = 9 }
check('%h.values', %hv.sort.list, (a => 9, b => 9));

my %hr = a => 1, b => 2;
for %hr.values <-> $x { $x = 9 }
check('<-> over %h.values', %hr.sort.list, (a => 9, b => 9));

# …and a view that is NOT the array must not be aliased: a Seq taken out into
# a variable has left the array behind, and writing the topic edits nothing
my @detached = 1, 2, 3;
my $seq = @detached.values;
for $seq { $_ = 9 }
check('a stored Seq is detached', @detached, [1, 2, 3]);

# reading is untouched, and the loop still stops where it is told to
my @read = 1, 2, 3;
my @seen;
for @read.values -> $v { @seen.push($v) }
check('reading the view', @seen, [1, 2, 3]);

my @early = 1, 2, 3;
for @early.values { $_ = 9; last }
check('last leaves the rest alone', @early, [9, 2, 3]);

die @fail.join('; ') if @fail;
say "PASS";
