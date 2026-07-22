# Regression: two composing bugs let `$.name` "work" against a class with no
# such attribute (has $.naam):
#   1. the default .new stored UNDECLARED named args into the attr store
#      (Rakudo binds nameds to declared public attributes only);
#   2. `$.name` read the raw attr store, and the universal `.name` builtin
#      answered the class name for user objects instead of dying.
# Rakudo dies X::Method::NotFound at the $.name call. Reported 2026-07-22
# (Cafe snippet #3).

my $ok = True;

class Cafe {
    has $.naam;
    method peek { $.name }
}

my $cafe = Cafe.new(name => "Paris", naam => "Riga");

# declared public attr binds; the stray is silently ignored
unless $cafe.naam eq 'Riga' {
    say "FAIL: declared attr did not bind: {$cafe.naam.raku}";
    $ok = False;
}

# the stray must not be readable through $.name — that's a method call now
my $died = False;
try { $cafe.peek; CATCH { default { $died = True } } }
unless $died {
    say 'FAIL: $.name on a class without name should die X::Method::NotFound';
    $ok = False;
}

# .^name still universal; a real name method/attr still works
unless Cafe.^name eq 'Cafe' && Cafe.new.^name eq 'Cafe' {
    say 'FAIL: .^name broken';
    $ok = False;
}
class Named { has $.name }
unless Named.new(name => 'x').name eq 'x' {
    say 'FAIL: real name attr broken';
    $ok = False;
}

say 'PASS' if $ok;
