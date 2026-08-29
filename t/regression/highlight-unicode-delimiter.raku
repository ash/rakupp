# BROKE: `--highlight` sized a quote's closing delimiter in BYTES. `closeDelim`
# mapped '\xAB' to '\xBB' — the second byte of « and », not the codepoints — so
# scanning `q:to«END»` stopped on the first byte of the closer and left the
# second one outside the span: `<span class="s">q:to«EN…\xC2</span>\xBB`, a
# split UTF-8 sequence in both the HTML and the ANSI output. Unreachable until
# issue #31 made the lexer accept those delimiters; the moment `q:to«END»`
# became a real quote form, the highlighter rendered it as mojibake.
# FIXED: delimPair() reads the delimiter as a UTF-8 sequence and closes it the
# way the lexer does — the Bidi mirror when the codepoint has one, else the next
# codepoint — and the body scan compares sequences. Recognising an adjacent
# non-ASCII delimiter as a quote form at all came with it, so `q«a b»` is now a
# string span instead of plain text.
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

# the whole closing delimiter belongs to the span
spans 'q:to«END»', '<span class="s">q:to«END»</span>', 'q:to«END»';
spans 'q:to｢END｣', '<span class="s">q:to｢END｣</span>', 'q:to｢END｣';
spans 'q:to「END」', '<span class="s">q:to「END」</span>', 'q:to「END」';
spans 'q:to♥END♥', '<span class="s">q:to♥END♥</span>', 'q:to♥END♥ (a codepoint pairs with itself)';

# an adjacent Unicode delimiter makes a quote form at all
spans 'q«a b»',  '<span class="s">q«a b»</span>',  'q«a b»';
spans 'Q♥raw♥',  '<span class="s">Q♥raw♥</span>',  'Q♥raw♥';
spans 'qw«a b»', '<span class="s">qw«a b»</span>', 'qw«a b»';

# …and the ASCII forms are where they were
spans 'q:to/END/', '<span class="s">q:to/END/</span>', 'q:to/END/';
spans 'q{a{b}c}',  '<span class="s">q{a{b}c}</span>',  'q{} still nests';
spans 'q(x)',      '<span class="s">q(x)</span>',      'q(x)';
spans '$x ~~ s/a/b/', '<span class="sr">s/a/b/</span>', 's/// keeps both halves';
spans '$x ~~ s[a][b]', '<span class="sr">s[a][b]</span>', 's[][] keeps both halves';

# nothing is lost or invented: the spans concatenate back to the source
sub round-trips(Str $code, Str $what) {
    my $text = hl($code).subst(/'<' <-[>]>* '>'/, '', :g).subst('&#39;', "'", :g)
                        .subst('&quot;', '"', :g).subst('&amp;', '&', :g)
                        .subst('&lt;', '<', :g).subst('&gt;', '>', :g).trim;
    @fail.push("$what: highlighted text {$text.raku} is not the source {$code.raku}")
        unless $text eq $code;
}
round-trips 'my $s = q:to«END»;', 'a guillemet heredoc introducer';
round-trips 'my $s = q♥abc♥;',    'a heart-delimited quote';
round-trips 'my $s = q:to/END/;', 'the ASCII form';

if @fail {
    note "FAILED: @fail.join('; ')";
    say 'FAIL';
}
else {
    say 'PASS';
}
