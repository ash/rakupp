# Regression: seven numeric answers that were wrong in the same way — a value
# was converted when it should have been refused, or measured with the wrong
# ruler (Int-Num-Rat semantics sheet N-06, N-13, N-18, N-19, N-21, N-23, N-26;
# S32-num/{rounders,int,narrow,rand,negative-zero}.t).
#
#   * `floor(NaN)` was 0 and `floor(Inf)` the int64 maximum. The METHOD forms
#     already passed a non-finite value through; the sub forms went straight to
#     the integer conversion and saturated.
#   * `(-126).msb` was 6. A negative number is measured in two's complement,
#     where the top bit is the sign, so the answer is the length of |n| - 1 —
#     the SUB form had this rule and the method did not.
#   * `my UInt $x = -42` stored the negative number: UInt was not among the
#     types an assignment checks, and the check it would have used did not know
#     the subset rule either.
#   * `Int.new(Int)` was 0. There is no value in a type object to convert.
#   * `((.1e0 + .2e0) * 10).narrow` stayed a Num. Rakudo's test is approximate,
#     and Roast asserts the Int.
#   * `rand(3)` was X::Undeclared::Symbols, which names the wrong problem:
#     `rand` is a term and the call form is the Perl 5 one.
#   * `"−0".Num` was +0e0 with a Unicode MINUS SIGN where `"-0".Num` was -0e0.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# --- N-13: a non-finite value has no integer form and is passed through ------
check floor(NaN),     NaN,   'floor(NaN) is NaN';
check ceiling(NaN),   NaN,   'and ceiling(NaN)';
check floor(Inf),     Inf,   'floor(Inf) is Inf';
check ceiling(Inf),   Inf,   'and ceiling(Inf)';
check floor(-Inf),   -Inf,   'floor(-Inf) is -Inf';
check ceiling(-Inf), -Inf,   'and ceiling(-Inf)';
check truncate(Inf),  Inf,   'truncate too';
check floor(2.7),       2,   'an ordinary value still rounds';
check ceiling(2.1),     3,   'in both directions';

# --- N-21: msb measures a negative in two's complement ----------------------
check (-1).msb,    0, '-1 needs one bit';
check (-2).msb,    1, '-2 needs two';
check (-126).msb,  7, '-126 needs eight';
check (-127).msb,  7, 'and so does -127';
check (-129).msb,  8, '-129 needs nine';
check (-255).msb,  8, 'and so does -255';
check (-256).msb,  8, 'and -256';
check (-8).lsb,    3, 'lsb still measures the magnitude';
check (255).msb,   7, 'a positive number is unchanged';
check (-(2**70)).msb,     70, 'a bigint follows the same rule';
check ((2**70)).msb,      70, 'positive';
check (-(2**70)-1).msb,   71, 'and one past it';

# --- N-23: UInt is a subset of Int, and an assignment checks it -------------
check (try { my UInt $x = -42; 'stored' }) // $!.^name,
      'X::TypeCheck::Assignment', 'a UInt refuses a negative';
check (try { my UInt $y = "foo"; 'stored' }) // $!.^name,
      'X::TypeCheck::Assignment', 'and a non-number';
my UInt $u;
check ($u = 42), 42, 'but it takes a positive Int';
check ($u = 0),   0, 'and zero';
check ($u = Nil).raku, 'UInt', 'and Nil resets it to the type object';
check (1 ~~ UInt),  True,  'a non-negative Int smartmatches UInt';
check (-1 ~~ UInt), False, 'a negative one does not';

# --- N-06: Int.new refuses what it cannot convert ---------------------------
check (try Int.new(Int)) // $!.^name, 'X::AdHoc',        'a type object has no value';
check (try Int.new(Str)) // $!.^name, 'X::AdHoc',        'whichever type it is';
check (try Int.new("abc")) // $!.^name, 'X::Str::Numeric', 'an unparsable string throws';
check Int.new(Inf).exception.^name, 'X::Numeric::CannotConvert', 'and Inf fails';
check Int.new("42"),  42, 'a numeric string still parses';
check Int.new(4.7),    4, 'a Rat truncates';
check Int.new(42e0),  42, 'a Num too';
check Int.new(^42),   42, 'a Range counts';
check Int.new,         0, 'and no argument at all is zero';

# --- N-19: narrow is approximate, but does not destroy a value --------------
check ((.1e0 + .2e0) * 10).narrow, 3, 'rounding noise narrows away';
check exp(i * pi).narrow,         -1, 'and so does an imaginary part that is noise';
check 1e20.narrow,   100000000000000000000, 'a double past int64 narrows exactly';
check (2**70).Num.narrow, 1180591620717411303424, 'however large';
check 3.0.narrow,     3, 'a Rat with denominator 1 narrows';
check <3+4i>.narrow, <3+4i>, 'a real imaginary part is kept';
# the half of Rakudo's rule we decline: a tiny number is NOT zero
check 1e-300.narrow, 1e-300, 'a value under the tolerance keeps itself';

# --- N-26: `rand` is a term; the call forms are the Perl 5 ones -------------
check rand.^name, 'Num', 'the term still works';
check (try rand()) // $!.^name,  'X::Obsolete', 'rand() is refused';
check (try rand(3)) // $!.^name, 'X::Obsolete', 'and rand(3)';
check 5.rand.^name, 'Num', 'the method form is how you scale it';

# --- N-22: polymod stops at a divisor of one, and refuses two things --------
check 120.polymod(1, 10, 100, 1000, 10000), (120,), 'a divisor of one stops the sequence';
check 100.polymod(1 xx *),  (100,), 'a lazy list of ones too';
check 10.polymod(0.5),      (10,),  'and anything below one';
check 10.polymod(-3),       (10,),  'including a negative';
check (-1).polymod(2).exception.^name, 'X::OutOfRange', 'a negative invocant fails';
check (try 10.polymod(0)) // $!.^name, 'X::Numeric::DivideByZero', 'a zero divisor throws';
check 120.polymod(10, 10),  (0, 2, 1), 'an ordinary polymod is unchanged';
check 1000.polymod(10, 10, 10), (0, 0, 0, 1), 'with its trailing remainder';

# --- N-18: U+2212 MINUS SIGN is a minus ------------------------------------
check "−0".Num,    -0e0, 'a Unicode minus keeps the negative zero';
check "−0e0".Num,  -0e0, 'in exponent form too';
check "-0".Num,    -0e0, 'as the ASCII one always did';
check "−5".Num,    -5e0, 'and it signs an ordinary number';
check "−1.5".Num, -1.5e0, 'fractional too';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
