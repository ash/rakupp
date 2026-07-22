# Regression: --highlight treated #`( ... ) as a line comment, so the
# continuation lines of a multi-line embedded comment were highlighted
# as code. All bracket forms (#`( #`[ #`{ #`<) must be consumed as one
# balanced, nestable group and emitted as a single comment span.
# Reported 2026-07-22 (user, multi-line #`( ... ) example).

my $src = q:to/END/;
my $x; #`( first
second ( nested ) third )
$x = 1; #`{ curly
form }
say $x;
END

my $tmp = 'temp_hl_' ~ $*PID ~ '.raku';
spurt $tmp, $src;
my $html = qqx{$*EXECUTABLE --highlight $tmp};
unlink $tmp;

my $ok = True;

# the whole paren comment (including its second line) is one cm span
unless $html.contains('<span class="cm">#`( first' ~ "\n" ~ 'second ( nested ) third )</span>') {
    say 'FAIL: paren embedded comment not one multi-line span';
    $ok = False;
}

# same for the curly form
unless $html.contains('<span class="cm">#`{ curly' ~ "\n" ~ 'form }</span>') {
    say 'FAIL: curly embedded comment not one multi-line span';
    $ok = False;
}

# continuation-line text must not leak out as code tokens
if $html.contains('<span class="nb">second</span>') or $html.contains('<span class="n">form</span>') {
    say 'FAIL: comment continuation line highlighted as code';
    $ok = False;
}

say 'PASS' if $ok;
