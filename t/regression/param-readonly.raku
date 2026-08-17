# Regression: a plain scalar parameter is READONLY, and the traits that change
# that are `is copy`, `is rw` and `is raw` — nothing else.
#
# BROKE: every parameter was bound as if `is copy` were always on, so
#   sub replace($s) { $s ~~ s:g/01/10/ while $s ~~ /01/; $s }
# ran here and died everywhere else with "Cannot assign to a readonly variable".
# The Value already carried a `readonly` flag and the binder already set it —
# only `$_` and `$/` ever tested it.
#
# The documentation is explicit (Language/signatures, "Parameter traits and
# modifiers"): parameters are bound to their argument and marked as read-only,
# and `is copy` is what allows modification inside the routine.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub dies(&c, $what) { my $ok = False; try { c(); CATCH { default { $ok = True } } }; @fail.push($what) unless $ok }
sub lives(&c, $want, $what) { my $got = try { c() }; @fail.push("$what: got {$got // $!.^name} want $want") unless (try { $got eq $want }) }

# the default: every mutating shape is refused
dies({ sub f($x) { $x = 5 };            f(1)   }, 'plain assign');
dies({ sub f($x) { $x ~= "b" };         f("a") }, '~= on a plain param');
dies({ sub f($x) { $x .= uc };          f("a") }, '.= on a plain param');
dies({ sub f($s) { $s ~~ s/a/b/ };      f("a") }, 's/// on a plain param');
dies({ sub f($s) { $s ~~ tr/a/b/ };     f("a") }, 'tr/// on a plain param');
dies({ sub f($x is readonly) { $x = 5 };f(1)   }, 'explicit is readonly');

# the traits that DO permit it
lives({ sub f($x is copy) { $x = 5; $x }; f(1) }, 5, 'is copy');
lives({ sub f($x is rw) { $x = 5 }; my $v = 1; f($v); $v }, 5, 'is rw writes back');
lives({ sub f($x is raw) { $x = 5 }; my $v = 1; f($v); $v }, 5, 'is raw writes back');
lives({ sub f(\x) { x = 5 }; my $v = 1; f($v); $v }, 5, 'backslash sigil writes back');

# the flag marks the CONTAINER, not the value: a copy of a readonly param is
# an ordinary writable slot (this is what regressed first time round)
lives({ sub f($x) { my $y = $x; $y = 9; $y }; f(1) }, 9, 'copy into a fresh scalar');
lives({ sub f($x) { my @a = $x, 2; @a[0] = 7; @a[0] }; f(1) }, 7, 'copy into an array element');

# "applies only to the container" — the object inside keeps its mutators
lives({ sub f(@a) { @a.push(9); @a.elems }; f([1, 2]) }, 3, 'push through a plain @ param');
lives({ sub f(%h) { %h<k> = 9; %h<k> }; f({}) }, 9, 'store through a plain % param');
lives({ class C { has $.v is rw }; sub f($o) { $o.v = 7; $o.v }; f(C.new) }, 7, 'is-rw attribute through a plain param');

# an argument with no container behind it cannot be written through
# (`is rw` given a literal is NOT covered: Rakudo rejects it at bind time, but
# the requirement has to take part in multi dispatch — `multi f($x is rw)` plus
# `multi f($x)` must send a literal to the second — so it is still permissive.)
dies({ sub f($y is raw) { $y = 5 }; f(1) }, 'is raw given a literal');
dies({ sub f($y is raw) { $y = 5 }; f(1 + 1) }, 'is raw given an expression');
dies({ sub f(\x) { x = 5 }; f(1) },         'backslash sigil given a literal');
lives({ sub f($y is raw) { $y + 1 }; f(1) }, 2, 'a raw literal still READS fine');
lives({ sub f($y is raw) { $y = 5 }; my @a = (1, 2); f(@a[0]); @a[0] }, 5, 'raw through an element');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
