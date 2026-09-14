# An attribute trait whose role keeps BOTH public and private state, and a
# private attribute deliberately named like one of its own methods. Companion to
# t/regression/metamodel-mixin-and-trait-imports.raku.
unit module RakuppTraitProbe;

role ProbeRole is export {
    has %.args;
    has $!slot;          # …and `method slot` below: the private one must not shadow it
    has $!count;
    method peek-private() { %!args<n> // '-' }   # the PRIVATE spelling of a public attribute
    method slot() { $!slot // 'SLOT' }
    method bump() { $!count = ($!count // 1) + 1; $!count }
}

multi trait_mod:<is>(Attribute $attr, :%probed --> Empty) is export {
    $attr does ProbeRole(%probed);
}
multi trait_mod:<is>(Attribute $attr, Bool :$probed! --> Empty) is export {
    $attr does ProbeRole(%());
}
