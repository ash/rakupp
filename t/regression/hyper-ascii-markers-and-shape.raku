# Regression: two hyper-operator divergences, from a user's quadratic-formula
# program that worked with `»÷»` but not with the ASCII `>>÷>>`.
#
# 1. The Unicode operator ALIASES (÷ → /, × → *, − → -, ≥ ≤ ≠) were applied
#    only when the markers were guillemets. With ASCII markers the inner
#    reached the runtime raw and died "Unsupported operator '÷'". Both marker
#    forms now share one alias table, and the ASCII scan reads a MULTIBYTE
#    inner (it used to count bytes, capping mid-codepoint).
# 2. A hyper's result now MIRRORS the left operand's shape, as Rakudo does:
#    an Array in gives an Array (`[8 10]`), a List in gives a List (`(8 10)`).
#    rakupp always answered a List, so the gist had the wrong brackets.
# Contract: exit 0 + last line PASS.
use MONKEY-SEE-NO-EVAL;   # the marker sweep builds its expressions as text
my @fail;
my @a = (6, 8);
my @b = (2, 2);

# 1. every marker spelling × Unicode and ASCII inner operators
for '÷', '×', '−', '+' -> $op {
    my %want = '÷' => '3 4', '×' => '12 16', '−' => '4 6', '+' => '8 10';
    for '»OP»', '>>OP>>', '«OP«', '<<OP<<', '»OP«', '>>OP<<', '«OP»', '<<OP>>' -> $m {
        my $expr = $m.subst('OP', $op);
        my $got = EVAL "my \@x = (6, 8); my \@y = (2, 2); (\@x $expr \@y).join(' ')";
        @fail.push("$expr: $got") unless $got eq %want{$op};
    }
}

# a word-form inner keeps working in both marker forms
@fail.push('»div»')   unless (@a »div» @b).join(' ') eq '3 4';
@fail.push('>>div>>') unless (@a >>div>> @b).join(' ') eq '3 4';

# 2. the result mirrors the left operand's shape
my $arr = @a >>+>> @b;
@fail.push("Array in: {$arr.WHAT.^name} {$arr.gist}")
    unless $arr.WHAT.^name eq 'Array' && $arr.gist eq '[8 10]';
my $lst = (6, 8) >>+>> @b;
@fail.push("List in: {$lst.WHAT.^name} {$lst.gist}")
    unless $lst.WHAT.^name eq 'List' && $lst.gist eq '(8 10)';
my $itm = [6, 8] >>+>> @b;
@fail.push("[…] in: {$itm.WHAT.^name}") unless $itm.WHAT.^name eq 'Array';

# the user's program: a quadratic solved with custom Unicode operators, whose
# hyper division is written in ASCII markers
sub prefix:<√>($x) { $x.sqrt }
sub infix:<±>($p, $q) is equiv(&infix:<+>) { ($p + $q), ($p - $q) }
my (\qa, \qb, \qc) = 1, -4, 3;
my $D = qb² − 4 × qa × qc;
@fail.push("discriminant: $D") unless $D == 4;
my @roots = (−qb ± √$D) >>÷>> (2 × qa);
@fail.push("roots: {@roots.gist}") unless @roots.gist eq '[3 1]';

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
