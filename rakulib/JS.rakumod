# `use JS` — the JavaScript interop surface (docs/guide/JS.md, TRANSPILE-PLAN P4).
#
# Under `rakupp --target=js` the term `JS` is the host's globalThis and a
# JS::Object is an opaque handle to a JavaScript object: `.name(args)` calls
# the property when it is a function and reads it otherwise, `$o<name>` reads
# a property, `$o<name> = v` writes one, and `EVAL 'code', :lang<JavaScript>`
# evaluates a literal.
#
# Under the interpreter there is no JavaScript host: this stub makes `use JS`
# load and every use die with a message that names the mode that has it, so a
# program written for the browser fails at the first interop call rather than
# at compile time. (The transpiler never uses this file — it recognises
# `use JS` itself.)

class JS::Object {
    method FALLBACK($name, |c) {
        die "JS interop ($name on a JS::Object) is only available in a program transpiled with --target=js";
    }
    method AT-KEY($k)     { self.FALLBACK($k) }
    method ASSIGN-KEY($k, $v) { self.FALLBACK($k) }
    method gist { 'JS::Object' }
    method Str  { 'JS::Object' }
}

class JS {
    method FALLBACK($name, |c) {
        die "JS interop (JS.$name) is only available in a program transpiled with --target=js";
    }
    method AT-KEY($k) { self.FALLBACK($k) }
}
