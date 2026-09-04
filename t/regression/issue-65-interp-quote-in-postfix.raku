# Issue #65: a quote inside an interpolated postfix chain ended the string.
#
#     say "\"@strings.join('"|"')\"";
#
# is legal Raku: `@a` interpolates when a postfix follows it, and the argument
# list of `.join(…)` is CODE, so the `"` inside `'"|"'` belongs to that code and
# does not close the enclosing string. rakupp's lexer scanned the literal for the
# next unescaped `"` and found that one, so the line lexed as
# `"…" | "…"` — two strings around infix `|` — and printed a Junction:
# `any("@strings.join(', ')")`.
#
# lexQuoted() (and the qq-family scanner) now copy a variable's whole postfix
# chain as a unit, with nested '…'/"…" opaque, exactly as the `{ … }` block
# branch already did. An unterminated group falls back to plain literal scanning
# rather than running away to end of input.
#
# Contract: exit 0 + last line PASS. Every expectation here is Rakudo's.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# the reproducer from the issue
my @strings = ('a', 'b', 'c', 'd', 'e', 'f');
check("\"@strings.join('"|"')\"", '"a"|"b"|"c"|"d"|"e"|"f"', 'issue #65 line');

# the delimiter itself inside the argument list
my @a = <x y>;
check("@a.join("-")", 'x-y', 'double quote in .join arg');
check("@a.join(", ")", 'x, y', 'delimiter plus comma in the arg');

my $x = 'q';
check("$x.subst("q", "Z")", 'Z', 'two quoted args');
check("$x.subst("q", "(")", '(', 'a bracket inside a quoted arg');

# subscripts are code too
my %h = k => 'v', 'a b' => 'z';
check("%h{"k"}", 'v', 'double-quoted hash subscript');
check("%h{'a b'}", 'z', 'single-quoted hash subscript');

# a chain: every link keeps its own quoting
check("@a.map({ .uc }).join(", ")", 'X, Y', 'block then quoted join');
check("$x.subst("q","Z").flip", 'Z.flip', 'bare method after a call stays literal');

# the qq family has the same rule, and its delimiter may sit in the arguments
check(qq{@a.join("}")}, 'x}y', 'closing brace inside a qq{ } argument');
check(qq[@a.join("]")], 'x]y', 'closing bracket inside a qq[ ] argument');
check(qq/@a.join("-")/, 'x-y', 'quoted arg under a slash delimiter');

# …and none of this may swallow ordinary text
check("@a[0] (paren)", 'x (paren)', 'a parenthesis after a subscript is text');
check("It's $x (ok)", "It's q (ok)", 'an apostrophe and parens in prose');
check("$x.uc", 'q.uc', 'a bare method call does not interpolate');
check("@a", '@a', 'a bare array does not interpolate');
check("@a.elems()", '2', 'an empty argument list still interpolates');

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
