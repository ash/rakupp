# Regression: an undeclared $!attr in a class method is a COMPILE-time error
# printed with Rakudo's ===SORRY!=== banner and a file:line location (the
# exception carries filename/line attrs, X::Comp style). Previously the bare
# message printed with no banner or location. Reported 2026-07-22 (Cafe
# snippet: has $.na but say $!name).

my $src = q:to/END/;
class Cafe {
    has $.na;
    method list-orders {
        say $!name;
    }
}
END

my $tmp = 'temp_attr_' ~ $*PID ~ '.raku';
spurt $tmp, $src;
my $out = qqx{$*EXECUTABLE $tmp 2>&1};
my $rc = $! // 0;
unlink $tmp;

my $ok = True;

unless $out.contains('===SORRY!=== Error while compiling') {
    say 'FAIL: no SORRY banner';
    $ok = False;
}
unless $out.contains('Attribute $!name not declared in class Cafe') {
    say 'FAIL: message changed';
    $ok = False;
}
unless $out ~~ / 'at ' \S+ ':' \d+ / {
    say 'FAIL: no file:line location';
    $ok = False;
}

# in-language view unchanged: throws-like still sees the typed exception
use Test;
plan 1;
throws-like 'class C2 { method m { $!nope } }', X::Attribute::Undeclared,
    symbol => '$!nope', 'typed exception with symbol attr';

say 'PASS' if $ok;
