# Ledger — plain-text accounting, where no amount is ever rounded.
#
# A journal is a text file of transactions. Each one is a date, a payee, and
# two or more postings, each moving an amount into or out of an account:
#
#     2026-02-14 * Osteria Lume  ; dinner, split three ways
#         Expenses:Food:Dining        $33.34
#         Assets:Receivable:Sam       $33.33
#         Assets:Receivable:Alex      $33.33
#         Liabilities:CreditCard
#
# The one rule is double entry: a transaction's postings sum to zero, so money
# is only ever moved, never made or lost. One posting may leave its amount
# out, and it is inferred as whatever makes the sum zero. That is the format
# of John Wiegley's ledger and of hledger, and this module reads the core of
# it: transactions, prices (`@`, `@@` and `P` lines), balance assertions
# (`= $1,234.56`), and `~ monthly` budgets.
#
# Every quantity here is a FatRat. "12.50" is read as the fraction 25/2, sums
# of fractions are fractions, and so a transaction balances when its sum is
# exactly zero and an assertion holds when the account holds exactly that
# much. A FatRat rather than a Rat because a Rat whose denominator outgrows
# 64 bits quietly turns into a Num; a FatRat never does. Rounding happens in
# one place only: the last step of turning a quantity into text.

unit module Ledger;

# ---------------------------------------------------------------- amounts ---

#| The fewest decimals that write $q exactly. A sum of written decimals
#| always has some; a unit price like 500/3 has none, and gets twelve.
sub decimals($q) is export {
    my $k = 0;
    $k++ until ($q * 10 ** $k).denominator == 1 || $k >= 12;
    $k
}

#| A quantity of one commodity: $12.50, 6.2125 ACME, €312.00.
class Amount is export {
    has Str    $.commodity = '';
    has FatRat $.qty;
    has        %.style;          # how it was written, when it came from the text

    method neg()      { Amount.new(:$!commodity, qty => -$!qty) }
    method times($n)  { Amount.new(:$!commodity, qty => $!qty * $n) }
}

#| Several commodities at once: what an account holds, or what a
#| transaction's postings add up to. Zero quantities are dropped, so a
#| balance is zero exactly when it holds nothing.
class Balance is export {
    has %.q;

    method add(Amount $a) {
        %!q{$a.commodity} += $a.qty;
        %!q{$a.commodity}:delete if %!q{$a.commodity} == 0;
        self
    }
    method merge(Balance $b)  { self.add($_) for $b.amounts; self }
    method qty(Str $c)        { %!q{$c} // 0.FatRat }
    method amounts()          { %!q.keys.sort.map({ Amount.new(commodity => $_, qty => %!q{$_}) }) }
    method is-zero()          { !%!q }
    method neg()              { my $b = Balance.new; $b.add(.neg) for self.amounts; $b }
    method copy()             { Balance.new.merge(self) }
}

# --------------------------------------------------------------- postings ---

class Transaction { ... }

class Posting is export {
    has Str    $.account;
    has Amount $.amount is rw;          # what the account receives; negative is out
    has Amount $.price;                 # as written after @ or @@
    has Str    $.kind;                  # '@' (per unit) or '@@' (in total)
    has Amount $.assert;                # what the account must hold after this
    has Str    $.comment;
    has Int    $.line;
    has Bool   $.inferred is rw = False;
    has Transaction $.txn is rw;

    #| What a priced posting cost, in the price's commodity. Ten shares
    #| `@ $41.20` cost $412.00; three `@@ $500.00` cost $500.00 whatever the
    #| unit price comes to — here 500/3, which no decimal can write down.
    method cost() {
        return Amount unless $!price;
        $!kind eq '@'
            ?? $!price.times($!amount.qty)
            !! $!price.times($!amount.qty.sign)
    }

