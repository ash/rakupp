# rakusheet.raku — evaluates one batch of spreadsheet formulas.
#
# The host (the Excel add-in or the Google Sheets script) builds one program:
# the workbook's own definitions (the "Raku" sheet, column A) first, so that
# a line number in an error is the row it came from, then this file. The
# batch arrives on standard input as JSON:
#
#     {"calls": [{"code": "$^a * 2", "args": [21]}, ...]}
#
# and the answer is one line on standard output, after a record separator so
# that whatever a formula prints itself cannot be mistaken for it:
#
#     \x1E[{"ok": [[42]]}, {"err": "...", "kind": "value"}, ...]
#
# Every "ok" is a matrix (a list of rows), because that is what a cell range
# takes; the host unwraps a 1x1 matrix for a single cell.

use MONKEY-SEE-NO-EVAL;

# The code of a formula is the body of an anonymous sub: $^a, $^b, ... are
# its arguments in order and @_ is all of them, ranges flattened. Each
# distinct code is compiled once per batch.
my %rakusheet-compiled;

sub rakusheet-compile(Str $code) {
    %rakusheet-compiled{$code} //= do {
        # Raku++ 5.2 flattens an implicit @_ only one level, where Rakudo
        # flattens it as *@_ does; spelling the slurpy out gives both engines
        # the same @_. A placeholder forbids a signature, so only without one.
        my $slurpy = $code.contains('@_') && $code !~~ / <[$@%&]> <[^:]> <.alpha> /;
        EVAL $slurpy ?? "sub (*\@_) \{ $code \}" !! "sub \{ $code \}"
    }
}

# A range arrives as nested JSON arrays. Lists rather than Arrays, so that
# @_ flattens a range into its cells instead of counting its rows.
sub rakusheet-arg($v) {
    $v ~~ Positional ?? $v.map(&rakusheet-arg).List !! $v
}

# One cell's worth of a result. A spreadsheet holds a double, a string or a
# boolean: an exact rational becomes the nearest double here, once, and an
# integer a double cannot hold becomes its digits.
sub rakusheet-cell($v) {
    given $v {
        when Failure  { .exception.throw }
        when !.defined { Any }
        when Bool     { ?$v }
        when Int      { $v.abs < 2 ** 53 ?? $v !! ~$v }
        when Rational {
            die "#DIV/0: the result divides by zero" if $v.denominator == 0;
            rakusheet-cell($v.Num)
        }
        when Num {
            die "#NUM: the result is $v" if $v.isNaN || $v == Inf | -Inf;
            $v
        }
        when Str      { $v }
        when Callable { die "the formula returned code, not a value (is a * left over?)" }
        when Match    { ~$v }
        default       { .Str }
    }
}

sub rakusheet-is-list($v) {
    $v.defined && $v !~~ Str && $v !~~ Match && ($v ~~ Positional || $v ~~ Seq)
}

# The whole result as rows. A flat list goes down one column; a list of
# lists is a table, padded so that every row has the same width; a hash is
# two columns of keys and values.
sub rakusheet-matrix($r) {
    if rakusheet-is-list($r) {
        die "the result is a lazy list; keep part of it with .head(N)" if $r.is-lazy;
        my @items = $r.list;
        return [["",],] unless @items;
        if @items.first(&rakusheet-is-list).defined {
            my @rows = @items.map: { rakusheet-is-list($_) ?? .list.map(&rakusheet-cell).eager.Array !! [rakusheet-cell($_)] };
            my $width = @rows.map(*.elems).max;
            return @rows.map({ [|$_, |("" xx ($width - .elems))] }).eager.Array;
        }
        return @items.map({ [rakusheet-cell($_)] }).eager.Array;
    }
    if $r ~~ Pair {
        return [[rakusheet-cell($r.key), rakusheet-cell($r.value)],];
    }
    if $r.defined && $r ~~ Associative {
        return $r.sort(*.key).map({ [rakusheet-cell(.key), rakusheet-cell(.value)] }).eager.Array;
    }
    [[rakusheet-cell($r)],]
}

# An exception's message, or its type's name when it has none to give.
sub rakusheet-text($e) {
    ((try $e.message) // $e.^name).Str
}

sub rakusheet-kind($e) {
    my $m = rakusheet-text($e);
    return 'div0' if $e ~~ X::Numeric::DivideByZero || $m.starts-with('#DIV/0');
    return 'name' if $e ~~ X::Undeclared | X::Undeclared::Symbols;
    return 'num'  if $m.starts-with('#NUM');
    'value'
}

sub rakusheet-message($e) {
    my $m = rakusheet-text($e).subst(/^ '===SORRY!===' \N* \n/, '').lines.map(*.trim).grep(*.chars).join(' ');
    $m.chars > 250 ?? $m.substr(0, 247) ~ '...' !! $m
}

sub rakusheet-call(%call) {
    CATCH { default { return %( err => rakusheet-message($_), kind => rakusheet-kind($_) ) } }
    my &formula = rakusheet-compile(%call<code> // '');
    my $result = formula(|(%call<args> // []).map(&rakusheet-arg));
    %( ok => rakusheet-matrix($result) )
}

sub rakusheet-run(Str $request) {
    use Data::Native <json>;   # the engine's own JSON, answered with nothing installed
    my %request = from-json($request);
    # A loop, not a map: Raku.js 5.2.0's to-json does not reify an Array
    # that a map over a sub with a CATCH filled lazily, and writes [].
    my @answers;
    for (%request<calls> // []).list -> %call { @answers.push: rakusheet-call(%call) }
    put "\x1E" ~ to-json(@answers, :!pretty);
}

rakusheet-run($*IN.slurp);
