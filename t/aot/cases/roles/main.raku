# Role methods, parametric roles, mixins (t/aot/run.raku).
use Aot::Role;

my $d = Dog.new(name => 'Rex');
say $d.greet;
say $d.speak;
say $d.tell;
say Twice.new.scale(10), ' ', Thrice.new.scale(10);
say Twice.new.factor, ', ', Thrice.new.factor;
say Ints.new.add(1).add(2).kind;
say Strs.new.add('a').kind;
my $loud = louder($d);
say $loud.shout;
my $d2 = Dog.new(name => 'Fido', sound => 'arf');
say make-loud($d2);
say $d2.shout;
say (try Ints.new.add('x')) // 'refused a Str';