    #| What the posting contributes to its transaction's balance.
    method weight() { self.cost // $!amount }
}

class Transaction is export {
    has Date    $.date;
    has Str     $.status = '';
    has Str     $.code;
    has Str     $.payee = '';
    has Str     $.comment;
    has Int     $.line;
    has Posting @.postings;
}

# ---------------------------------------------------------------- grammar ---

#| The journal format, one entry at a time. An entry that does not read is
#| not the end of the file: `<unreadable>` takes it, from its first line to
#| the next line that is not indented, so that every problem in a file is
#| reported in one pass rather than the first one only.
grammar JournalGrammar is export {
    token TOP { [ <entry> || <unreadable> ]* }

    proto token entry {*}
    token entry:sym<blank>       { \h* \n }
    token entry:sym<comment>     { <[;#%|*]> \N* \n }
    # A transaction ends at the first line that is not one of its postings,
    # and that line must not be indented: an indented line that does not
    # read as a posting makes the whole transaction unreadable, rather than
    # quietly ending it one posting short.
    token entry:sym<transaction> { <header> \h* \n <posting-line>+ <!before \h+ \S> }
    token entry:sym<periodic>    { '~' \h+ $<period>=[\w+] \h* <comment>? \n <posting-line>+ <!before \h+ \S> }
    token entry:sym<price>       { <price-line> \h* \n }
    token entry:sym<account>     { 'account' \h+ <account> \h* <comment>? \n [ \h+ \N* \n ]* }
    token entry:sym<commodity>   { 'commodity' \h+ \N* \n [ \h+ \N* \n ]* }
    token unreadable             { \N* \n [ \h+ \N* \n ]* }

    token price-line  { 'P' \h+ <date> \h+ <commodity> \h+ <amount> \h* <comment>? }
    token header      { <date> [ \h+ <status> ]? [ \h+ <code> ]? [ \h+ <description> ]? \h* <comment>? }
    token date        { $<y>=[\d**4] <[-/]> $<m>=[\d**1..2] <[-/]> $<d>=[\d**1..2] }
    token status      { <[*!]> }
    token code        { '(' $<text>=[<-[)\n]>*] ')' }
    token description { [ <!before \h* ';'> \N ]+ }
    token comment     { ';' \h? $<text>=[\N*] }

    token posting-line { \h+ [ <posting> || <comment> ] \h* \n }
    token posting      { [ <status> \h+ ]? <account> [ [ \t || \h \h+ ] \h* <amounts> ]? \h* <comment>? }
    # An account name may hold single spaces; two, or a tab, end it.
    token account      { <part>+ % ':' }
    token part         { <-[:;\s]>+ [ ' ' <-[:;\s]>+ ]* }
    token amounts      { <amount> [ \h* <price> ]? [ \h* <assertion> ]? }
    token price        { $<kind>=['@@' || '@'] \h* <amount> }
    token assertion    { '=' \h* <amount> }

    # $12.50  $-12.50  -$12.50  12.50 EUR  €312.00  6.2125 ACME  1,850.00
    token amount {
        $<neg>=['-']?
        [ <commodity> $<sp>=[\h*] $<neg2>=['-']? <quantity>
        || <quantity> $<sp>=[\h*] <commodity>
        || <quantity> ]
    }
    token quantity  { \d+ [ ',' \d\d\d ]* [ '.' \d* ]? || '.' \d+ }
    token commodity { <[$€£¥]> || <:L>+ || '"' $<name>=[<-["\n]>+] '"' }
}

#| The actions: each entry becomes part of a Journal as it is read.
class Reader {
    has $.journal;
    has @.starts;           # the offset at which each line begins

    method line(Int $pos) {
        my ($lo, $hi) = 0, @!starts.end;
        while $lo < $hi {
            my $mid = ($lo + $hi + 1) div 2;
            if @!starts[$mid] <= $pos { $lo = $mid } else { $hi = $mid - 1 }
        }
        $lo + 1
    }

    method entry:sym<transaction>($/) {
        my %h = $<header>.made;
        unless %h<date> {
            $!journal.problem(self.line($/.from), "there is no such date as {$<header><date>}");
            return;
        }
        my @postings = $<posting-line>.map(*.made).grep(Posting);
        $!journal.transactions.push:
            Transaction.new(|%h, line => self.line($/.from), :@postings);
    }
    method entry:sym<periodic>($/) {
        if ~$<period> ne 'monthly' {
            $!journal.problem(self.line($/.from), "only `~ monthly` budgets are read, not `~ $<period>`");
            return;
        }
        $!journal.budget.append: $<posting-line>.map(*.made).grep({ $_ && .amount });
    }
    method entry:sym<price>($/) {
        my $m = $<price-line>;
        $!journal.prices.push: %( date => $m<date>.made, commodity => $m<commodity>.made,
                                  price => $m<amount>.made );
    }
    method entry:sym<account>($/) { $!journal.declared.push: ~$<account> }
    method unreadable($/) { $!journal.unreadable(~$/, self.line($/.from)) }

    method header($/) {
        make %(
            date    => $<date>.made,
            status  => $<status> ?? ~$<status> !! '',
            code    => $<code> ?? ~$<code><text> !! Str,
            payee   => $<description> ?? $<description>.Str.trim !! '',
            comment => $<comment> ?? $<comment><text>.Str.trim !! Str,
        )
    }
    method date($/) { make (try Date.new(+$<y>, +$<m>, +$<d>)) }

    method posting-line($/) { make $<posting> ?? $<posting>.made !! Nil }
    method posting($/) {
        my $a = $<amounts>;
        my %p = account => ~$<account>, line => self.line($/.from);
        %p<comment> = $<comment><text>.Str.trim if $<comment>;
        if $a {
            %p<amount> = $a<amount>.made;
            %p<assert> = $a<assertion><amount>.made if $a<assertion>;
            if $a<price> {
                %p<price> = $a<price><amount>.made;
                %p<kind>  = ~$a<price><kind>;
            }
        }
        make Posting.new(|%p);
    }

    method amount($/) {
        my $digits = $<quantity>.Str.subst(',', '', :g);
        $digits = "0$digits" if $digits.starts-with('.');
        $digits ~= '0' if $digits.ends-with('.');
        my $qty = $digits.FatRat;
        # an optional '-' that is absent still matched, as an empty string
        $qty = -$qty if ($<neg> // '') ~ ($<neg2> // '') eq '-';
        my %style =
            left  => ?($<commodity> && $<commodity>.from < $<quantity>.from),
            space => ?($<sp> && $<sp>.chars),
            prec  => $digits.contains('.') ?? $digits.split('.')[1].chars !! 0,
            group => $<quantity>.Str.contains(',');
        make Amount.new(commodity => $<commodity> ?? $<commodity>.made !! '', :$qty, :%style);
    }
    method commodity($/) { make $<name> ?? ~$<name> !! ~$/ }
}

# ---------------------------------------------------------------- journal ---

class Journal is export {
    has Str         $.file = '-';
    has Transaction @.transactions;
    has             @.prices;            # date, commodity, price: one unit's worth
    has Posting     @.budget;            # the ~ monthly postings
    has Str         @.declared;          # `account` directives
    has             @.problems;          # line, message, context
    has             %.styles;            # commodity => how to write it
    has             %.uses;              # commodity => how many postings
    has Int         $.assertions = 0;

    method from-file(Str $path) { self.from-text($path.IO.slurp, file => $path) }

    method from-text(Str $text is copy, Str :$file = '-') {
        $text ~= "\n" unless $text.ends-with("\n");
        my $j = self.new(:$file);
        my @starts = 0, |$text.indices("\n").map(* + 1);
        JournalGrammar.parse($text, actions => Reader.new(journal => $j, :@starts));
        $j.finish;
        $j
    }

    method problem(Int $line, Str $message, :@context) {
        @!problems.push: %( :$line, :$message, :@context );
    }

    #| An entry the grammar could not read: find the line in it that fails,
    #| and how far into that line the grammar got, and point there.
    method unreadable(Str $block, Int $first) {
        my @lines = $block.lines;
        for @lines.kv -> $i, $l {
            next unless $l.trim;
            my ($rule, $text, $indent) = $l ~~ /^\h/  ?? ('posting', $l.trim-leading, $l.chars - $l.trim-leading.chars)
                                      !! $l ~~ /^ 'P' \h/ ?? ('price-line', $l, 0)
                                      !!                     ('header', $l, 0);
            my $m = JournalGrammar.subparse($text, :$rule);
            my $got = $m ?? $m.to !! 0;
            next if $m && $text.substr($got).trim eq '';
            my $col = $indent + $got;
            my $why = $rule eq 'header' && !$m ?? 'a transaction starts with a date, as 2026-01-31'
                   !! $rule eq 'posting' && $text.substr($got).trim.starts-with('=')
                                                  ?? 'an assertion needs an amount before it; a balance assignment is not read'
                   !! $rule eq 'posting' && $text.substr($got, 1) eq ','
                                                  ?? "this amount cannot be read: a decimal point is '.', and ',' only groups thousands"
                   !! $rule eq 'posting' && $m    ?? 'this amount cannot be read'
                   !! 'this cannot be read';
            self.problem($first + $i, $why, context => [$l, ' ' x $col ~ '^']);
            return;
        }
        self.problem($first, 'a transaction needs at least one posting, indented under it',
                     context => [@lines[0]]);
    }

    method !saw(Amount $a) {
        my $s = %!styles{$a.commodity} //= %( |$a.style );
        $s<prec>  max= $a.style<prec>;
        $s<group> ||= $a.style<group>;
        %!uses{$a.commodity}++;
    }

    #| After reading: infer the missing amounts, check that every
    #| transaction balances and every assertion holds, and gather the prices
    #| the transactions imply.
    method finish() {
        # how each commodity is written, before anything is written back
        for @!transactions.map(*.postings.Slip) -> $p {
            self!saw($p.amount) if $p.amount;
            self!saw($p.assert) if $p.assert;
        }

        # A transaction whose amounts cannot all be known is reported and
        # left out, so that the reports are made of whole transactions only.
        my @kept;
        for @!transactions -> $t {
            my @swallowed = $t.postings.grep({ .account ~~ / ' ' <[$€£¥-]>? \d / });
            for @swallowed -> $p {
                self.problem($p.line, "the account name `{$p.account}` has swallowed its amount: "
                    ~ 'two spaces or a tab must separate them');
            }
            next if @swallowed;
            my @blank = $t.postings.grep({ !.amount });
            my $sum = Balance.new;
            $sum.add(.weight) for $t.postings.grep(*.amount);
            if @blank > 1 {
                self.problem($t.line, "{+@blank} postings have no amount; only one can be inferred");
                next;
            }
            elsif @blank {
                my @owed = $sum.neg.amounts;
                @owed = Amount.new(qty => 0.FatRat) unless @owed;
                my $p = @blank[0];
                $p.amount = @owed.shift;
                $p.inferred = True;
                # owing several commodities, the posting becomes one per commodity
                $t.postings.push: Posting.new(account => $p.account, amount => $_,
                                              inferred => True, line => $p.line) for @owed;
            }
            elsif !$sum.is-zero {
                # the whole residue, however small: rounded, $0.0044 would say $0.00
                self.problem($t.line, "this transaction does not balance: it is off by "
                    ~ $sum.amounts.map({ self.fmt($_, prec => max(decimals(.qty), self.prec(.commodity))) }).join(', '));
            }
            @kept.push: $t;
            for $t.postings -> $p {
                $p.txn = $t;
                next unless $p.price;
                @!prices.push: %( date => $t.date, commodity => $p.amount.commodity,
                    price => Amount.new(commodity => $p.cost.commodity,
                                        qty => ($p.cost.qty / $p.amount.qty).abs) );
            }
        }
        @!transactions = @kept;
        @!prices = @!prices.sort({ $^a<date> cmp $^b<date> });

        # Assertions are checked in date order, as a bank statement would be.
        my %held;
        for self.ordered -> $t {
            for $t.postings -> $p {
                (%held{$p.account} //= Balance.new).add($p.amount);
                my $want = $p.assert or next;
                $!assertions++;
                my $have = %held{$p.account}.qty($want.commodity);
                next if $have == $want.qty;
                self.problem($p.line, "balance assertion failed: {$p.account} holds "
                    ~ "{self.fmt(Amount.new(commodity => $want.commodity, qty => $have))}, "
                    ~ "not {self.fmt($want)}");
            }
        }
    }

    #| Transactions by date; on the same date, in the order written.
    method ordered() { @!transactions.sort({ $^a.date cmp $^b.date || $^a.line <=> $^b.line }) }

    #| The postings a report covers. Each query word matches part of an
    #| account name, ignoring case; `not:word` excludes, and `@word` matches
    #| the payee instead.
    method postings(:$begin, :$end, :@query) {
        my @acct  = @query.grep({ !.starts-with('not:') && !.starts-with('@') })».fc;
        my @not   = @query.grep(*.starts-with('not:'))».substr(4)».fc;
        my @payee = @query.grep(*.starts-with('@'))».substr(1)».fc;
        gather for self.ordered -> $t {
            next if $begin && $t.date < $begin;
            next if $end && $t.date >= $end;
            next if @payee && !@payee.grep({ $t.payee.fc.contains($_) });
            for $t.postings -> $p {
                my $name = $p.account.fc;
                next if @acct && !@acct.grep({ $name.contains($_) });
                next if @not.grep({ $name.contains($_) });
                take $p;
            }
        }
    }

    #| The commodity most postings are in: what --value and the monthly and
    #| budget reports count in.
    method home() { %!uses.sort({ $^b.value <=> $^a.value || $^a.key cmp $^b.key }).first.key // '' }

    method first-date() { self.ordered.first.date }
    method last-date()  { (|@!transactions.map(*.date), |@!prices.map(*<date>)).max }

    #| One unit of $c, in $in, by the latest price on or before $on: a `P`
    #| line or a price a transaction paid, either way round.
    method rate(Str $c, Str $in, Date $on) {
        return 1.FatRat if $c eq $in;
        my $rate;
        for @!prices -> %p {
            last if %p<date> > $on;
            if %p<commodity> eq $c && %p<price>.commodity eq $in { $rate = %p<price>.qty }
            elsif %p<commodity> eq $in && %p<price>.commodity eq $c { $rate = 1 / %p<price>.qty }
        }
        $rate
    }

    #| A balance at market value: each commodity that has a price in $in is
    #| converted, and one that has none is left as it is.
    method value(Balance $b, Date $on, Str $in = self.home) {
        my $out = Balance.new;
        for $b.amounts -> $a {
            my $r = self.rate($a.commodity, $in, $on);
            $out.add($r.defined ?? Amount.new(commodity => $in, qty => $a.qty * $r) !! $a);
        }
        $out
    }

    # ------------------------------------------------------------ writing ---

    method prec(Str $c) { %!styles{$c}<prec> // 2 }

    #| The one place a quantity is rounded: to the most decimal places the
    #| journal ever wrote that commodity with, half away from zero.
    method fmt(Amount $a, Int :$prec) {
        my %s = %!styles{$a.commodity} // %( left => False, space => True, prec => 2, group => False );
        my $p = $prec // %s<prec>;
        my $scaled = $a.qty.abs * 10 ** $p;
        my $n = $scaled.floor + ($scaled - $scaled.floor >= 1/2 ?? 1 !! 0);
        my $digits = ~($n div 10 ** $p);
        $digits = $digits.flip.comb(3).join(',').flip if %s<group>;
        $digits ~= '.' ~ ($n mod 10 ** $p).fmt('%0' ~ $p ~ 'd') if $p;
        $digits = "-$digits" if $n && $a.qty < 0;
        return $digits unless $a.commodity;
        my $c = $a.commodity ~~ /^ <:L>+ $/ || $a.commodity.chars == 1 ?? $a.commodity !! "\"{$a.commodity}\"";
        my $sp = %s<space> ?? ' ' !! '';
        %s<left> ?? "$c$sp$digits" !! "$digits$sp$c"
    }

    #| A balance as one line per commodity; nothing at all is written "0".
    method fmt-balance(Balance $b) {
        $b.is-zero ?? ('0',) !! $b.amounts.map({ self.fmt($_) }).List
    }
}
