#!/usr/bin/env rakupp
# Writes examples/household.journal: six months of a fictional household.
#
#   rakupp showcase/ledger/tools/household.raku > showcase/ledger/examples/household.journal
#
# The point of keeping it is the balance assertions. Each `= $...` in the
# journal is a figure this program worked out, in integer cents, sharing no
# code with lib/Ledger.rakumod — so when the engine agrees with all 24 of
# them, two separate pieces of arithmetic have agreed. The amounts come from
# a fixed linear congruential sequence, so the output is the same every run.

my $seed = 20260101;
sub rnd(Int $lo, Int $hi) {          # cents in lo..hi
    $seed = ($seed * 1103515245 + 12345) % 2147483648;
    $lo + ($seed div 65536) % ($hi - $lo + 1)
}
sub pick(@a) { @a[rnd(0, @a.end)] }

sub usd(Int $c) {
    my $s = $c < 0 ?? '-' !! '';
    my $a = $c.abs;
    my $int = ($a div 100).flip.comb(3).join(',').flip;
    '$' ~ $s ~ $int ~ '.' ~ sprintf('%02d', $a % 100)
}

# entries: date, payee, code?, comment?, postings: [account, cents|Nil, text-override?, assert?]
my @entries;
my $n = 0;
sub entry($date, $payee, **@postings, :$comment, :$status = '*') {
    @entries.push: %( :$date, :$payee, :@postings, :$comment, :$status, order => $n++ );
}
sub P($acct, $cents, :$text, :$assert, :$elide) { %( :$acct, :$cents, :$text, :$assert, :$elide ) }

my @last = 0, 31, 28, 31, 30, 31, 30;
sub d($m, $day) { sprintf('2026-%02d-%02d', $m, $day) }

# ---- opening
entry(d(1,1), 'Opening balances',
    P('Assets:Checking', 421055),
    P('Assets:Savings', 1200000),
    P('Assets:Retirement:401k', 1840000),
    P('Liabilities:CreditCard', -31240),
    P('Equity:Opening Balances', -421055 - 1200000 - 1840000 + 31240, :elide));

my @acme-buy   = 4829, 4685, 4890, 5075, 5102, 5210;
my @power      = 12140, 11387, 9622, 7815, 6490, 7133;
my @dining     = 'Noodle Bar', 'Taco Cart', 'Blue Door Cafe', 'Osteria Lume', 'Golden Wok';
my @fun        = 'Pageturner Books', 'Rialto Cinema', 'Climbing Gym', 'Vinyl Corner';

