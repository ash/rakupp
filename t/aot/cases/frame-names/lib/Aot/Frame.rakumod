# Parameters, which a native body copies out of the frame the interpreter
# bound, and outer names, which it binds through that frame's scope chain on
# every call.
unit module Aot::Frame;

my $calls = 0;                      # a module variable, written by a routine
my @log;                            # …an array, pushed to
my %seen;                           # …a hash, written through a subscript
our $greeting = 'hello';            # an `our` variable
constant SEP = '/';                 # a constant

sub join-all(*@parts) {             #aot: native
    @parts.join(SEP)
}

sub positional($a, $b) is export {  #aot: native
    "$a-$b"
}

sub defaults($a, $b = 'B', :$c = 'C', :$d) is export {   #aot: native
    join-all($a, $b, $c, $d // 'no-d')
}

sub slurpy($first, *@rest, *%opts) is export {           #aot: native
    "$first+{@rest.elems}+{%opts.keys.sort.join(',')}"
}

sub no-signature is export {        #aot: native
    @_.elems ~ " positional, " ~ %_.elems ~ " named"
}

sub copied($s is copy) is export {  #aot: native
    $s ~= '!';
    $s
}

sub typed(Str $s, Int :$times = 2) is export {   #aot: native
    $s x $times
}

sub counter() is export {           #aot: native
    $calls++;
    @log.push("call $calls");
    $calls
}

sub remember(Str $k) is export {    #aot: native
    my $first = %seen{$k}:!exists;
    %seen{$k}++;
    $first ?? "first $k" !! "again $k ({%seen{$k}})"
}

sub log-lines() is export {         #aot: native
    @log.join('; ')
}

sub greet($who) is export {         #aot: native
    "$greeting, $who"
}

sub set-greeting($g) is export {    #aot: native
    $greeting = $g;
    Nil
}

# A lexical sub inside a routine belongs to that routine's native body.
sub outer-with-inner($s) is export {   #aot: native
    sub shout($x) { $x.uc ~ '!' }
    shout($s) ~ shout($s.flip)
}

# A routine whose name shadows a built-in: calls inside the module reach it.
sub uc($s) {                        #aot: native
    "uc<$s>"
}
sub uses-own-uc($s) is export {     #aot: native
    uc($s)
}

# A code parameter, called by the native body.
sub apply(&f, $x) is export {       #aot: native
    f(f($x))
}
