# Native bodies that decline at entry: the interpreter runs the routine
# (t/aot/run.raku).
#aot-run-env: RAKUPP_AOT_FAULT_DECLINE=plain,typed,by-reference,meth
#aot-expect: declined plain
#aot-expect: declined typed
#aot-expect: declined by-reference
#aot-expect: declined meth
use Aot::Decline;

say plain(1, 2);
say plain('a', 'b');
say typed('ab');
say typed('x', :times(4));
say mapped(<p q>);
say fine(1);
say Obj.new(v => 'w').meth('?');
say trace();