for 1..6 -> $m {
    entry(d($m,1), 'Maple Street Apartments',
        P('Expenses:Housing:Rent', 185000), P('Assets:Checking', -185000, :elide));
    entry(d($m,2), 'Metro Transit',
        P('Expenses:Transport:Transit', 8600), P('Liabilities:CreditCard', -8600, :elide));
    entry(d($m,5), 'Streamly',
        P('Expenses:Subscriptions', 1549), P('Liabilities:CreditCard', -1549, :elide));
    entry(d($m,8), 'City Power & Light',
        P('Expenses:Housing:Utilities', @power[$m-1]), P('Assets:Checking', -@power[$m-1], :elide));
    entry(d($m,9), 'CloudBox',
        P('Expenses:Subscriptions', 299), P('Liabilities:CreditCard', -299, :elide));
    entry(d($m,12), 'FiberNet',
        P('Expenses:Housing:Utilities', 5999), P('Liabilities:CreditCard', -5999, :elide));
    for 3, 10, 17, 24, 31 -> $day {
        next if $day > @last[$m];
        my $c = rnd(4800, 14200);
        entry(d($m,$day), 'Green Basket Market',
            P('Expenses:Food:Groceries', $c), P('Liabilities:CreditCard', -$c, :elide));
    }
    for ($m %% 2 ?? (6, 19, 27) !! (9, 23)).list -> $day {
        my $c = rnd(1850, 7400);
        entry(d($m,$day), pick(@dining),
            P('Expenses:Food:Dining', $c), P('Liabilities:CreditCard', -$c, :elide));
    }
    {
        my $c = rnd(1200, 2800);
        entry(d($m,14), 'RideShare',
            P('Expenses:Transport:Rides', $c), P('Liabilities:CreditCard', -$c, :elide));
    }
    for ($m %% 2 ?? (11,) !! (11, 22)).list -> $day {
        my $c = rnd(1400, 6500);
        entry(d($m,$day), pick(@fun),
            P('Expenses:Fun', $c), P('Liabilities:CreditCard', -$c, :elide));
    }
    # payroll, twice a month, every line written out
    for 15, @last[$m] -> $day {
        my ($gross, $tax, $ins, $ret) = 375000, 81246, 14230, 22500;
        entry(d($m,$day), 'Northwind Payroll',
            P('Assets:Checking', $gross - $tax - $ins - $ret, :assert),
            P('Expenses:Taxes:Income', $tax),
            P('Expenses:Health:Insurance', $ins),
            P('Assets:Retirement:401k', $ret),
            P('Income:Salary', -$gross));
    }
    # fractional shares, $300.00 worth
    {
        my $price = @acme-buy[$m-1];
        my $qty = (30000 / $price * 10000).round / 10000;
        entry(d($m,20), 'Brightline Brokerage',
            P('Assets:Brokerage', 30000, text => sprintf('%.4f ACME @@ $300.00', $qty)),
            P('Assets:Checking', -30000, :elide), comment => 'monthly buy');
    }
    entry(d($m,26), 'Card payment',
        P('Liabilities:CreditCard', 'PAYOFF', :assert), P('Assets:Checking', 0, :elide));
    entry(d($m,27), 'Transfer to savings',
        P('Assets:Savings', 50000), P('Assets:Checking', -50000, :elide));
    entry(d($m,@last[$m]), 'Harbor Savings', comment => 'interest, 4.10% a year',
        P('Assets:Savings', 'INTEREST', :assert), P('Income:Interest', 0, :elide));
}

# ---- one-offs
entry(d(2,14), 'Osteria Lume', comment => 'dinner for three; I take the odd cent',
    P('Expenses:Food:Dining', 3334), P('Assets:Receivable:Sam', 3333),
    P('Assets:Receivable:Alex', 3333), P('Liabilities:CreditCard', -10000));
entry(d(2,16), 'Sam', comment => 'paid me back',
    P('Assets:Checking', 3333), P('Assets:Receivable:Sam', -3333, :elide));
entry(d(2,21), 'Alex', comment => 'paid me back',
    P('Assets:Checking', 3333), P('Assets:Receivable:Alex', -3333, :elide));
entry(d(3,3), 'Atlantic Air', comment => 'flights to Lisbon',
    P('Expenses:Travel:Flights', 61240), P('Liabilities:CreditCard', -61240, :elide));
entry(d(3,12), 'Corner Pharmacy',
    P('Expenses:Health:Pharmacy', 2318), P('Liabilities:CreditCard', -2318, :elide));
entry(d(5,9), 'Petal & Stem', comment => 'flowers for Mum',
    P('Expenses:Gifts', 5400), P('Liabilities:CreditCard', -5400, :elide));
entry(d(6,26), 'Brightline Brokerage', comment => 'dividend',
    P('Assets:Checking', 1184), P('Income:Dividends', -1184, :elide));

# ---- Lisbon, in euros
entry(d(4,7), 'ATM, Rua Augusta',
    P('Assets:Cash:EUR', 0, text => '€100.00 @@ $109.87'), P('Assets:Checking', -10987, :elide));
entry(d(4,7), 'Hotel Alfama', comment => 'four nights',
    P('Expenses:Travel:Hotel', 0, text => '€312.00 @@ $338.11'), P('Liabilities:CreditCard', -33811, :elide));
entry(d(4,8), 'Pastelaria Sol',
    P('Expenses:Travel:Food', 0, text => '€8.40'), P('Assets:Cash:EUR', 0, :elide));
entry(d(4,8), 'City Tram',
    P('Expenses:Travel:Transport', 0, text => '€6.00'), P('Assets:Cash:EUR', 0, :elide));
entry(d(4,9), 'Tasca do Rio',
    P('Expenses:Travel:Food', 0, text => '€41.50 @@ $45.21'), P('Liabilities:CreditCard', -4521, :elide));
