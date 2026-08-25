# NativeHelpers::CStruct, the rakupp shadow — same reasoning as the
# NativeHelpers::Blob shadow beside it: the ecosystem dist reads MoarVM's
# REPR memory layout (BODY_OF(...).cstruct) to find a struct's native body,
# which only MoarVM can answer. Here a CStruct instance already carries its
# native pointer, and Rakupp::Internals::Blob.addr answers it; everything
# else is plain NativeCall over calloc'd memory. The dist's surface is kept:
# LinearArray[T] with new / new-from-pointer / ASSIGN-POS / dispose /
# nativesizeof / Pointer / base / typed-pointer / _Pointer, and the
# module-level pointer-to for CStruct instances (DBDish::mysql binds its
# result buffers through exactly these).

unit module NativeHelpers::CStruct;

use NativeCall;

constant stdlib = Str;
our $debug = False;

# The element size is asked for INSIDE the methods, never bound to a role-body
# lexical (`my int $sol = nativesizeof(T)`, which is what the ecosystem dist
# writes). A role body runs ONCE, at declaration, before any `[T]` is bound —
# Rakudo re-runs it per parameterisation, Raku++ does not — so a role-body
# lexical derived from T is either the unbound name or a hard error here.
# Inside a method T *is* bound, so this spelling works on both engines.
role LinearArray[::T] does Positional is export {

    has Pointer $!storage;
    has @!cache handles <AT-POS elems>;
    has Int $!size;
    has $.managed;

    sub calloc(size_t, size_t --> Pointer) is native(stdlib) {*}

    submethod BUILD(:$!size!, :$!storage!, :$!managed) {
        @!cache = ();
        for ^$!size {
            my Pointer $p .= new(+$!storage + $_ * nativesizeof(T));
            @!cache[$_] = nativecast(T, $p);
        }
        self
    }

    method new(::?CLASS:U: Int $size) {
        with calloc($size, nativesizeof(T)) -> $storage {
            self.bless(:$size, :$storage, :managed)
        }
        else {
            fail "Can't allocate memory";
        }
    }

    method new-from-pointer(::?CLASS:U: Int :$size, Pointer :$ptr) {
        self.bless(:$size, :storage(nativecast(Pointer, $ptr)), :!managed)
    }

    method ASSIGN-POS(::?CLASS:D: $idx, T:D \st) {
        sub memmove(Pointer, Pointer, size_t) is native(stdlib) {*}

        memmove(self._Pointer($idx), pointer-to(st), nativesizeof(st))
    }

    method dispose(::?CLASS:D:) {
        sub free(Pointer) is native(stdlib) {*}

        with $!storage {
            @!cache = ();
            free($!storage) if $!managed;
            $!storage = Pointer;
            True
        }
        else {
            False
        }
    }

    method nativesizeof() { nativesizeof(T) * $!size }

    multi method Pointer(::?CLASS:D: :$typed) {
        $typed ?? nativecast(Pointer[T], $!storage) !! $!storage
    }

    method base() { @!cache[0] }

    # Back-compat for DBIish's mysql
    method typed-pointer() { @!cache[0] }

    method _Pointer(Int $idx) {
        Pointer.new(+$!storage + $idx * nativesizeof(T))
    }

    multi method Pointer(::?CLASS:U: T:D $struct) {
        Rakupp::Internals::Blob.addr($struct)
    }
}

multi sub pointer-to(
  Mu:D $struct where .REPR eq 'CStruct', :$typed
) is export {
    my \t = $struct.WHAT;
    my \ptr = Rakupp::Internals::Blob.addr($struct);
    $typed ?? nativecast(Pointer[t], ptr) !! ptr
}

# vim: expandtab shiftwidth=4
