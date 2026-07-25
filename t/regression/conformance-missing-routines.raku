# Regression: routines the documentation demonstrates that had no implementation
# at all, so every example touching them died on dispatch.
#   * Junction.new(kind, elems) — the constructor spelling of any()/all()/one().
#   * Format.new("%s|%s") — a reusable sprintf; calling it formats its arguments.
#     (Rakudo needs `use v6.e.PREVIEW` for this type; rakupp has it unguarded.)
#   * indir($path, &code) — run the block with the process directory changed,
#     restoring it however the block exits, exception included.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# Junction.new — same value as the any()/all() sub forms
check(Junction.new('any', (1, 2)).gist,    'any(1, 2)',    'junction-new-any');
check(Junction.new('all', (1, 2, 3)).gist, 'all(1, 2, 3)', 'junction-new-all');
check(Junction.new('one', (1,)).^name,     'Junction',     'junction-new-name');
check((Junction.new('any', (1, 2)) == 2).Bool, 'True',  'junction-new-autothreads');
check((Junction.new('all', (1, 2)) == 2).Bool, 'False', 'junction-new-all-autothreads');

# Format — a reusable sprintf. EVAL'd because Rakudo 2026.06 only has this type
# under `use v6.e.PREVIEW`, and this file is meant to run on both engines.
if (try EVAL 'Format.new("%s|%s")').defined {
    check((EVAL 'Format.new("%s|%s").^name'),     'Format', 'format-name');
    check((EVAL 'Format.new("%s|%s").arity'),     '2',      'format-arity-counts-directives');
    check((EVAL 'Format.new("%s|%s").Str'),       '%s|%s',  'format-str-is-the-format');
    check((EVAL 'Format.new("%s|%s")("a","b")'),  'a|b',    'format-is-callable');
    check((EVAL 'Format.new("%d%%").arity'),      '1',      'format-arity-skips-escaped-percent');
}

# indir — scoped chdir
my $before = $*CWD.Str;
my $inside = indir('/tmp', { $*CWD.Str });
check($inside.contains('tmp'), 'True', 'indir-runs-in-the-directory');
check($*CWD.Str eq $before,    'True', 'indir-restores-on-normal-exit');
# … and on an exception too
try { indir('/tmp', { die 'boom' }) };
check($*CWD.Str eq $before,    'True', 'indir-restores-on-exception');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
