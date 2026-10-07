# A sigilless name is BOUND to what its right side leaves: for a `\`
# declarator `=` means `:=`, so a container on the right becomes the name's
# container, and a raw (`\`) loop parameter binds the element's own Scalar.
#
# What broke: `my \x = …` copied the VALUE, and a parse-time guess marked the
# name read-only unless the initializer was an element or a plain `$x`. So
# `my \x = $ = 5; x = 6` died "Cannot modify an immutable Int (5)", `my \a =
# [1,2,3]; a = 4, 5, 6` sank the 5 and 6 and died, `my \x = $y` / `my \x := $y`
# / `my \v := %h<a>` wrote to a copy, and `x.VAR` was an Int. In a loop,
# `for @a -> \e { e *= 2 }` lost the write (the aliasing paths were gated on
# `$_` and `is rw` only — `-> $e is raw` lost it too), and `-> \row` over an
# Array of Arrays iterated each row's elements instead of the row as one item.
# A TYPED raw/rw pointy parameter (`-> Int \e`, `-> Int $e is rw`) lost the
# write too (real signature binding hands it a copy); `my \x = $!a` and `my $x
# := $!a` copied the attribute; and `.clone` shared an attribute bound to a
# variable with the original.
# Found by the sigil-free R&D (Acme::Sigilless translator).
#
# Runs under both engines: Rakudo passes every check natively.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
sub dies-with(&code, $want, $desc) {
    my $msg = 'lived';
    code();
    CATCH { default { $msg = .message } }
    @fail.push("$desc: got {$msg.raku}, want {$want.raku}") unless $msg eq $want;
}

# --- declarations bind the container -----------------------------------------
{ my \x = $ = 5; x = 6; x++; check x, 7, 'S1 `my \x = $ = 5` is a writable Scalar' }
{ my \a = [1,2,3]; a = 4, 5, 6; check a.raku, '[4, 5, 6]', 'S2 list assignment into a bound Array' }
{ my \h = {a => 1}; h = b => 2; check h.raku, '{:b(2)}', 'S3 list assignment into a bound Hash' }
{ my $x = 5; my \x := $x; x = 9; check $x, 9, 'S4 `my \x := $x` aliases' }
{ my $x = 5; my \x = $x; x = 9; check $x, 9, 'S5 `my \x = $x` aliases' }
{ my %h = a => 1; my \v := %h<a>; v = 42; check %h<a>, 42, 'S13 `my \v := %h<a>` binds the slot' }
{ my \x = $ = 5; check x.VAR.^name, 'Scalar', 'S16 .VAR of a bound anonymous Scalar' }
{ my $y = 1; my \x = $y; $y = 7; check x, 7, 'the alias sees writes to the source' }
{ my @b = 1, 2; my \a = @b; a = 7, 8; check @b, [7, 8], 'an aliased @b takes the list assignment' }
{ my \x = my $q = 3; x = 4; check $q, 4, '`my \x = my $q = 3` aliases $q' }
{ my \x = $ = 5; x = 6, 7; check x.raku, '$(6, 7)', 'a bound Scalar takes the List as one item' }
{ my \x = $ = 5; my \y = x; y = 6; check x, 6, 'a sigilless name bound to another shares its container' }
{ my \a = [1,2]; my \b = a; b = 3, 4; check a, [3, 4], '…and its Array' }
{ my $o = ''; for ^3 { my \x = $ = 5; x++; $o ~= x }; check $o, '666', 'a fresh `$ = 5` each time round' }
{ sub f(\a) { a = 3, 4 }; my @b = 1, 2; f(@b); check @b, [3, 4], 'a sigilless parameter list-assigns into the caller\'s Array' }

# --- a bare value stays immutable ---------------------------------------------
dies-with { my \x = 5; x = 6 }, 'Cannot modify an immutable Int (5)', 'a literal';
dies-with { my \x = 5; x += 1 }, 'Cannot modify an immutable Int (5)', 'a literal, op=';
dies-with { my \x := 1 + 2; x = 4 }, 'Cannot modify an immutable Int (3)', 'an operator\'s result, bound';
dies-with { my \x = 5; my \y = x; y = 6 }, 'Cannot modify an immutable Int (5)', 'a name bound to one';
dies-with { my \x = Int; x = 3 }, "Cannot modify an immutable 'Int' type object", 'a type object';

