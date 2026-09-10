# Regression: a `for` loop's pointy parameter kept only its NAME, so a type, a
# coercion, a `:D`/`:U` smiley or a `where` on it silently stopped existing.
#
#   for @paths -> IO() $f { $f.e }     # $f was a Str: "no such method 'e'"
#   for ("x",) -> Int $n { }           # bound without a murmur; Rakudo refuses
#
# The parser has two paths for a pointy signature: real binding through
# bindParams, and a fast path that records the names. The fast one was taken
# unless a parameter had a sub-signature or `is copy` — so every other thing a
# parameter can carry was dropped on the floor. `is copy` had already been
# fixed here once, one trait at a time; the question is asked once now.
#
# Found by `fez upload` under rakupp: its tar bundler is
# `for @fs -> IO() $f { die unless $f.e || $f.l }`, and every path arrived a
# Str. Subs, methods, bare blocks and `.map` blocks all coerced correctly — the
# `for` statement was the only one that did not, which is why it survived.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

# Coercion, in every shape that takes a signature — `for` is the one that broke.
check (sub (IO() $p) { $p.^name })('/etc'),               'IO::Path', 'sub';
check (-> IO() $f { $f.^name })('/etc'),                  'IO::Path', 'bare block';
check ('/etc',).map(-> IO() $f { $f.^name }).head,        'IO::Path', 'map block';
check (class { method m(IO() $p) { $p.^name } }).m('/etc'), 'IO::Path', 'method';
check (do { my $n; for ('/etc',) -> IO() $f { $n = $f.^name }; $n }),
      'IO::Path', 'for loop';
check (do { my $n; for ('42',) -> Int() $i { $n = $i.^name }; $n }),
      'Int', 'for loop, Int()';

# Two parameters per iteration still bind one row at a time.
check (do { my @s; for ('/etc', '/tmp') -> IO() $a, IO() $b { @s = $a.^name, $b.^name }; @s.join(',') }),
      'IO::Path,IO::Path', 'for loop, two coercing params';

# A plain type constraint is CHECKED, not ignored.
my $died = False;
try { EVAL 'for ("x",) -> Int $n { }' };
$died = True if $!;
check $died, True, 'a type constraint on a for parameter is enforced';

# …and the fast path still works, because most loops have no type at all.
check (do { my $s = ''; for <a b c> -> $x { $s ~= $x }; $s }), 'abc', 'an untyped for loop is unaffected';
check (do { my $s = ''; for (1,2), (3,4) -> ($a, $b) { $s ~= "$a$b" }; $s }), '1234', 'destructuring still works';

if @fail { die "FAIL:\n" ~ @fail.join("\n") }
say 'PASS';
