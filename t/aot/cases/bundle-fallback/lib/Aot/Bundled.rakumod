# The program that uses this module cannot be compiled natively, so `--exe`
# bundles it with the interpreter — and still compiles this module's routines
# to native bodies (a plain `--bundle` would not).
unit module Aot::Bundled;

my %cache;

sub fib(Int $n) is export {   #aot: native
    return %cache{$n} if %cache{$n}:exists;
    %cache{$n} = $n < 2 ?? $n !! fib($n - 1) + fib($n - 2)
}
sub words-by-length(Str $s) is export {   #aot: native
    $s.words.classify(*.chars).sort(*.key).map({ "{.key}:{.value.join(',')}" }).join(' ')
}
class Stack is export {
    has @!items;
    method push($x) {   #aot: native
        @!items.push($x);
        self
    }
    method pop() {   #aot: native
        @!items.pop
    }
    method Str() {   #aot: native
        '[' ~ @!items.join(' ') ~ ']'
    }
}
role Tagged is export {
    method tag() {   #aot: native
        'tagged:' ~ self.Str
    }
}
