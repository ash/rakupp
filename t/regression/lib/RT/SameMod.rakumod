unit module RT::SameMod;
class Thing is export { has Str $.name }
sub make(Str $n --> Thing) is export { Thing.new(name => $n) }
sub describe(Thing $t --> Str) is export { 'thing: ' ~ $t.name }
