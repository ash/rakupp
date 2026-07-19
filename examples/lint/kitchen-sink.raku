# A small program with several planted mistakes, to show a whole --lint run at
# once.  Run:  rakupp --lint kitchen-sink.raku
#
# It reads like working code, but the linter flags six real problems and two
# style notes without ever executing it.

sub parse-price($s, $currency) {          # $currency is never used  -> note
    my $cleaned = $s.trim;
    my $amount  = $cleaned.subst('$', '');
    return $amount.Num;                   # redundant final return -> note
}

sub describe($n) {
    return "free"  if $n == 0;
    return "cheap" if $n < 10;
    return "pricey";
    "unreachable tier";                   # unreachable-code
}

sub format-tax($base) {                   # never called -> unused-routine
    $base * 1.2;
}

my $price = parse-price('$42.50', 'USD');
my $label = describe($price);
my $note  = "TODO: currencies";           # unused-variable

if $label == "free" {                     # numeric == on a string
    say "on the house";
}

my $seen = 0;
$seen = $seen;                            # self-assignment

if True {                                 # constant-condition
    say "price: $price ($label)";
}
