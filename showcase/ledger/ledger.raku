#!/usr/bin/env rakupp
# ledger — plain-text accounting, where no amount is ever rounded.
#
#   rakupp showcase/ledger/ledger.raku balance                 what every account holds
#   rakupp showcase/ledger/ledger.raku balance --depth=2
#   rakupp showcase/ledger/ledger.raku balance Assets --value  at the latest prices
#   rakupp showcase/ledger/ledger.raku register Groceries      each posting, with a running total
#   rakupp showcase/ledger/ledger.raku monthly                 income, spending and savings by month
#   rakupp showcase/ledger/ledger.raku monthly Dining
#   rakupp showcase/ledger/ledger.raku budget                  the `~ monthly` budget against what was spent
#   rakupp showcase/ledger/ledger.raku print @Lisbon           transactions, written out again
#   rakupp showcase/ledger/ledger.raku check -f=examples/broken.journal
#   rakupp showcase/ledger/ledger.raku float                   what floating point would have made of it
#   rakupp showcase/ledger/ledger.raku --check
#
# Without -f it reads examples/household.journal, or $LEDGER_FILE when that
# is set. The arithmetic lives in lib/Ledger.rakumod; this file is the
# terminal.

use lib $?FILE.IO.parent.add('lib').Str;
use Ledger;

my %*SUB-MAIN-OPTS = :named-anywhere;

constant RESET = "\e[0m";
my $colour = $*OUT.t && !%*ENV<NO_COLOR>;
sub dim($s)  { $colour ?? "\e[2m$s" ~ RESET !! $s }
sub bold($s) { $colour ?? "\e[1m$s" ~ RESET !! $s }
sub red($s)  { $colour && $s.contains('-') ?? "\e[31m$s" ~ RESET !! $s }

sub rpad($s, $w) { $s ~ ' ' x max(0, $w - $s.chars) }
sub lpad($s, $w) { ' ' x max(0, $w - $s.chars) ~ $s }

#| A bar in eighths of a character.
sub bar($cells) {
    my $eighths = ($cells * 8).round;
    '█' x ($eighths div 8) ~ ('', '▏', '▎', '▍', '▌', '▋', '▊', '▉')[$eighths % 8]
}

#| "Expenses:Food:Groceries" in 17 columns is "Ex:Food:Groceries":
#| the leading names give way first, as ledger's register abbreviates them.
sub abbrev(Str $account, Int $w) {
    my @parts = $account.split(':');
    for ^(@parts - 1) -> $i {
        last if @parts.join(':').chars <= $w;
        @parts[$i] = @parts[$i].substr(0, 2);
    }
    my $s = @parts.join(':');
    $s.chars <= $w ?? $s !! $s.substr(0, $w - 1) ~ '…'
}
sub trunc(Str $s, Int $w) { $s.chars <= $w ?? $s !! $s.substr(0, $w - 1) ~ '…' }

