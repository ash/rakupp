# Regression: `#`«…»` is an embedded comment, and `#`«««…»»»` is too.
#
# An embedded comment takes ANY bracket pair, not only the ASCII four. This
# engine handled `( [ { <` and their repeats but not the guillemets, so the
# commented-out text was lexed as CODE. Terminal::Gauge closes each of its subs
# with the signature repeated inside `#`«««…»»»`, and the `-->` in that
# signature was reported as "unexpected operator in term position" — a parse
# error 60 lines away from anything the author had written as code.
#
# The delimiters are two UTF-8 bytes each, which is why they could not ride the
# single-char path: a repeated opener has to count PAIRS of bytes, and nesting
# counts only full n-char sequences.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# The single form, inline: the comment is text, and the expression resumes
# after its closer — so the `+ 99` on the same line still counts.
my $a = 1 #`« this is a comment » + 99
;
check $a, 100, q{a single-guillemet comment ends at its closer, not at end of line};

# The tripled form spanning lines, holding text that would not parse as code.
sub gauge() { 42 } #`««« sub gauge(Str:D $prefix, Int:D $current = 1,
                                   Int:D $length = 2 --> Bool:D) is export »»»
check gauge(), 42, 'a tripled guillemet comment spans lines and hides a signature';

# A lone closer inside the tripled form is literal text, not the end.
my $b = 5 #`««« one » two » three »»»
;
check $b, 5, 'a single » inside a tripled comment does not close it';

# Nesting: a full opener inside opens a level the matching closer has to shut.
my $c = 7 #`« outer «inner» still outer »
;
check $c, 7, 'a nested guillemet pair is counted';

# The declarator spellings take the same brackets.
#|« leading declarator comment »
sub documented() { 'yes' }
check documented(), 'yes', 'a #|« » declarator comment is skipped too';

# And the ASCII forms this sits beside must keep working.
my $d = 3 #`( still a comment ) ;
check $d, 3, 'the paren form is unaffected';
my $e = 4 #`{{ a } brace inside }} ;
check $e, 4, 'the repeated-brace form is unaffected';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
