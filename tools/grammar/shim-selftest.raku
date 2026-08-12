# Shim self-test BODY — grammar-smoke.raku concatenates the shim in front and
# runs the result under plain rakupp. Everything here is engine-side, so this
# leg needs no Python and no shared library: it pins the shim's own contract,
# and the host drivers then only have to prove the ABI carries it faithfully.

my $ok = 0;
my $bad = 0;
sub check(Bool() $cond, $desc) {
    if $cond { $ok++;  say "ok - $desc" }
    else     { $bad++; say "NOT OK - $desc" }
}
sub dies(&code) {
    my $died = True;
    try { code(); $died = False }
    $died
}

my $src = q:to/END/;
grammar Cfg {
    rule TOP    { <entry>+ }
    token entry { <key> '=' <value> \n? }
    token key   { \w+ }
    token value { \N+ }
}
class CfgActions {
    method value($/) { make ~$/ ~ '!' }
}
END

# compile + cache
my $id = rk-grammar-compile($src, 'Cfg', 'CfgActions');
check $id >= 0, 'compile returns an id';
check rk-grammar-compile($src, 'Cfg', 'CfgActions') == $id, 'same source+names hit the cache';
check rk-grammar-compile($src, 'Cfg', '') != $id, 'different actions choice is a different slot';

# parse + walk
my $m = rk-grammar-parse($id, "a=1\nb=two\n", '');
check $m.defined, 'a good input parses';
check rk-match-walk($m, ['entry', 1, 'key'], 'str') eq 'b', 'str walks named + index + named';
check rk-match-walk($m, ['entry'], 'elems') == 2, 'elems counts a quantified capture';
check rk-match-walk($m, ['entry'], 'islist') === True, 'a quantified capture is a list';
check rk-match-walk($m, ['entry', 0], 'islist') === False, 'a single node is not';
check rk-match-walk($m, ['entry', 0, 'value'], 'made') eq '1!', 'made reaches same-file actions';
check rk-match-walk($m, ['entry', 0, 'value'], 'str') eq '1', 'str is the text, not the made';
check rk-match-walk($m, ['nope'], 'bool') === False, 'bool on a missing capture is False';
check rk-match-walk($m, ['nope'], 'elems') == 0, 'elems on a missing capture is 0';
check rk-match-walk($m, ['nope', 3, 'deeper'], 'bool') === False, 'a walk survives missing middles';
check dies({ rk-match-walk($m, ['nope'], 'str') }), 'str on a missing capture dies';
check dies({ rk-match-walk($m, [], 'wat') }), 'an unknown op dies';

# tree shape: leaf = text, node = hash, quantified = list
my $tree = rk-match-tree($m);
check $tree<entry>[1]<value> eq 'two', 'tree nests named under list under named';

# rule=, failed parse, bad id
my $e = rk-grammar-parse($id, "k=v\n", 'entry');
check $e.defined && rk-match-walk($e, ['key'], 'str') eq 'k', ':rule parses a fragment';
check !rk-grammar-parse($id, '=== not config ===', '').defined, 'a failed parse is undefined';
check dies({ rk-grammar-parse(99991, 'x', '') }), 'a bad grammar id dies';

# compile-time validation
check dies({ rk-grammar-compile('class C { }', '', '') }),
      'a type with no parse method is refused';
check dies({ rk-grammar-compile($src, '', 'CfgActions') }),
      'actions without a grammar name is refused';
check dies({ rk-grammar-compile('grammar G {', 'G', '') }),
      'broken grammar source fails at compile, not at parse';

# last-statement mode: no name needed when the grammar comes last
my $anon = rk-grammar-compile(q[grammar Last { token TOP { \d+ } }], '', '');
check rk-grammar-parse($anon, '123', '').defined, 'last-statement mode works without a name';

# fresh source under the same grammar name gets its own slot — and the OLD
# handle keeps the OLD body (each compile lives in its own wrapper package;
# before that isolation, the engine's name-keyed registry silently rebound
# every earlier handle to the newest body)
my $v2 = rk-grammar-compile($src.subst("'='", "':'"), 'Cfg', 'CfgActions');
check $v2 != $id, 'edited source is a new compile, not a cache hit';
check rk-grammar-parse($v2, "a:1\n", '').defined, 'and the new body is the one that parses';
check rk-grammar-parse($id, "a=1\n", '').defined, 'while the old handle still parses the OLD syntax';
check !rk-grammar-parse($v2, "a=1\n", '').defined, 'and the two do not bleed into each other';

# the unnamed path has no wrapper — a same-name recompile there is refused by
# the SHIM (the engine only refuses while the first declaration's scope is
# still reachable, which a sub-scoped EVAL's is not), because it would rebind
# every earlier handle to the new body
check dies({ rk-grammar-compile(q[grammar Last { token TOP { \w+ } }], '', '') }),
      'unnamed same-name recompile is refused';

# G1 diagnostics: a failed parse names where and what; a success clears it
check !rk-grammar-parse($id, "a=1\n???\n", '').defined, 'the diag input really fails';
my $diag = rk-grammar-diagnosis("a=1\n???\n");
check $diag.defined, 'a failed parse has a diagnosis';
check $diag<line> == 2, 'diagnosis line is 1-based and right';
check $diag<col> == 1, 'diagnosis column is 1-based and right';
check $diag<rule> eq 'key', 'diagnosis names the deepest failing rule';
rk-grammar-parse($id, "a=1\n", '');
check !rk-grammar-diagnosis("a=1\n").defined, 'a successful parse clears the diagnosis';

say "shim self-test: $ok ok, $bad failed";
exit 1 if $bad;
