# Attributes in native method bodies: read inline (rtAotAttr), written inline
# when the value is defined (rtAotAttrAssign), and through the interpreter for
# Nil, type objects, shadowed private attributes and closures.
unit module Aot::Attr;

class Point is export {
    has Int $.x = 0;
    has Int $.y is rw = 0;
    has @.trail;
    has %.tags;
    has Str $.label;

    method read() {   #aot: native
        "$!x,$!y"
    }
    method move($dx, $dy) {   #aot: native
        $!x = $!x + $dx;
        $!y += $dy;
        @!trail.push("$!x,$!y");
        self
    }
    method bump() {   #aot: native
        $!x++;
        ++$!y;
        $!x * 10 + $!y
    }
    method tag($k, $v) {   #aot: native
        %!tags{$k} = $v;
        %!tags.keys.sort.join(',')
    }
    method reset-x() {   #aot: native
        $!x = Nil;
        $!x.^name ~ ' ' ~ ($!x // 'undefined')
    }
    method set-type-object() {   #aot: native
        $!label = Str;
        $!label.^name ~ ' ' ~ $!label.defined
    }
    method via-accessor() {   #aot: native
        self.y = 42;   #aot: delegated
        $.y
    }
    method trail-length() {   #aot: native
        @!trail.elems
    }
    # a parameter stored in an attribute must not take the binder's read-only
    # mark with it: the attribute is written again afterwards
    method relabel($l) {   #aot: native
        $!label = $l;
        $!label ~= '!';
        $!label
    }
    method adder() {   #aot: native
        -> $n { $!x + $n }
    }
}

class Base is export {
    has $!secret = 'base-secret';
    method base-secret() {   #aot: native
        $!secret
    }
    method set-base($s) {   #aot: native
        $!secret = $s;
        $!secret
    }
}

class Derived is Base is export {
    has $!secret = 'derived-secret';
    method derived-secret() {   #aot: native
        $!secret
    }
    method set-derived($s) {   #aot: native
        $!secret = $s;
        $!secret
    }
    method both() {   #aot: native
        self.base-secret ~ ' & ' ~ $!secret
    }
}

class Accessor is export {
    has $.n = 5;
    method n() { 'overridden ' ~ $!n }   #aot: native
    method public() {   #aot: native
        $.n
    }
    method private() {   #aot: native
        $!n
    }
}
