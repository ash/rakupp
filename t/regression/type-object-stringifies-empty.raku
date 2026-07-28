# Regression: a type object STRINGIFIES EMPTY.
#
# `(Int)` is its GIST — a different question. `~Int`, `"{Int}"`, `put Int`,
# `.join` and a quanthash key all want the empty string, and four sites gave
# three different answers in one five-line program.
#
# This was tried once and BACKED OUT, with the reason recorded in Value.cpp:
# quanthash keys were built straight from toStr, so every type object then keyed
# on the same empty string while `.raku` still rendered the typed key's gist —
# `Set (|) Set` became `Set.new("(Set)")` against Rakudo's `Set.new("")`, costing
# 48 assertions across the four set-operator files. baggyKeyStr has since grown
# its own `v.t == VT::Type` arm that keys on the gist, which is exactly the
# prerequisite the old note asked for, so the change lands clean now: the four
# set-operator files are unchanged and four other files gained.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# stringification is empty…
check(Int.Str,      '', '.Str');
check(~Int,         '', 'the ~ prefix');
check("{Int}",      '', 'string interpolation');
check(Str.Str,      '', 'for any type');
check([Int, Str].join('|'), '|', 'and through .join');

# …while the gist keeps the parenthesised form
check(Int.gist,     '(Int)', '.gist is unchanged');
check(Int.raku,     'Int',   'and .raku is the bare name');
check(Int.^name,    'Int',   'as is .^name');

# a type object as a quanthash element still renders as itself
check(set(Int, Str).elems, '2',              'type objects are distinct Set elements');
check(set(Int, Str).gist, 'Set((Int) (Str))', 'and render with their gist');
check(set(Int).keys.raku, '(Int,).Seq',       'the key keeps its type object');

# IterationEnd is a SENTINEL, not a type object — it keeps its name
check(IterationEnd.Str, 'IterationEnd', 'IterationEnd stringifies to its name');

# a hash keyed by a type object uses the empty string, as Rakudo does
my %h; %h{Int} = 1;
check(%h.keys.raku, '("",).Seq', 'a type-object hash key is ""');

# an instance is unaffected
check(~42,          '42',  'an Int instance');
check(~"x",         'x',   'a Str instance');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
