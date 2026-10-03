# A routine table that does not fit its module: the binary refuses the whole
# module and runs it interpreted (t/aot/run.raku).
#aot-run-env: RAKUPP_AOT_FAULT_TABLE=Aot::TableA
#aot-expect: refused Aot::TableA
use Aot::TableA;
use Aot::TableB;

say first-a(1);
say second-a(2);
say A.new.who;
say last-a();
say b-uses-a(3);
say b-alone();
