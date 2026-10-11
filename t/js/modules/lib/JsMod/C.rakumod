# A `unit class` whose methods call a sub and read a hash declared in the
# class body.
unit class JsMod::C;
has $.n;
method doubled() { nums($!n) }
method cached(::?CLASS:U: Int $k) { %cache{$k} //= self.new(n => $k) }
my %cache;
sub nums($v) { ($v, $v * 2).join(',') }
