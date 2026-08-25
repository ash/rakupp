# NativeHelpers::Pointer, the rakupp shadow — the third module of the
# NativeHelpers::Blob distribution, and MoarVM-only for the same reason as the
# other two: it reaches for `nqp::` to unbox a pointer and for
# `NativeCall::Types::Pointer.^add_method` to graft methods onto a core type.
# Here a Pointer already answers its own address, so the arithmetic is ordinary
# Raku. The dist's surface is kept: Pointer.add / .succ / .pred, and the
# exported `+` / `-` / `++` / `--` over a Pointer and an offset.
#
# A `void` pointer has no element size, so arithmetic on one dies — as it does
# in C, and as the ecosystem module does.

unit module NativeHelpers::Pointer;

use NativeCall;

my sub elem-size(\p) {
    my \t = p.of;
    # an UNPARAMETERISED Pointer is C's `void *` — it has no element size, so
    # arithmetic on one dies, as it does in C and in the ecosystem module
    die "Can't do arithmetic with a void pointer"
        if t === Mu || t === Any || ((try t.^name) // '') eq 'void';
    nativesizeof(t)
}
my sub shift-by(\p, Int $off) {
    Pointer[p.of].new(+p + $off * elem-size(p))
}

# `.add` is what the dist grafts onto Pointer itself; rakupp cannot reopen a
# built-in type, so it ships as an exported multi and reads the same at the
# call site for the one spelling dependents use.
multi sub pointer-add(Pointer:D \p, Int $off) is export { shift-by(p, $off) }

multi sub infix:<+>(Pointer:D \p, Int $off) is export { shift-by(p, $off) }
multi sub infix:<+>(Int $off, Pointer:D \p) is export { shift-by(p, $off) }
multi sub infix:<->(Pointer:D \p, Int $off) is export { shift-by(p, -$off) }
multi sub prefix:<++>(Pointer:D \p is rw) is export { p = shift-by(p, 1) }
multi sub prefix:<-->(Pointer:D \p is rw) is export { p = shift-by(p, -1) }

# vim: expandtab shiftwidth=4
