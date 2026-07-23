# Regression: batch 11 parse/dispatch cluster (HTTP::UserAgent, Text::Utils,
# JSON::Class, Digest triage). Contract: exit 0 + last line PASS.
my @fail;

# 1. if/elsif binding with a trait
my $x = 5;
if $x -> $y is copy { $y++; @fail.push('if-copy') unless $y == 6 }
if 0 -> $y { } elsif "b" -> $z is copy { @fail.push('elsif-copy') unless $z eq "b" }

# 2. quote-ish chars as word-list members inside a token body
grammar G1 {
    token TOP { <tchar>+ }
    token tchar { || < ! # $ % & ' * + - . ^ _ ` | ~ > || <[a..z]> }
}
@fail.push('wordlist-token') unless G1.parse("a'b#c");

# 3. comma-delimited match regex
@fail.push('comma-m') unless "ABC" ~~ m:i,^abc$,;

# 4. indented pod inside a sub body
sub f4($n) {
    my $r = $n + 1;
    =begin comment
    this { is } not [ code
    =end comment
    $r
}
@fail.push('indented-pod') unless f4(4) == 5;

# 5. enum with a trait argument before the value list
enum E5 is export(:tag) < A5 B5 C5 >;
@fail.push('enum-trait-arg') unless B5.value == 1;

# 6. negated container identity
my $a6 = 1; my $b6 = 2;
@fail.push('bang-bind-id') unless $a6 !=:= $b6;

# 7. compiler version is rakupp's own release version (not a Rakudo date)
@fail.push('compiler-version') unless $*RAKU.compiler.version >= v1;
@fail.push('lang-version') unless $*RAKU.version.Str.starts-with('6');

# 8. Blob does not bind Str params in dispatch
multi f8(Str $s) { 'str' }
multi f8(blob8 $b) { 'blob' }
@fail.push('blob-dispatch') unless f8("x".encode) eq 'blob' && f8("x") eq 'str';
sub f8b(--> blob32) { blob32.new }
@fail.push('blob32-rettype') unless (try { f8b(); True }) // False;

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
