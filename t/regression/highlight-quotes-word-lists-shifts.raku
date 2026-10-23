# BROKE: `--highlight` read none of the Unicode quotes as a string: `“a $x”`,
# `‘a’`, `｢a｣`, `„a“` were punctuation, names and variables, so FatRoast left
# their letters alone and names-v1 renamed variables in them as code (found
# 2026-10-23: “'$a' cmp '$b' is More” in S03-operators/comparison.t). A `"…"`
# ended at a quote nested in its `{…}` block (`"a {f("b")} c"`), and what
# followed was read with code and strings swapped.
#
# And it called every name a value, so the `<` after one was an
# operator: in `is <a b>, 1`, `for <a b> {…}` and `$a cmp <a b>` the word list
# was a `<`, two names and a `>`. About 600 Roast lines read so; FatRoast left
# their letters alone, and ops-v1 put `∞` into `is <NaN+Inf\i>, …` (found
# 2026-10-23, S02-literals/numeric.t). And `$v +<= 1` was `+`, then a word
# list from the `<` to the next `>` on the line, so ops-v1 made it `+≤`. A
# `token infix:sym<+> { … }` ended its name at the `<`, so its body was code,
# and ops-v1 made `$<x>=[.+]` in one `$<x≥[.+]` (S05-grammar/protoregex.t).
# A reduction `[<=] @b` was an array holding a word list from the `<` to the
# next `>` (integration/advent2009-day20.t: `@b` left un-renamed), and a word
# hyper `>>dot<<` began a `<<…>>` list (S06-operator-overloading/workout.t).
# FIXED: scanUnicodeQuote pairs the quotes as Rakudo does (`“…”`, `„…”`,
# `„…“`, `”…”` interpolate; `‘…’`, `‚…’`, `‚…‘`, `’…’` do not; `｢…｣` has no
# escapes), and scanQuoted skips an interpolating string's `{…}` block whole
# (src/Highlight.cpp). The scanner remembers where a name that can be a listop ended (not a
# method, a declared name or a term such as `pi`); a `<` spaced from it and
# tight against a word starts a word list (src/Highlight.cpp, afterListop). A
# shift after a term is one operator, its `=` form too (`+<=`, `~>`). A
# token's name takes the `<…>` or `«…»` after a `:`, and its body is a pattern.
# A reduction holding `<` or `>` is one operator where a term is wanted
# (reduceOpEnd), and hyperOpEnd takes a word between the brackets.
#
# Contract: exit 0 + last line PASS.

my @fail;
sub hl(Str $code) {
    my $p = run($*EXECUTABLE, '--highlight', '-e', $code, :out, :err);
    my $o = $p.out.slurp(:close);
    $p.err.slurp(:close);
    $o
}
sub spans(Str $code, Str $want, Str $what) {
    my $got = hl($code);
    @fail.push("$what: {$want.raku} missing from {$got.trim.raku}")
        unless $got.contains($want);
}

# the Unicode quotes are strings, and a string in a block does not end one
spans 'say “a $x {1}”', '<span class="s">“a $x {1}”</span>', '“…”';
spans 'say „a“, „b”', '<span class="s">„a“</span>, <span class="s">„b”</span>', '„…“ and „…”';
spans 'say ”a”', '<span class="s">”a”</span>', '”…”';
spans 'say ‘a’, ‚b’, ’c’', '<span class="s">‘a’</span>, <span class="s">‚b’</span>, <span class="s">’c’</span>', 'single';
spans 'say ｢a\｣', '<span class="s">｢a\｣</span>', '｢…｣ has no escapes';
spans 'say "a {f("b")} c", $d', '<span class="s">&quot;a {f(&quot;b&quot;)} c&quot;</span>, <span class="nv">$d</span>', 'a string in a block';
spans 'say “x {"}"} y”, $d', '<span class="s">“x {&quot;}&quot;} y”</span>, <span class="nv">$d</span>', 'a brace in a string in a block';
spans q[say 'a {', $d], q[<span class="s">'a {'</span>, <span class="nv">$d</span>], 'no block in single quotes';

# after a listop, a keyword or an infix word, `<a b>` is a word list
spans 'is <a b>, 1', '<span class="k">is</span> <span class="s">&lt;a b&gt;</span>', 'is <a b>';
spans 'is <NaN+Inf\i>, 1', '<span class="s">&lt;NaN+Inf\i&gt;</span>', 'an allomorph';
spans 'for <a b> -> $x { }', '<span class="s">&lt;a b&gt;</span> -&gt;', 'for <a b>';
spans 'say $a cmp <a b>', '<span class="n">cmp</span> <span class="s">&lt;a b&gt;</span>', 'cmp <a b>';
spans 'is-deeply set <a b>, 1', '<span class="n">set</span> <span class="s">&lt;a b&gt;</span>', 'set <a b>';

# and after a method, a term or with a space after it, `<` compares
spans 'ok @a.elems <3 && 1 > 0', '<span class="nb">elems</span> &lt;<span class="mi">3</span>', 'a method';
spans 'ok pi <4 && 1 > 0', '<span class="n">pi</span> &lt;<span class="mi">4</span>', 'a term';
spans 'ok x < 3 && 1 > 0', '<span class="n">x</span> &lt; <span class="mi">3</span>', 'spaced';
spans 'ok f <= 3 && 1 > 0', '<span class="n">f</span> &lt;= <span class="mi">3</span>', '<=';

# a shift is one operator
spans '$v +<= 1; @a >>.foo', '+&lt;= <span class="mi">1</span>; <span class="nv">@a</span>', '+<=';
spans 'my $x = $y ~< 3; say "a>"', '~&lt; <span class="mi">3</span>; <span class="nb">say</span> <span class="s">&quot;a&gt;&quot;</span>', '~<';

# a token's name takes its `:sym<…>`, and the body is a pattern
spans 'token cap:sym<a> { $<x>=[.+] }', '{<span class="sr"> $&lt;x&gt;=[.+] </span>}', ':sym<a>';
spans 'rule t:sym«b c» { a <= b }', '{<span class="sr"> a &lt;= b </span>}', ':sym«b c»';

# a reduction is an operator, and a hyper's operator may be a word
spans 'ok ([<=] @b>>.k), "x"', '([&lt;=] <span class="nv">@b</span>&gt;&gt;.', '[<=]';
spans 'ok [<] 1, 2; say "a>"', '[&lt;] <span class="mi">1</span>', '[<] after a listop';
spans 'say [\<] 1; my @a = [<a b>]', '[<span class="s">&lt;a b&gt;</span>]', 'an array of a word list is one';
spans 'f(@v >>dot<< @v)', '&gt;&gt;dot&lt;&lt; <span class="nv">@v</span>)', '>>dot<<';
spans 'say @a <<cmp>> @b, %h<<a>>', '&lt;&lt;cmp&gt;&gt; <span class="nv">@b</span>, <span class="nv">%h</span><span class="s">&lt;&lt;a&gt;&gt;</span>', '<<cmp>> after a term';

if @fail {
    note "FAILED: @fail.join('; ')";
    say 'FAIL';
}
else {
    say 'PASS';
}
