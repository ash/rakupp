my $name = 'World';
my $n = 3;
say "Hello, {$name}! n/2 = {$n / 2}";
say 'single {name} stays';
if 'abc-123' ~~ / (\d+) / { say "got $0" }
my $text = q:to/END/;
    name and n here are text
    END
print $text;
say 10 / $n;
my %h = %(a => 1, b => 2);
say %h<a> + %h{'b'};
say <name n>;
my $x = 3;
say 'ab' x $x;
say $x * $x;
