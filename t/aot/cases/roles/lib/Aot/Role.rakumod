# Role methods get native bodies like class methods. A parametric role's
# parameters are outer names of its methods: one native body serves every
# instantiation, binding them through the frame on each call.
unit module Aot::Role;

role Named is export {
    has $.name = 'anon';
    method greet() {   #aot: native
        "I am $!name, a {self.kind}"
    }
    method !secret() {   #aot: native
        "{$!name}'s secret"
    }
    method tell() {   #aot: native
        self!secret
    }
}

class Dog does Named is export {
    has $.sound = 'woof';
    method kind() {   #aot: native
        'dog'
    }
    method speak() {   #aot: native
        self.greet ~ " who says $!sound"
    }
}

role Scaled[$factor] is export {
    method scale($x) {   #aot: native
        $x * $factor
    }
    method factor() {   #aot: native
        "factor $factor"
    }
}
class Twice does Scaled[2] is export { }
class Thrice does Scaled[3] is export { }

role Typed[::T] is export {
    has T @.items;
    method add(T $x) {   #aot: native
        @!items.push($x);
        self
    }
    method kind() {   #aot: native
        T.^name ~ '[' ~ @!items.elems ~ ']'
    }
}
class Ints does Typed[Int] is export { }
class Strs does Typed[Str] is export { }

role Loud is export {
    method shout() {   #aot: native
        self.speak.uc ~ '!'
    }
}
# `but` and `does` are not compiled natively: those statements are delegated,
# and a `does` on a parameter changes the caller's object (it is not a copy)
sub louder($d) is export {   #aot: native
    my $l;
    $l = $d but Loud;   #aot: delegated
    $l
}
sub make-loud($d) is export {   #aot: native
    $d does Loud;   #aot: delegated
    $d.shout
}
