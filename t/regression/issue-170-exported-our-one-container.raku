# Issue #170: a module's `our $X is export` had two containers — the one
# the module's own code and the importer's bare `$X` share, and a separate
# `$M::X`. A write through either name was invisible through the other, so
# `set-x(42); say $M::X` said Nil and `$M::X = 7; say get-x()` said Nil.
# The initialised form published a COPY under the qualified name; the bare
# `our $X is export;` published nothing, and `$M::X` was whatever its first
# writer autovivified. Both now install a view onto the one container.
#
# Contract: exit 0 + last line PASS. Every case is oracle-checked against
# Rakudo 2026.08 — run this file under `rakudo` too.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}

my $dir = $*TMPDIR.add("issue170-{$*PID}");
$dir.mkdir;
my %modules =
    I170Init    => q:to/END/,
        unit module I170Init;
        our $X is export = Nil;
        sub set-x($v) is export { $X = $v }
        sub get-x is export { $X }
        END
    I170Bare    => q:to/END/,
        unit module I170Bare;
        our $X is export;
        sub set-x($v) is export { $X = $v }
        sub get-x is export { $X }
        END
    I170Braced  => q:to/END/,
        module I170Braced {
            our $X is export;
            sub set-x($v) is export { $X = $v }
            sub get-x is export { $X }
        }
        END
    I170Class   => q:to/END/,
        unit class I170Class;
        our $X is export = Nil;
        sub set-x($v) is export { $X = $v }
        sub get-x is export { $X }
        END
    ;
for %modules.kv -> $name, $src { $dir.add("$name.rakumod").spurt: $src }

for %modules.keys.sort -> $name {
    my $code = qq:to/END/;
        use $name;
        set-x(42); print \$$name\::X // 'Nil', ' ', get-x() // 'Nil';
        \$$name\::X = 7; print ' ', \$$name\::X // 'Nil', ' ', get-x() // 'Nil', ' ', \$X;
        \$X = 9; say ' ', get-x(), ' ', \$$name\::X;
        END
    for <cold cached> -> $run {
        my $p = run($*EXECUTABLE, '-I', ~$dir, '-e', $code, :out, :err);
        my $out = $p.out.slurp(:close).trim;
        my $err = $p.err.slurp(:close);
        check $out, '42 42 7 7 7 9 9',
            "$name, $run run: one container under every name{$err ?? " ($err.lines.head())" !! ''}";
    }
}

# the aggregate sigils: the qualified name writes the module's array and hash
$dir.add('I170Agg.rakumod').spurt: q:to/END/;
    unit module I170Agg;
    our @A is export = 1, 2;
    our %H is export;
    sub get-a is export { @A.join(',') }
    sub get-h is export { %H.sort.map({ "{.key}={.value}" }).join(',') }
    END
{
    my $p = run($*EXECUTABLE, '-I', ~$dir, '-e',
        'use I170Agg; @I170Agg::A.push(3); %I170Agg::H<k> = 1; say get-a(), " ", get-h(), " ", @A.elems, " ", %H<k>',
        :out, :err);
    check $p.out.slurp(:close).trim, '1,2,3 k=1 3 1', 'exported @ and % are one container too';
    $p.err.slurp(:close);
}

# an `our` that is not exported was already one container; it stays so
$dir.add('I170Plain.rakumod').spurt: q:to/END/;
    unit module I170Plain;
    our $X = 1;
    sub get-x is export { $X }
    END
{
    my $p = run($*EXECUTABLE, '-I', ~$dir, '-e', 'use I170Plain; $I170Plain::X = 5; say get-x()', :out, :err);
    check $p.out.slurp(:close).trim, '5', 'unexported our: the qualified write reaches the module';
    $p.err.slurp(:close);
}

for $dir.dir { .d ?? run('rm', '-rf', ~$_) !! .unlink }
$dir.rmdir;

.note for @fail;
say @fail ?? "FAIL ({+@fail})" !! 'PASS';
exit @fail ?? 1 !! 0;