entry(d(4,10), 'Tile Museum',
    P('Expenses:Travel:Fun', 0, text => '€15.00'), P('Assets:Cash:EUR', 0, :elide));
entry(d(4,10), 'Ribeira Market',
    P('Expenses:Travel:Food', 0, text => '€23.60'), P('Assets:Cash:EUR', 0, :elide));
entry(d(4,11), 'Sintra train',
    P('Expenses:Travel:Transport', 0, text => '€4.70'), P('Assets:Cash:EUR', 0, :elide));

# ---- order, then compute balances in cents and render
my @sorted = @entries.sort({ $^a<date> cmp $^b<date> || $^a<order> <=> $^b<order> });
my %bal;
my @out;
for @sorted -> %e {
    my @p = %e<postings>.list;
    for @p -> %p {
        if %p<cents> eq 'PAYOFF' { %p<cents> = -%bal{%p<acct>}; %p<assert-to> = 0 }
        if %p<cents> eq 'INTEREST' {
            %p<cents> = (%bal{%p<acct>} * 41 / 1000 / 12).round;
            %p<assert-to> = %bal{%p<acct>} + %p<cents>;
        }
    }
    # the elided posting takes the rest (USD only; EUR legs are handled by text)
    my $rest = [+] @p.grep({ !.<elide> && !.<text> }).map(*<cents>);
    $rest += 0;
    for @p.grep({ .<text> && .<text>.contains('@@') }) -> %p {
        %p<text> ~~ / '$' (<[\d.]>+) /; $rest += ($0.Str * 100).Int;
    }
    for @p.grep(*<elide>) -> %p { %p<cents> = -$rest }
    for @p -> %p {
        next if %p<text> && !%p<text>.contains('$') && %p<acct>.starts-with('Assets:Cash');
        next if %p<text> && %p<acct>.starts-with('Expenses:Travel');
        next if %p<acct> eq 'Assets:Cash:EUR';
        %bal{%p<acct>} += %p<cents>;
        %p<assert-to> //= %bal{%p<acct>} if %p<assert>;
    }
    my $head = "%e<date> %e<status> %e<payee>";
    $head ~= "  ; %e<comment>" if %e<comment>;
    @out.push: $head;
    for @p -> %p {
        my $line = '    ' ~ %p<acct>;
        unless %p<elide> {
            my $amt = %p<text> // usd(%p<cents>);
            $line = sprintf('%-36s %14s', $line, $amt);
            $line ~= ' = ' ~ usd(%p<assert-to>) if %p<assert>;
        }
        @out.push: $line;
    }
    @out.push: '';
}

my @acme-p = 4862, 4710, 4985, 5130, 5012, 5348;
my @eur-p  = '1.0412', '1.0489', '1.0655', '1.0837', '1.0761', '1.0893';
my @prices = (1..6).map({ sprintf("P %s ACME \$%d.%02d", d($_, @last[$_]), @acme-p[$_-1] div 100, @acme-p[$_-1] % 100) });
my @eur    = (1..6).map({ "P {d($_, @last[$_])} EUR \${@eur-p[$_-1]}" });

say q:to/END/.chomp;
; household.journal — six months of a fictional household's money, in the
; plain-text format of ledger and hledger. Nothing here is anybody's real
; account; the shop names are made up.
;
; Every amount is written as it would appear on a statement. The `= $...`
; after a posting is a balance assertion: the account must hold exactly that
; much once the posting is made, as the bank's own figure would say.

account Assets:Checking
account Assets:Savings
account Assets:Brokerage
account Assets:Retirement:401k
account Assets:Cash:EUR
account Liabilities:CreditCard
account Income:Salary
account Income:Interest
account Income:Dividends
account Equity:Opening Balances

; what a month is meant to cost
~ monthly
    Expenses:Food:Groceries              $450.00
    Expenses:Food:Dining                 $150.00
    Expenses:Transport                   $140.00
    Expenses:Fun                          $80.00
    Expenses:Subscriptions                $20.00
    Assets:Checking

; month-end prices, for valuing what is held at market
END
say @prices.join("\n");
say @eur.join("\n");
say '';
say @out.join("\n").chomp;
