# `return` from blocks across native and interpreted frames (t/aot/run.raku).
use Aot::Return;

say via-builtin([1, 20, 3]);
say via-builtin([1, 2]);
say native-through-interpreted([1, 3, 4, 5]);
say native-through-interpreted([1, 3]);
say interpreted-through-native([1, 2, 6, 7]);
say interpreted-through-native([1]);
say native-through-native([1, 2, 3]);
say native-through-native([0]);
say deep([1, 2, 3], 2);
say deep([5], 1);
say nested-loops().raku;
say inner-return();
say bare-return(True).raku;
say bare-return(False);
