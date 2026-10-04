# `.new` on a plain class goes straight to the build (plainNewClass): a class
# with no `new`/`bless` of its own, no built-in parent, nothing the general
# path treats specially. The build is the same one — attribute defaults, the
# BUILD/TWEAK chain, a Failure from either, the refusal of positionals — and a
# class that is not plain takes the general path. Each case here is a thing
# the shortcut must still do, or must step aside for.
use Test;
plan 18;

class P { has $.x; has $.y = 7; has $!secret = 's'; method secret { $!secret } }
my $p = P.new(x => 1);
is "{$p.x} {$p.y} {$p.secret}", '1 7 s',      'named arguments and defaults';
is P.new(x => 1, secret => 'no').secret, 's',  'a private attribute is not set by name';
is P.new(x => 1, nope => 2).x, 1,              'an unknown name is ignored';
is P.new(x => 1, x => 2).x, 2,                 'the last of a repeated name wins';
throws-like { P.new(1) }, X::Constructor::Positional, 'a positional argument is refused';

class B { has $.v; has @.log; submethod BUILD(:$!v) { @!log.push('BUILD') }; submethod TWEAK { @!log.push('TWEAK') } }
my $b = B.new(v => 3);
is "{$b.v} {$b.log}", '3 BUILD TWEAK',         'BUILD and TWEAK run';

class Parent { has $.a = 'pa'; submethod TWEAK { $!a ~= '+' } }
class Child is Parent { has $.c = 'ch' }
my $ch = Child.new(c => 'x');
is "{$ch.a} {$ch.c}", 'pa+ x',                 'a parent\'s defaults and TWEAK';

class F { submethod TWEAK { fail 'no' } }
my $f = F.new;
ok $f ~~ Failure,                               'a Failure from TWEAK is what .new answers';
$f.so;

class T { has Int $.n; has Str $.s }
throws-like { T.new(n => 'x') }, X::TypeCheck,  'a typed attribute checks its argument';
is T.new.n.^name, 'Int',                        'an unset typed attribute is its type object';

class R { has $.r is required }
throws-like { R.new }, X::Attribute::Required,  'is required (not a plain class)';

class OwnNew { has $.z; method new(:$z) { self.bless(z => $z * 2) } }
is OwnNew.new(z => 5).z, 10,                    'a class with its own new';

class IsStr is Str { has $.extra }
is IsStr.new(value => 'abc', extra => 1).extra, 1, 'a class with a built-in parent';

role Ro { has $.ro = 'role' }
class WithRole does Ro { has $.w }
is WithRole.new(w => 1).ro, 'role',             'a role\'s attribute default';

my class Lex { has $.l }
is Lex.new(l => 9).l, 9,                        'a lexical class';

class Cnt { my $made = 0; submethod BUILD { $made++ }; method made { $made } }
Cnt.new for ^3;
is Cnt.made, 3,                                 'BUILD runs once per construction';

class Arr { has @.a; has %.h }
my $arr = Arr.new(a => (1, 2), h => { k => 1 });
is "{$arr.a.^name} {$arr.h.^name}", 'Array Hash', 'containers become their sigil\'s type';

class Later { has $.q }
Later.^add_method('new', method (*%a) { 'custom' });
is Later.new(q => 1), 'custom',                 'a new added at run time';
