# BROKE (guarded, same leg 18): the Perl5Var punctuation-variable checks
# ($; $. $? %! @- ...) originally threw on sight, which would outlaw the
# LEGAL readings below; they were narrowed to fire only when directly
# assigned. This locks the legal set.
my $;                       # anonymous scalar declaration
my ($a, $, $b) = 1, 2, 3;   # anonymous slot in a list declaration
die 'anon list slot broken' unless $a == 1 && $b == 3;
$ = 5;                      # assignment to the anonymous state var
class P { has $.x; has %!h; method m { %!h<k> = $.x; %!h<k> } }
die '$.x / %!h attribute forms broken' unless P.new(x => 9).m == 9;
die '$?FILE broken' unless $?FILE.IO.basename eq 'perl5var-legal-readings.raku';
my @args-probe = (my $r = \(1)); # $ before ( stays a contextualizer, not an error
die 'contextualizer after sigil broken' unless so @args-probe;
say 'PASS';
