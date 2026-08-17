# BROKE: `rakupp mandelbrot.raku` printed the PGM header and died
# "Undefined routine 'hyper'". Two cooperating gaps:
#   * `hyper`/`race`/`eager`/`lazy`/`sink` were not statement prefixes, so
#     `my @a = hyper for 1..4 { $_ }` parsed as `for 1..4 { my @a = hyper }`
#   * a generator sequence (`$seed, * + $step ... $end`) is lazy, and
#     assignment / X+ / say only saw the already-materialised seed — so
#     `cut(-2 .. 1/2, 5)` was just `-2` and the image was empty
# FIXED: those words prefix a following `for` like `do` does; a finite
# lazy Seq drains when flattened, assigned to `@a`, listed, or gisted.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# statement-prefix hyper/race/eager collect the loop, like `do for`
check((hyper for 1..4 { $_ * 2 }).join(","), "2,4,6,8", "hyper for as expression");
check((race  for 1..4 { $_ * 2 }).join(","), "2,4,6,8", "race for as expression");
check((eager for 1..3 { $_ + 1 }).join(","), "2,3,4",   "eager for as expression");
{
    my @a = hyper for 1..4 { $_ * 2 };
    check(@a.join(","), "2,4,6,8", "hyper for assigned to @array");
}
# postfix on the collected list (mandelbrot's `.rotor`)
check((hyper for 1..6 { $_ }).rotor(2).map(*.join(" ")).join("|"),
      "1 2|3 4|5 6", "hyper for then .rotor");

# a finite generator sequence is no longer just its seed
check((1, { $_ + 1 } ... 5).join(","), "1,2,3,4,5", "say/join drains a generator seq");
check((1, { $_ + 1 } ... 5).gist, "(1 2 3 4 5)", "gist drains a generator seq");
{
    my @a = 1, { $_ + 1 } ... 5;
    check(@a.WHAT.^name, "Array", "assignment of a generator seq is an Array");
    check(@a.join(","), "1,2,3,4,5", "and holds every element");
}
check(((-2, * + 5/8 ... 1/2)).join(","),
      "-2,-1.375,-0.75,-0.125,0.5", "WhateverCode generator with a Rat endpoint");

# the cut() used by mandelbrot.raku, and X+ / X* over its result
sub cut(Range $r, UInt $n where $n > 1 --> Seq) {
    $r.min, * + ($r.max - $r.min) / ($n - 1) ... $r.max
}
check(cut(-2 .. 1/2, 5).join(","), "-2,-1.375,-0.75,-0.125,0.5", "cut subdivides a Range");
{
    my @re = cut(-2 .. 1/2, 3);
    my @im = cut(0 .. 1, 2) X* 1i;
    check(@re.elems, 3, "assigned cut has every real sample");
    check(@im.elems, 2, "cut X* 1i has every imag sample");
    check((@im X+ @re).elems, 6, "X+ of two cut results is the cartesian product");
}

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL'; exit 1 } else { say 'PASS' }
