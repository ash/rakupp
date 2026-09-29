# Regression: a lookbehind whose inner is one CHARACTER of a named rule —
# `<!after <.alpha>>`, `<?after <.alnum>>`, a one-class grammar token such as
# YAMLish's `token space { <[\ \t]> }` — tried every start position back to the
# beginning of the input whenever it did not match. The scan window is the
# inner's width, a character is 1..n bytes, and an open byte width meant "from
# position 0" (for a grammar token since 364c26b6, which was right that the
# byte width is open; for a built-in class outside a grammar, always). YAMLish
# asks `<!after <.alnum>>` at every comment and plain scalar, so its parse time
# grew with the square of the input: 11 s for the Raku course's 2,653-line
# table of contents where Rakudo takes 0.5 s, and `m:g/<!after <.alpha>> a/`
# over 40,000 words took 70 s (Rakudo 0.15 s). The window is now bounded in
# characters too (Regex::RxWidth::chars): each character back is at most a run
# of non-ASCII bytes and one ASCII base, with CR LF as one.
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
use lib $?FILE.IO.parent.add('../fixtures/yamlish/lib');
use YAMLish;

my @fail;

# The time bounds are blow-up detectors, not benchmarks. Measured on the
# workstation that found this: linear, the cases take 0.01, 0.03 and 1.2 s;
# quadratic they took 18, ~32 and ~30 s. A shared CI runner is several times
# slower than that machine, which only widens the quadratic side's lead.

# 1. a plain regex, built-in class
my $words = 'ab ' x 20_000;
my $t0 = now;
my $hits = +($words ~~ m:g/ <!after <.alpha>> a /);
my $secs = now - $t0;
@fail.push("plain <!after <.alpha>>: $hits hits") unless $hits == 20_000;
@fail.push("plain <!after <.alpha>> too slow: {$secs.round(0.1)} s") if $secs > 5;

# 2. a grammar, a one-class token of its own and a built-in class
grammar Words {
    token sp  { <[\ \t]> }
    token TOP { [ <!after <.sp>> <.alnum> | <?after <.sp>> <.alpha> | <.sp> ]+ }
}
$t0 = now;
my $ok = so Words.parse('xy ' x 40_000);
$secs = now - $t0;
@fail.push('grammar lookbehinds did not parse') unless $ok;
@fail.push("grammar lookbehinds too slow: {$secs.round(0.1)} s") if $secs > 5;

# 3. YAMLish: a long flat list with comments
my $yaml = (^3000).map({ "- title: t$_  # note $_\n  url: u$_\n" }).join;
$t0 = now;
my @docs = load-yamls($yaml);
$secs = now - $t0;
@fail.push("YAMLish list: {@docs.elems} documents") unless @docs.elems == 1;
my $items = @docs[0] // [];
@fail.push("YAMLish list: {$items.elems} items") unless $items.elems == 3000;
@fail.push("YAMLish list: last item {($items.tail // {})<url> // 'Nil'}")
    unless (($items.tail // {})<url> // '') eq 'u2999';
@fail.push("YAMLish list too slow: {$secs.round(0.1)} s") if $secs > 15;

# 4. the bounded window still reaches the start of the character before —
# over combining marks, emoji sequences, a keycap and CR LF (oracle: Rakudo)
my @cases =
    ["e\x[301]#",                  / <?after <.alpha>> '#' /,           True ],
    ["é #",                        / <!after <.alpha>> '#' /,           True ],
    ["日本#",                       / <?after <.alpha> <.alpha>> '#' /,  True ],
    ["日本#",                       / <?after <.alpha> ** 2> '#' /,      True ],
    ["👨\x[200D]👩\x[200D]👧#",       / <?after <.alpha>> '#' /,           False],
    ["👨\x[200D]👩\x[200D]👧#",       / <!after <.alpha>> '#' /,           True ],
    ["x\x[FE0F]\x[20E3]#",         / <?after <.alpha>> '#' /,           True ],
    ["a\r\nb",                     / <?after <.cntrl>> 'b' /,           True ],
    ["ass#",                       / <?after <.alpha> :i 'ß'> '#' /,    True ], # ß folds to two
    ["ab",                         / <?after <.print> 'b'> /,           True ];
for @cases -> [$s, $rx, $want] {
    @fail.push("{$s.raku} ~~ {$rx.raku}: {!$want}") unless so($s ~~ $rx) === $want;
}
grammar Crlf { token nl { \n }; token TOP { .*? <?after <.nl>> 'b' .* } }
@fail.push('grammar <?after <.nl>> over CR LF') unless so Crlf.parse("a\r\nb");
grammar Hash { token sp { <[\ \t]> }; token TOP { .*? <?after <.sp>> '#' .* } }
@fail.push('grammar <?after <.sp>> "a #"') unless so Hash.parse("a #");
@fail.push('grammar <?after <.sp>> "a#"') if so Hash.parse("a#");

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
