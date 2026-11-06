# ledger — plain-text accounting, where no amount is ever rounded

Money is the one place where everybody agrees what the right answer is, to
the cent, and binary floating point cannot give it: 0.10 has no exact double,
so neither does any sum of prices. Accounting programs work around that with
integer cents, or decimal libraries, or a tolerance on every comparison.
Raku does not need to. `"12.50".FatRat` is the fraction 25/2, the sum of two
fractions is a fraction, and nothing is lost until a number is written out.

This is a reader for the journal format of John Wiegley's
[ledger](https://ledger-cli.org) and of [hledger](https://hledger.org) —
a text file of dated transactions, each of whose postings must sum to zero —
and the reports that make it useful: balances, a register, month by month,
a budget, and a check that finds the mistakes and says where they are.

```sh
build/rakupp showcase/ledger/ledger.raku balance                 # what every account holds
build/rakupp showcase/ledger/ledger.raku balance --depth=2
build/rakupp showcase/ledger/ledger.raku balance Assets --value  # at the latest prices
build/rakupp showcase/ledger/ledger.raku register Dining -p=2026-02
build/rakupp showcase/ledger/ledger.raku monthly                 # income, spending, savings rate
build/rakupp showcase/ledger/ledger.raku monthly Groceries
build/rakupp showcase/ledger/ledger.raku budget                  # the `~ monthly` budget vs. what was spent
build/rakupp showcase/ledger/ledger.raku print @Osteria          # transactions, written out again
build/rakupp showcase/ledger/ledger.raku check -f=showcase/ledger/examples/broken.journal
build/rakupp showcase/ledger/ledger.raku float                   # what doubles would have made of it
build/rakupp showcase/ledger/ledger.raku --check
```

Without `-f` it reads [`examples/household.journal`](examples/household.journal)
— six months of a fictional household, 145 transactions — or `$LEDGER_FILE`
when that is set. A query word picks accounts by part of their name, ignoring
case; `not:word` leaves some out and `@word` picks payees. `-b`, `-e` and
`-p=2026-03` narrow the dates, `-B` reports at cost and `-V` at market value.

## The journal

```
2026-02-14 * Osteria Lume  ; dinner for three; I take the odd cent
    Expenses:Food:Dining                     $33.34
    Assets:Receivable:Sam                    $33.33
    Assets:Receivable:Alex                   $33.33
    Liabilities:CreditCard                 $-100.00
```

A date, a payee, and postings: each moves an amount into an account, or out
of it when negative. The postings of a transaction sum to zero — that is
double entry, and it means money is only ever moved, never made or lost. One
posting may leave its amount out; it is inferred as whatever makes the sum
zero. The rest of the format this reads:

| Written | Means |
|---|---|
| `6.2125 ACME @@ $300.00` | six and a bit shares, $300.00 for the lot |
| `€312.00 @ $1.0837` | euros, at $1.0837 each |
| `$2,570.24 = $4,809.39` | and afterwards the account holds exactly $4,809.39 |
| `P 2026-06-30 ACME $53.48` | a market price, for `--value` |
| `~ monthly` + postings | the budget, for `budget` |
| `account …`, `commodity …`, `; # % \| *` | declarations and comments, read and passed over |

## What it prints

```
$ build/rakupp showcase/ledger/ledger.raku balance Assets --value
  $50,772.58  Assets
   $1,941.35    Brokerage
      $46.08    Cash:EUR
  $12,400.97    Checking
  $21,100.00    Retirement:401k
  $15,284.18    Savings
------------
  $50,772.58
```

The 36.3004 shares in the brokerage account are worth 36.3004 × $53.48 =
$1,941.345392 at June's price, exactly — and printed to the cent, because the
journal writes dollars with two decimals. The €42.30 left in the wallet after
Lisbon is $46.08 at June's rate. Without `--value` the same accounts show
their shares and euros as shares and euros.

```
$ build/rakupp showcase/ledger/ledger.raku budget
The monthly budget against what was spent, 2026-01 to 2026-06 (6 months)

                                spent        budget   used
Expenses:Food:Groceries     $2,560.92     $2,700.00    95%  ███████████████████
Expenses:Food:Dining          $776.18       $900.00    86%  █████████████████▎
Expenses:Transport            $633.45       $840.00    75%  ███████████████▏
Expenses:Fun                  $338.83       $480.00    71%  ██████████████▏
Expenses:Subscriptions        $110.88       $120.00    92%  ██████████████████▌
----------------------------------------------------------
                            $4,420.26     $5,040.00    88%
```

`monthly` is the same idea a month at a time: income, spending, what was
left, and the savings rate as a bar. `register` lists postings with a running
total, `print` writes transactions back out in the journal's own format, and
`accounts` and `payees` count what is there.

## When it is wrong

[`examples/broken.journal`](examples/broken.journal) has six mistakes in it,
the kinds that creep into a file typed by hand. `check` finds all six in one
pass, because an entry the grammar cannot read does not stop it:

```
$ build/rakupp showcase/ledger/ledger.raku check -f=showcase/ledger/examples/broken.journal
showcase/ledger/examples/broken.journal:10: this transaction does not balance: it is off by $-0.09
showcase/ledger/examples/broken.journal:15: the account name `Expenses:Health:Pharmacy $23.18` has swallowed its amount: two spaces or a tab must separate them
showcase/ledger/examples/broken.journal:20: this amount cannot be read: a decimal point is '.', and ',' only groups thousands
        Liabilities:CreditCard            $-96,22
                                              ^
showcase/ledger/examples/broken.journal:23: balance assertion failed: Assets:Checking holds $3,570.24, not $3,570.42
showcase/ledger/examples/broken.journal:26: there is no such date as 2026-02-30
showcase/ledger/examples/broken.journal:30: 2 postings have no amount; only one can be inferred
6 problems in showcase/ledger/examples/broken.journal
```

The second is the format's classic trap. One space where two belong makes
the amount part of the account's name, and then the transaction balances by
inferring it, so this case gets its own check. The other reports still run
on a file like this. They leave out what could not be read, and say so first.

A residue is reported in full, however small. `€312.00 @ $1.0837` costs
$338.1144, so a card line of `$-338.11` leaves it off by $0.0044. That is
what this says, where a rounded message would say "$0.00". A transaction
balances when its sum is exactly zero. No tolerance is needed, because no
sum here was ever rounded.

## Why exact

`float` reads the same journal and does the same sums in the same order with
`Num` instead of `FatRat`, as a spreadsheet would, and counts the damage:

```
$ build/rakupp showcase/ledger/ledger.raku float
...
  transactions that no longer sum to zero       0 of 145
      each one alone is short enough to come out even
  balance assertions that fail                 18 of 24
      2026-01-15 Assets:Checking          holds 4809.389999999999, not 4809.39
      2026-01-31 Assets:Checking          holds 5582.269999999999, not 5582.27
      2026-02-15 Assets:Checking          holds 6188.6399999999985, not 6188.64
  prices paid with @@ that do not multiply      2 of 9
  back to what was paid
      2026-04-07 €312.00 @@ $338.11: the unit price times 312 is 338.10999999999996
      2026-04-09 €41.50 @@ $45.21: the unit price times 41.5 is 45.21000000000001

And the running totals drift:
  Assets:Checking          exactly  $12,400.97    in doubles 12400.969999999998
  Assets:Savings           exactly  $15,284.18    in doubles 15284.18
  Liabilities:CreditCard   exactly     $-56.94    in doubles -56.94000000000034
```

Eighteen of the twenty-four bank-statement figures fail an `==` in doubles.
In FatRats all twenty-four hold. The hotel in Lisbon cost €312.00 for
$338.11, so a euro cost 33811/31200 dollars — a repeating decimal that no
double can hold, and that a FatRat holds exactly, so that 312 of them come
back to $338.11.

Why `FatRat` and not `Rat`: a `Rat` whose denominator outgrows 64 bits turns
into a `Num`, quietly. Money rarely gets there, but a unit price like the one
above, multiplied by another, can. A `FatRat` never turns.

## How it works

| File | What it is |
|---|---|
| [`lib/Ledger.rakumod`](lib/Ledger.rakumod) | the engine: the grammar, the journal, prices and formatting — 440 lines, no dependencies |
| [`ledger.raku`](ledger.raku) | the terminal: the reports, `float`, `--check` |
| [`examples/household.journal`](examples/household.journal) | six months, three commodities, 24 balance assertions |
| [`examples/broken.journal`](examples/broken.journal) | six mistakes, for `check` to find |
| [`tools/household.raku`](tools/household.raku) | wrote `household.journal`, assertions included |

**The grammar** reads a whole file as `[ <entry> || <unreadable> ]*`. An entry
is a transaction, a price, a budget, a declaration, a comment or a blank
line. Anything else, from its first line to the next line that is not
indented, is `<unreadable>`. Its actions then find the first line in it that
fails and run that line's own rule with `subparse`, which says how far it
got. That is where the `^` goes. A transaction also refuses to end on an
indented line, so a bad posting makes its whole transaction unreadable rather
than quietly ending it one posting short.

**The actions** build the journal as the grammar reads it. Each amount
remembers how it was written: which side the symbol is on, whether there is a
space, how many decimals, whether thousands are grouped. Every commodity is
written back out that way, at the most decimals the journal ever used for it.
Then `finish` infers the missing amounts, checks that each transaction sums
to zero, records the price each `@` and `@@` implies, and replays the postings
in date order to check the assertions.

**Formatting** is the only place a quantity is rounded, half away from zero,
in `Journal.fmt`. Nothing computed from a rounded value is ever used again.

## Is it right?

`--check` runs twenty checks against the bundled journals:

```
$ build/rakupp showcase/ledger/ledger.raku --check
all 20 checks passed
```

The figures behind the assertions are not the engine's own.
[`tools/household.raku`](tools/household.raku) wrote the journal and worked
out every balance assertion as it went, in integer cents, with no code in
common with `lib/Ledger.rakumod`. So all 24 holding means two different pieces
of arithmetic agree. Beyond that, the reports must agree with each other:

- At cost, all accounts together hold exactly nothing, and every transaction
  sums to exactly zero.
- Every quantity is still a `FatRat` at the end. None has turned into a `Num`.
- Each account's balance is the end of its running total in `register`. Each
  `--depth=2` total is the sum of the accounts beneath it.
- `print`'s output reads back in as the same transactions, posting for
  posting.
- What `monthly` says was saved is exactly what the assets and liabilities
  grew by. `budget`'s Transport, Transit and Rides together, is the balance
  report's.
- The pencil answers come out right. The dinner splits as $33.34 + $33.33 +
  $33.33. The wallet comes home with €42.30. A euro at the hotel cost exactly
  33811/31200 dollars, and 312 of them make $338.11 again. The shares are
  worth exactly $1,941.345392.
- `broken.journal`'s six problems are found on their own six lines.

The program runs unchanged on Rakudo, and every report above prints the same
thing byte for byte on both engines.

## What it leaves out

ledger's format is large, and this reads its core. Not read: `include`, lot
prices and dates (`{$41.20}`), virtual postings (`(Account)`, `[Account]`),
automated transactions (`= expr`), periodic transactions other than
`~ monthly`, balance assignments (`= $X` with no amount), `=*` assertions that
include subaccounts, and decimal commas. Queries are plain substrings, not
regular expressions. `--value` converts with a direct price only, either way
round, and not through a chain of them.

## As one binary

```sh
build/rakupp --exe showcase/ledger/ledger.raku -o ledger
./ledger balance -f=showcase/ledger/examples/household.journal
```

It compiles to native code, with the engine module embedded, and prints what
the interpreter prints. Run from anywhere else, the binary needs `-f` or
`$LEDGER_FILE`, because the bundled journal is found next to the source.
