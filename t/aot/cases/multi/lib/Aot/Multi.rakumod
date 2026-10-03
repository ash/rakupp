# Each candidate of a multi is a routine of its own: some get native bodies,
# some stay interpreted, and the interpreter's dispatcher picks among them —
# redispatching from one kind to the other included.
unit module Aot::Multi;

proto describe($) is export {*}   #aot: interpreted (proto)
multi describe(Int $n) {   #aot: native
    "Int $n"
}
multi describe(Str $s) {   #aot: interpreted (phaser)
    LEAVE { }
    "Str $s"
}
multi describe(@a [$first, *@]) {   #aot: interpreted (sub-signature)
    "list starting $first"
}
multi describe(Rat $r) {   #aot: native
    "Rat {$r.nude.join('/')}"
}
multi describe(Any $x) {   #aot: native
    'something else'
}

# a proto that wraps its candidates
proto wrapped($) is export {   #aot: interpreted (proto)
    '<' ~ {*} ~ '>'
}
multi wrapped(Int $n) {   #aot: native
    "int $n"
}
multi wrapped(Str $s is copy) {   #aot: native
    $s .= uc;
    "str $s"
}

# redispatch between native and interpreted candidates
proto chain($) is export {*}   #aot: interpreted (proto)
multi chain(Int $n) {   #aot: native
    'Int>' ~ callsame()
}
multi chain(Numeric $n) {   #aot: interpreted (phaser)
    LEAVE { }
    'Numeric>' ~ callsame()
}
multi chain(Cool $n) {   #aot: native
    'Cool'
}

class Shape is export {
    has $.name;
    multi method area(Int $side) {   #aot: native
        "$!name: square {$side * $side}"
    }
    multi method area(Int $w, Int $h) {   #aot: interpreted (phaser)
        LEAVE { }
        "$!name: rect {$w * $h}"
    }
    multi method area(Str $what) {   #aot: native
        "$!name: no area for $what"
    }
}