#| An amount as the journal wrote it, at its own precision rather than the
#| commodity's: `@ $1.0837` must not come back as `@ $1.08`.
sub as-written(Journal $j, Amount $a) {
    $j.fmt($a, prec => $a.style<prec> // decimals($a.qty))
}

sub month-of(Date $d) { $d.Str.substr(0, 7) }

#| What a posting is worth in the home commodity on its own date: its cost
#| when it has one, and otherwise its value at the price of the day.
sub worth(Journal $j, Posting $p) {
    $j.value(Balance.new.add($p.weight), $p.txn.date).qty($j.home)
}

# ---- balance --------------------------------------------------------------
sub balances(Journal $j, @query, :$begin, :$end, :$depth, :$cost) {
    my %bal;
    for $j.postings(:$begin, :$end, :@query) -> $p {
        my $name = $depth ?? $p.account.split(':').head($depth).join(':') !! $p.account;
        (%bal{$name} //= Balance.new).add($cost ?? $p.weight !! $p.amount);
    }
    %bal
}

sub balance-report(Journal $j, @query, :$begin, :$end, :$depth, Bool :$flat, Bool :$cost, Bool :$value) {
    my %bal = balances($j, @query, :$begin, :$end, :$depth, :$cost);
    if $value {
        my $on = $end ?? $end - 1 !! $j.last-date;
        %bal{$_} = $j.value(%bal{$_}, $on) for %bal.keys;
    }

    # every account and every parent of one, holding all that is beneath it
    my (%total, %children);
    for %bal.kv -> $name, $b {
        my @parts = $name.split(':');
        for 1..@parts -> $k {
            my $n = @parts.head($k).join(':');
            (%total{$n} //= Balance.new).merge($b);
            %children{$k > 1 ?? @parts.head($k - 1).join(':') !! ''}{$n} = True;
        }
    }

    my @rows;
    if $flat {
        @rows = %bal.keys.sort.grep({ !%bal{$_}.is-zero }).map({ [%bal{$_}, 0, $_] });
    }
    else {
        # A parent with one child and nothing of its own shares its line:
        # "Liabilities:CreditCard", not two lines saying the same amount.
        sub walk($name, $label, $indent) {
            my @kids = (%children{$name} // {}).keys.sort.grep({ !%total{$_}.is-zero });
            if @kids == 1 && !%bal{$name} {
                my $kid = @kids[0];
                return walk($kid, "$label:{$kid.split(':').tail}", $indent);
            }
            @rows.push: [%total{$name}, $indent, $label];
            walk($_, .split(':').tail, $indent + 1) for @kids;
        }
        walk($_, $_, 0) for (%children{''} // {}).keys.sort.grep({ !%total{$_}.is-zero });
    }

    my $grand = Balance.new;
    $grand.merge($_) for %bal.values;
    my @lines = |@rows.map({ |$j.fmt-balance(.[0]) }), |$j.fmt-balance($grand);
    my $w = max(12, |@lines».chars);
    for @rows -> $row {
        my ($b, $indent, $label) = @$row;
        my @amt = $j.fmt-balance($b);
        say red(lpad($_, $w)) for @amt.head(*-1);
        say red(lpad(@amt.tail, $w)) ~ '  ' ~ '  ' x $indent ~ ($indent ?? $label !! bold($label));
    }
    say dim('-' x $w);
    say red(lpad($_, $w)) for $j.fmt-balance($grand);
}

# ---- register -------------------------------------------------------------
sub register-report(Journal $j, @query, :$begin, :$end, Bool :$cost) {
    my $run = Balance.new;
    my $last-txn;
    for $j.postings(:$begin, :$end, :@query) -> $p {
        my $amount = $cost ?? $p.weight !! $p.amount;
        $run.add($amount);
        my @tot = $j.fmt-balance($run);
        my $first = !($last-txn === $p.txn);
        $last-txn = $p.txn;
        say ($first ?? $p.txn.date.Str ~ ' ' ~ rpad(trunc($p.txn.payee, 20), 20) !! ' ' x 31) ~ ' '
            ~ rpad(abbrev($p.account, 22), 22) ~ ' '
            ~ red(lpad($j.fmt($amount), 12)) ~ ' ' ~ red(lpad(@tot[0], 12));
        say ' ' x 67 ~ red(lpad($_, 12)) for @tot.skip(1);
    }
}

# ---- print ----------------------------------------------------------------
sub txn-text(Journal $j, Transaction $t, Bool :$explicit --> Str) {
    my @out;
    my $head = $t.date.Str;
    $head ~= " {$t.status}" if $t.status;
    $head ~= " ({$t.code})" if $t.code.defined;
    $head ~= " {$t.payee}" if $t.payee;
    $head ~= "  ; {$t.comment}" if $t.comment.defined;
    @out.push: $head;
    for $t.postings -> $p {
        next if $p.inferred && !$explicit && $p !=== $t.postings.first(*.inferred);
        my $line = '    ' ~ $p.account;
        if !$p.inferred || $explicit {
            my $amt = as-written($j, $p.amount);
            $line = rpad($line, 36) ~ ' ' ~ lpad($amt, max(14, $amt.chars));
            $line ~= " {$p.kind} {as-written($j, $p.price)}" if $p.price;
            $line ~= " = {as-written($j, $p.assert)}" if $p.assert;
        }
        $line ~= "  ; {$p.comment}" if $p.comment.defined;
        @out.push: $line;
    }
    @out.join("\n") ~ "\n\n"
}

sub print-report(Journal $j, @query, :$begin, :$end, Bool :$explicit) {
    my %want = $j.postings(:$begin, :$end, :@query).map({ .txn.WHICH => True });
    print txn-text($j, $_, :$explicit) for $j.ordered.grep({ %want{.WHICH} });
}

# ---- monthly --------------------------------------------------------------
#| What came in (Income) and went out (Expenses) in each month, in the home
#| commodity; with a query, what the matching postings came to instead.
sub flows(Journal $j, @query, :$begin, :$end) {
    my (@months, %in, %out, %sum);
    for $j.postings(:$begin, :$end, :@query) -> $p {
        my $m = month-of($p.txn.date);
        @months.push($m);
        my $q = worth($j, $p);
        if @query { %sum{$m} += $q }
        elsif $p.account.starts-with('Income')   { %in{$m}  -= $q }
        elsif $p.account.starts-with('Expenses') { %out{$m} += $q }
    }
    @months.unique.list, %in, %out, %sum
}

sub monthly-report(Journal $j, @query, :$begin, :$end) {
    my $home = $j.home;
    my sub money($q) { $j.fmt(Amount.new(commodity => $home, qty => $q)) }
    my ($months, $in, $out, $sum) = flows($j, @query, :$begin, :$end);
    my @months = @$months;
    my %in     = %$in;
    my %out    = %$out;
    my %sum    = %$sum;

    if @query {
        say bold("{@query.join(' ')}, month by month, in $home");
        say '';
        my $max = max(1, |%sum.values».abs);
        for @months -> $m {
            my $q = %sum{$m} // 0;
            say "$m  {lpad(money($q), 12)}  {bar($q.abs / $max * 30)}";
        }
        say dim(' ' x 7 ~ '-' x 14);
        my $total = [+] %sum.values;
        say "{lpad('total', 7)}  {lpad(money($total), 12)}   average {money($total / max(1, +@months))}";
        return;
    }

    say bold("Income and spending, month by month, in $home");
    say '';
    say bold("month        income      spending           net    saved");
    for @months -> $m {
        my ($i, $o) = %in{$m} // 0, %out{$m} // 0;
        my $rate = $i ?? ($i - $o) / $i !! 0;
        say "$m  {lpad(money($i), 12)}  {lpad(money($o), 12)}  {red(lpad(money($i - $o), 12))}  "
            ~ "{lpad(sprintf('%.1f%%', $rate * 100), 6)}  {bar(max(0, $rate) * 30)}";
    }
    my ($i, $o) = ([+] %in.values), ([+] %out.values);
    say dim('-' x 56);
    say "{lpad('total', 7)}  {lpad(money($i), 12)}  {lpad(money($o), 12)}  {red(lpad(money($i - $o), 12))}  "
        ~ lpad(sprintf('%.1f%%', ($i ?? ($i - $o) / $i !! 0) * 100), 6);
}

# ---- budget ---------------------------------------------------------------
#| What was spent on an account and everything beneath it.
sub spent(Journal $j, Str $account, @postings) {
    [+] @postings.grep({ .account eq $account || .account.starts-with("$account:") }).map({ worth($j, $_) })
}

sub budget-report(Journal $j, :$begin, :$end) {
    die "{$j.file} has no `~ monthly` budget\n" unless $j.budget;
    my $home = $j.home;
    my sub money($q) { $j.fmt(Amount.new(commodity => $home, qty => $q)) }
    my @postings = $j.postings(:$begin, :$end);
    my @months = @postings.map({ month-of(.txn.date) }).unique;
    my @lines = $j.budget.grep({ !.account.starts-with('Assets') && !.account.starts-with('Liabilities') });

    say bold("The monthly budget against what was spent, {@months.head} to {@months.tail} ({+@months} months)");
    say '';
    my $w = max(|@lines.map(*.account.chars), 12);
    say bold(rpad('', $w) ~ '         spent        budget   used');
    my ($all-spent, $all-budget) = 0, 0;
    for @lines -> $b {
        my $spent = spent($j, $b.account, @postings);
        my $budget = $b.amount.qty * +@months;
        $all-spent += $spent;
        $all-budget += $budget;
        my $r = $budget ?? $spent / $budget !! 0;
        my $bar = $r <= 1 ?? bar($r * 20) !! bar(20) ~ '│' ~ bar(($r - 1) * 20);
        say "{rpad($b.account, $w)}  {lpad(money($spent), 12)}  {lpad(money($budget), 12)}  "
            ~ "{lpad(sprintf('%d%%', ($r * 100).round), 5)}  $bar";
    }
    say dim('-' x ($w + 35));
    say "{rpad('', $w)}  {lpad(money($all-spent), 12)}  {lpad(money($all-budget), 12)}  "
        ~ lpad(sprintf('%d%%', ($all-spent / $all-budget * 100).round), 5);
}

# ---- accounts and payees --------------------------------------------------
sub accounts-report(Journal $j) {
    my %n;
    %n{.account}++ for $j.postings;
    say "{lpad(%n{$_}, 4)}  $_" for %n.keys.sort;
}
sub payees-report(Journal $j) {
    my %n;
    %n{.payee}++ for $j.transactions;
    say "{lpad(%n{$_}, 4)}  $_" for %n.keys.sort({ %n{$^b} <=> %n{$^a} || $^a cmp $^b });
}

# ---- check ----------------------------------------------------------------
sub problems(Journal $j) {
    for $j.problems.sort(*<line>) -> %p {
        note "{$j.file}:{%p<line>}: {%p<message>}";
        note "    $_" for %p<context>.list;
    }
}

sub check-report(Journal $j) {
    if $j.problems {
        problems($j);
        note "{+$j.problems} problem{'s' if $j.problems > 1} in {$j.file}";
        exit 1;
    }
    my @c = $j.styles.keys.sort;
    my $postings = [+] $j.transactions.map(*.postings.elems);
    say $j.file.IO.basename;
    say "  {$j.first-date} to {$j.ordered.tail.date}, {+$j.transactions} transactions, $postings postings";
    say "  {+$j.postings.map(*.account).unique} accounts, {+@c} commodities ({@c.join(', ')}), {+$j.prices} prices";
    say "  every transaction balances to exactly zero";
    say "  all {$j.assertions} balance assertions hold";
}

# ---- float ----------------------------------------------------------------
#| The same journal again, with every amount a binary floating-point number,
#| as a spreadsheet or a Num would hold it. Nothing else changes: the same
#| sums, in the same order, compared the same way.
sub float-report(Journal $j) {
    my (@residue, @failed, @unprice);
    my %held;                                   # account => commodity => Num
    my %exact;
    my $checked = 0;
    for $j.ordered -> $t {
        my %sum;
        my %f;                                  # posting => its Num amount
        for $t.postings.grep({ !.inferred }) -> $p {
            my $w = $p.weight;
            %sum{$w.commodity} += $w.qty.Num;
            %f{$p.WHICH} = $p.amount.qty.Num;
        }
        # one posting left blank takes whatever the others leave over
        for $t.postings.grep(*.inferred) -> $p {
            %f{$p.WHICH} = -(%sum{$p.amount.commodity} // 0e0);
            %sum{$p.amount.commodity} = 0e0;
        }
        my $worst = %sum.values».abs.max;
        @residue.push: [$t, $worst] if $worst > 0;
        for $t.postings -> $p {
            %held{$p.account}{$p.amount.commodity} += %f{$p.WHICH};
            (%exact{$p.account} //= Balance.new).add($p.amount);
            if $p.assert -> $want {
                $checked++;
                @failed.push: [$p, %held{$p.account}{$want.commodity}]
                    unless %held{$p.account}{$want.commodity} == $want.qty.Num;
            }
            if $p.kind && $p.kind eq '@@' {
                my $unit = $p.price.qty.Num / $p.amount.qty.Num;
                @unprice.push: [$p, $unit * $p.amount.qty.Num] unless $unit * $p.amount.qty.Num == $p.price.qty.Num;
            }
        }
    }

    say bold('The same journal, summed in binary floating point');
    say '';
    say "Every amount below is held as a double instead of a fraction. The sums are";
    say "the same sums, made in the same order, and compared with == as before.";
    say '';
    say "  transactions that no longer sum to zero     {lpad(+@residue, 3)} of {+$j.transactions}";
    for @residue.sort({ -$_[1] }).head(3) -> $row {
        my ($t, $r) = @$row;
        say dim("      off by {$r.Str.fmt('%-24s')} {$t.date} {$t.payee}");
    }
    say dim('      each one alone is short enough to come out even') unless @residue;
    say "  balance assertions that fail                {lpad(+@failed, 3)} of $checked";
    for @failed.head(3) -> $row {
        my ($p, $have) = @$row;
        say dim("      {$p.txn.date} {rpad($p.account, 24)} holds {$have}, not {$p.assert.qty.Num}");
    }
    my $n-at = $j.transactions.map(*.postings.grep({ .kind && .kind eq '@@' }).elems).sum;
    say "  prices paid with @@ that do not multiply    {lpad(+@unprice, 3)} of $n-at";
    say "  back to what was paid";
    for @unprice.head(3) -> $row {
        my ($p, $back) = @$row;
        say dim("      {$p.txn.date} {as-written($j, $p.amount)} @@ {as-written($j, $p.price)}: "
              ~ "the unit price times {$p.amount.qty.Num} is {$back}");
    }
    say '';
    say 'And the running totals drift:';
    for <Assets:Checking Assets:Savings Liabilities:CreditCard> -> $a {
        my $num = %held{$a}{$j.home};
        my $q = %exact{$a}.qty($j.home);
        say "  {rpad($a, 24)} exactly {lpad($j.fmt(Amount.new(commodity => $j.home, qty => $q)), 11)}    "
            ~ "in doubles {$num}";
    }
    say '';
    say dim('In lib/Ledger.rakumod each of these is a FatRat, and all of them are exact.');
}

# ---- --check --------------------------------------------------------------
sub self-check() {
    my ($ok, $bad) = 0, 0;
    my sub check(Bool() $cond, Str $what) {
        if $cond { $ok++ } else { $bad++; say "FAILED: $what" }
    }
    my $here = $?FILE.IO.parent;
    my $j = Journal.from-file($here.add('examples/household.journal').Str);
    my $home = $j.home;
    my sub usd($q) { Amount.new(commodity => '$', qty => $q.FatRat) }
    my sub same(Balance $a, Balance $b) { $a.copy.merge($b.neg).is-zero }

    # the journal reads, balances and reconciles
    check !$j.problems, 'household.journal reads with no problems';
    check $j.transactions == 145, '145 transactions';
    check $j.assertions == 24, 'its 24 balance assertions are all checked';
    my @uneven = $j.transactions.grep(-> $t {
        my $sum = Balance.new;
        $sum.add(.weight) for $t.postings;
        !$sum.is-zero
    });
    check !@uneven, "every transaction's postings sum to exactly zero (not: {@uneven».payee.join(', ')})";
    check $j.transactions.map(*.postings.Slip).map({ |(.amount, .cost, .price, .assert) }).grep(*.defined)
            .map(*.qty).grep({ $_ !~~ FatRat }) == 0,
          'every quantity is still a FatRat: none has degraded into a Num';

    # the reports agree with each other
    my %flat = balances($j, ());
    my $total = Balance.new;
    $total.merge($_) for balances($j, (), :cost).values;
    check $total.is-zero, 'at cost, all the accounts together hold exactly nothing';
    my %run;
    (%run{.account} //= Balance.new).add(.amount) for $j.postings;
    check %flat.keys.grep({ !same(%flat{$_}, %run{$_}) }) == 0,
          'every account\'s balance is the end of its running total';
    my %two = balances($j, (), :depth(2));
    check %two.keys.grep(-> $k {
              my $sum = Balance.new;
              $sum.merge(%flat{$_}) for %flat.keys.grep({ $_ eq $k || .starts-with("$k:") });
              !same($sum, %two{$k})
          }) == 0, 'each --depth=2 total is the sum of the accounts beneath it';

    # print writes back exactly what it read
    my $again = Journal.from-text($j.ordered.map({ txn-text($j, $_) }).join);
    check !$again.problems && $again.transactions == $j.transactions, 'print\'s output reads back in';
    check (^$j.transactions).grep(-> $i {
              my ($a, $b) = $j.ordered[$i], $again.ordered[$i];
              my &key = { .account ~ ' ' ~ .amount.commodity ~ ' ' ~ .amount.qty.nude.join('/') };
              $a.date ne $b.date || $a.payee ne $b.payee
                  || $a.postings.map(&key).join('|') ne $b.postings.map(&key).join('|')
          }) == 0, 'and what it reads back is the same, posting for posting';

    # the answers a reader can work out with a pencil
    my %b = balances($j, ());
    check %b<Assets:Receivable:Sam>.is-zero && %b<Assets:Receivable:Alex>.is-zero,
          'the dinner is split $33.34 + $33.33 + $33.33, and both friends paid their share back';
    check %b<Assets:Cash:EUR>.qty('€') == 42.3, 'the wallet comes home with €100.00 − 8.40 − 6.00 − 15.00 − 23.60 − 4.70 = €42.30';
    my $hotel = $j.postings(query => ('@Hotel',)).first;
    my $unit = $hotel.cost.qty / $hotel.amount.qty;
    check $unit.nude eqv (33811, 31200), 'the hotel\'s euro cost exactly 33811/31200 dollars';
    check $unit * 312 == 338.11, 'and 312 of them cost exactly $338.11 again';
    check %b<Assets:Brokerage>.qty('ACME') == 36.3004
              && $j.value(%b<Assets:Brokerage>, Date.new('2026-06-30')).qty('$') == 1941.345392,
          'the 36.3004 shares are worth exactly 36.3004 × $53.48 = $1,941.345392 at June\'s price';
    check $j.rate('EUR', '$', Date.new('2026-04-06')) == 1.0655 && $j.rate('EUR', '$', Date.new('2026-07-01')) == 1.0893,
          'a price holds from its date until the next one';
    check $j.fmt(usd(-1234.5)) eq '$-1,234.50' && $j.fmt(usd(0.005)) eq '$0.01' && $j.fmt(usd(-0.005)) eq '$-0.01',
          'amounts are written as the journal writes them, rounded half away from zero';

    # the monthly and budget reports count what balance counts
    my ($, $in, $out) = flows($j, ());
    check ([+] $in.values) - ([+] $out.values)
              == ([+] $j.postings(query => <Assets Liabilities Equity>).map({ worth($j, $_) })),
          'what monthly says was saved is what the assets and liabilities grew by';
    check spent($j, 'Expenses:Transport', $j.postings.list)
              == balances($j, (), :depth(2))<Expenses:Transport>.qty($home),
          'budget\'s transport, Transit and Rides together, is the balance report\'s';

    # a broken journal says what is wrong with it, and where
    my $broken = Journal.from-file($here.add('examples/broken.journal').Str);
    my @where = $broken.problems.sort(*<line>).map(*<line>);
    check @where eqv [10, 15, 20, 23, 26, 30], "broken.journal's six problems are found on their own lines (found: @where[])";

    say $bad ?? "$bad of {$ok + $bad} checks FAILED" !! "all $ok checks passed";
    exit $bad ?? 1 !! 0;
}

# ---- MAIN -----------------------------------------------------------------
sub period(Str $p) {
    given $p {
        when /^ \d**4 $/            { Date.new(+$p, 1, 1), Date.new(+$p + 1, 1, 1) }
        when /^ (\d**4) '-' (\d\d) $/ { my $d = Date.new(+$0, +$1, 1); $d, $d.later(:1month) }
        default { die "--period is a year or a month, as 2026 or 2026-03\n" }
    }
}

sub MAIN(
    Str $command = 'balance',   #= balance, register, print, monthly, budget, accounts, payees, check or float
    *@query,                    #= accounts to report on; not:NAME leaves one out, @NAME is a payee
    Str  :f(:$file),            #= the journal (default: $LEDGER_FILE, or examples/household.journal)
    Str  :b(:$begin),           #= from this date
    Str  :e(:$end),             #= up to, and not including, this date
    Str  :p(:$period),          #= one year or one month, as 2026 or 2026-03
    Int  :$depth,               #= balance: no deeper than this many levels
    Bool :$flat,                #= balance: full names, no tree
    Bool :B(:$cost),            #= at what was paid, in the commodity it was paid in
    Bool :V(:$value),           #= balance: at the latest prices
    Bool :$explicit,            #= print: write the amounts that were left out
    Bool :$check,               #= run the self-checks
) {
    self-check() if $check;
    my $path = $file // %*ENV<LEDGER_FILE> // $?FILE.IO.parent.add('examples/household.journal').Str;
    my $j = Journal.from-file($path);
    my ($from, $to) = $period ?? period($period) !! ($begin && Date.new($begin), $end && Date.new($end));
    $from ||= Date;
    $to   ||= Date;

    if $command ne 'check' && $j.problems {
        problems($j);
        note "{+$j.problems} problem{'s' if $j.problems > 1}: `check` lists them; the reports below leave out what could not be read";
        note '';
    }
    given $command {
        when 'balance'  | 'bal' | 'b' { balance-report($j, @query, :begin($from), :end($to), :$depth, :$flat, :$cost, :$value) }
        when 'register' | 'reg' | 'r' { register-report($j, @query, :begin($from), :end($to), :$cost) }
        when 'print'    | 'p'         { print-report($j, @query, :begin($from), :end($to), :$explicit) }
        when 'monthly'                { monthly-report($j, @query, :begin($from), :end($to)) }
        when 'budget'                 { budget-report($j, :begin($from), :end($to)) }
        when 'accounts'               { accounts-report($j) }
        when 'payees'                 { payees-report($j) }
        when 'check'                  { check-report($j) }
        when 'float'                  { float-report($j) }
        default { note "unknown command `$command`"; note $*USAGE; exit 2 }
    }
}
