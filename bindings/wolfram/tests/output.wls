(* What Raku prints, under the Wolfram binding (issue #56). Raku's output is
 * captured with rk_set_output and printed by the kernel, in the order it was
 * written, and RakuEval follows the REPL's rule: code that printed to stdout
 * returns Null, so `say 3 ** 2` shows 9 rather than 9 and True.
 *
 * tools/bindings-smoke.raku runs this and compares what it prints, stdout and
 * stderr, with the transcript it expects. By hand, from a checkout:
 *
 *   RAKUPP_LIB=build/librakupp.dylib wolframscript -file bindings/wolfram/tests/output.wls
 *)

$here = DirectoryName[
  If[StringQ[$InputFileName] && $InputFileName =!= "",
    $InputFileName, First[$ScriptCommandLine]]];
Get[FileNameJoin[{$here, "..", "RakuLang.wl"}]];

show[label_, r_] := Print[label, " -> ", ToString[r, InputForm]];

show["say", RakuEval["say 3 ** 2"]];                 (* printed: Null *)
show["value", RakuEval["3 ** 2"]];                   (* nothing printed: the value *)
show["print-nothing", RakuEval["print ''; 7"]];      (* the bytes count, not the call *)
show["note", RakuEval["note 'to stderr'; 8"]];       (* stderr does not count *)
show["in-order", RakuEval["say 1; print 2; say 3; 42"]];
show["unicode", RakuEval["say \"\\x[2713]\\x[1F98B]\""]];
show["die", FailureQ[RakuEval["say 'before'; die 'boom'"]]];
RakuEval["sub chatty($x) { say \"chatty $x\"; $x * 2 }"];
show["call", RakuCall["chatty", 21]];                (* a call keeps its value *)

(* a thread still printing after its eval returned: held, and shown first
   by the next call *)
show["started", RakuEval["start { sleep 0.05; say 'late' }; 10"]];
Pause[1.5];
show["next", RakuEval["11"]];
