# Regression: the Mu-level universal methods — role membership, `Mu.new`,
# `.item`, `.Capture` — and the IterationEnd sentinel.
#   * `.does` counts the ROLES in a built-in's ancestry (`Date.does(Dateish)`),
#     while `.isa` is strict class inheritance and must reject them. The two were
#     answering each other's results: .does(Dateish) was False and .isa(Dateish)
#     True. Smartmatch agrees with .does, for an INSTANCE as well as a type.
#   * `Mu.new` / `Any.new` had no implementation at all: an instance of the bare
#     root type, defined and therefore truthy, gisting as `Mu.new`.
#   * `.item` (and the `item(…)` sub) itemizes a Hash as well as an Array.
#   * `$obj.Capture` is the object's public attributes as named arguments, read
#     through their ACCESSORS — so a method that overrides an attribute wins.
#   * IterationEnd is a sentinel, not a type object, so it gists bare.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# .does counts roles, .isa does not
my $d = Date.new('2016-06-03');
check($d.does(Dateish),  'True',  'a-date-does-dateish');
check($d.does(Any),      'True',  'and-is-an-any');
check($d.does(DateTime), 'False', 'but-is-not-a-datetime');
check($d.isa(Dateish),   'False', 'isa-rejects-a-role');
check($d.isa(Date),      'True',  'isa-accepts-its-own-class');
check($d.isa(Any),       'True',  'isa-accepts-an-ancestor');
check(($d ~~ Dateish).Bool,  'True',  'smartmatch-agrees-with-does');
check(($d ~~ Any).Bool,      'True',  'smartmatch-any');
check(($d ~~ DateTime).Bool, 'False', 'smartmatch-rejects-the-wrong-class');
check((Date ~~ Dateish).Bool, 'True', 'the-type-object-does-it-too');
# the numeric tower keeps the same split
check(5.does(Numeric), 'True',  'an-int-does-numeric');
check(5.isa(Numeric),  'False', 'but-is-not-an-isa-numeric');
check(5.isa(Int),      'True',  'an-int-isa-int');
check(Int.^mro.gist,   '((Int) (Cool) (Any) (Mu))', 'mro-excludes-roles');

# Mu.new
check(Mu.gist,          '(Mu)',   'the-mu-type-object');
check(Mu.new.gist,      'Mu.new', 'an-instance-of-mu');
check(Mu.Bool,          'False',  'a-type-object-is-false');
check(Mu.new.Bool,      'True',   'an-instance-is-true');
check(Mu.new.defined,   'True',   'and-defined');
check(Mu.defined,       'False',  'the-type-object-is-not');
check(Mu.new.^name,     'Mu',     'and-knows-its-name');
check(Any.new.gist,     'Any.new', 'any-new-too');
# the other Bool cases from the same documentation table
check([1, 2, 3].Bool, 'True',  'a-full-array');
check([].Bool,        'False', 'an-empty-array');
check(%( hash => 'full' ).Bool, 'True', 'a-full-hash');
check({}.Bool,        'False', 'an-empty-hash');
check("".Bool,        'False', 'the-empty-string');
check("0".Bool,       'True',  'the-string-zero');
check(0.Bool,         'False', 'the-number-zero');

# .item on either container kind
check([1, 2, 3].item.raku,          '$[1, 2, 3]',    'item-on-an-array');
check(%( apple => 10 ).item.raku,   '${:apple(10)}', 'item-on-a-hash');
check(item([1, 2, 3]).raku,         '$[1, 2, 3]',    'the-item-sub');
check(item("abc").raku,             '"abc"',         'item-leaves-a-scalar-alone');

# $obj.Capture reads through the accessors
class Foo { has $.foo = 42; has $.bar = 70; method bar { 'something else' } }
check(Foo.new.Capture.gist, '\(:bar("something else"), :foo(42))', 'object-capture');
check(Foo.new.Capture.^name, 'Capture', 'and-it-is-a-capture');

# IterationEnd is a sentinel
my $it = Mu.iterator;
check($it.pull-one.gist, '(Mu)',        'the-one-element');
check($it.pull-one.gist, 'IterationEnd', 'then-the-sentinel-gists-bare');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
