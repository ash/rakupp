# Two scanner bugs that only became visible once an unterminated literal
# started being an ERROR (see unterminated-quotes-at-eof.raku). Both made a
# well-formed construct pick the WRONG closer, so the scan ran to end of file:
# before the diagnostic landed that silently truncated the program, after it
# the file died with a bogus "couldn't find terminator".
#
# 1. `)>` was always taken for the closing half of the `<( … )>` match-capture
#    marker. In `/^<at(0)>/` it is an argument list ending inside an assertion,
#    so eating it left the `<` unbalanced and the closing `/` was never found —
#    S05-mass/stdrules.t lost everything after line 317. `)>` now only closes a
#    `<(` that actually opened.
#
# 2. A Unicode Ps delimiter's closer was computed as codepoint+1. That is wrong
#    for the tick brackets, which cross over (⦍ U+298D closes with ⦐ U+2990 and
#    ⦏ U+298F with ⦎ U+298E), and for the fullwidth pairs (［ U+FF3B closes with
#    ］ U+FF3D). The closer is the Bidi mirrored glyph; U+301D 〝 has no mirror
#    and keeps the +1 rule.
# Contract: exit 0 + last line PASS.
use MONKEY-SEE-NO-EVAL;   # the delimiter cases are built as source and EVAL'd
my @fail;

# --- 1. `)>` inside an assertion is not a capture marker ---
sub want($got, $want, $why) { @fail.push("$why: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

# the shape that broke it: an argument list closing right before the `>`
grammar G { token TOP { ^ <chunk(3)> $ }; token chunk($n) { . ** {$n} } }
want (G.parse('abc') ?? 'y' !! 'n'), 'y', 'assertion with an argument list `<chunk(3)>`';
want (G.parse('ab') ?? 'y' !! 'n'), 'n', 'and it really applies the argument';
# statements AFTER such a regex must still be seen — the truncation symptom
my $after = 'reached';
want $after, 'reached', 'code after `(…)>` in a regex still parses';
# and the real `<( … )>` capture marker still works
want ('abcde' ~~ /b <( c )> d/).Str, 'c', '<( … )> capture marker';
want ('abcde' ~~ /a <(bcd)> e/).Str, 'bcd', '<( … )> capture marker, wider';

# --- 2. Ps delimiters close with their MIRRORED glyph ---
# (built by EVAL so this file itself stays free of the odd brackets)
for ('⦍' => '⦐', '⦏' => '⦎', '［' => '］', '｛' => '｝', '「' => '」',
     '〝' => '〞', '⦑' => '⦒', '（' => '）') -> $p {
    my $src = 'q' ~ $p.key ~ 'abc' ~ $p.value;
    my $got = try EVAL $src;
    want $got, 'abc', "q{$p.key}abc{$p.value} (U+{$p.key.ord.fmt('%04X')}/U+{$p.value.ord.fmt('%04X')})";
}
# a genuinely unclosed one is still an error, not a silent 'abc…'
@fail.push('unclosed ⦍ should not parse') if (try EVAL "q\c[0x298D]abc").defined;

if @fail {
    die "regex capture marker / mirrored quote delimiters broken:\n" ~ @fail.map({ "  - $_" }).join("\n");
}
say 'PASS';