# --- raw loop parameters bind the element ------------------------------------
{ my $n = 0; for [[1,2],[3,4]] -> \row { $n++ for row }; check $n, 2, 'S6 `-> \row` binds the itemized row' }
{ my $n = 0; for ((1,2),(3,4)) -> \row { $n++ for row }; check $n, 4, '…but a List\'s rows are bare' }
{ my @a = 1, 2, 3; for @a -> \e { e *= 2 }; check @a, [2, 4, 6], 'S8 `-> \e` writes through' }
{ my @a = 1, 2, 3; for @a -> $e is raw { $e *= 2 }; check @a, [2, 4, 6], '`-> $e is raw` writes through' }
{ my %h = a => 1, b => 2; for %h.values -> \v { v *= 10 }; check %h, %(a => 10, b => 20), 'over %h.values' }
{ my $a = 1; for $a -> \v { v = 5 }; check $a, 5, 'over one scalar' }
{ my ($x, $y) = 1, 2; for $x, $y -> \v { v = 0 }; check ($x, $y), (0, 0), 'over a list of scalars' }
{ my @a = 1, 2, 3; for @a.grep(* > 1) -> \e { e = 0 }; check @a, [1, 0, 0], 'over a grep view' }
{ my $s = 0; for ^5 -> \i { $s += i }; check $s, 10, 'over a Range, read only' }
dies-with { for 1, 2, 3 -> \e { e = 5 } }, 'Cannot modify an immutable Int (1)', 'over bare values';
dies-with { for ^3 -> \i { i = 5 } }, 'Cannot modify an immutable Int (0)', 'over an Int Range';

# --- typed raw / rw pointy parameters write through too -----------------------
{ my @a = 1, 2, 3; for @a -> Int \e { e = 0 }; check @a, [0, 0, 0], '`-> Int \e`' }
{ my @a = 1, 2, 3; for @a -> Int $e is rw { $e = 0 }; check @a, [0, 0, 0], '`-> Int $e is rw`' }
{ my @a = 1, 2, 3; for @a -> Int $e is raw { $e = 0 }; check @a, [0, 0, 0], '`-> Int $e is raw`' }
{ my @a = 1, 2, 3; for @a -> Int $e is copy { $e = 0 }; check @a, [1, 2, 3], '…but not `is copy`' }
{ my @a = 1, 2, 3; for @a -> Int $e is rw { last if $e == 2; $e = 0 }; check @a, [0, 2, 3], '…and `last` keeps the writes so far' }

# --- an attribute's container ---------------------------------------------------
class Attr {
    has Int $.a is rw = 1;
    method bind-sigilless { my \x = $!a; x = 5; $!a }
    method bind-scalar { my $x := $!a; $x = 6; $!a }
    method seen { my $x := $!a; $!a = 9; $x }
    method typed-sigilless { my \x = $!a; x = "s" }
    method typed-scalar { my $x := $!a; $x = "s" }
    method keep { my $x := $!a; self }
    method set($v) { $!a = $v }
    method bind-to(\v) { $!a := v }
}
check Attr.new.bind-sigilless, 5, '`my \x = $!a` aliases the attribute';
check Attr.new.bind-scalar, 6, '`my $x := $!a` aliases the attribute';
check Attr.new.seen, 9, '…and sees writes to it';
{ my $t = 'lived'; try { Attr.new.typed-sigilless; CATCH { default { $t = .^name } } }; check $t, 'X::TypeCheck::Assignment', 'the attribute\'s type, through `\x`' }
{ my $t = 'lived'; try { Attr.new.typed-scalar; CATCH { default { $t = .^name } } }; check $t, 'X::TypeCheck::Assignment', 'the attribute\'s type, through `$x`' }
{ my $o = Attr.new.keep; my $p = $o.clone; $p.set(7); check ($o.a, $p.a), (1, 7), 'a clone gets its own container' }
{ my $s = 1; my $o = Attr.new; $o.bind-to($s); my $p = $o.clone; $s = 5; check ($o.a, $p.a), (5, 1), '…also when the attribute is bound to a variable' }

# --- already agreeing: keep them green ----------------------------------------
{ sub f(\v) { v = 5 }; my $w = 1; f($w); check $w, 5, 'a sigilless parameter writes the caller\'s variable' }
{ my @a = 1, 2; my \b = @a; b.push(3); check @a, [1, 2, 3], 'a method call through the alias' }
{ my \x = $ = [1,2]; my $n = 0; $n++ for x; check $n, 1, 'a Scalar holding an Array is one item' }
{ my \x = $ = 1; my &c = { x += 10 }; c(); c(); check x, 21, 'a closure writes the bound Scalar' }
{ sub g(\v) { v.VAR.^name }; my $q; check g($q) ~ ' ' ~ g(5), 'Scalar Int', '.VAR of a sigilless parameter' }
{ my ($a, \b) = 1, 2; check b, 2, 'a sigilless item in a declarator list' }

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
