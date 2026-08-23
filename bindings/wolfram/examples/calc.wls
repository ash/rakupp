(* The Wolfram guide's second example: Raku as a library, no grammar involved.
 * Evaluate source, call subs with Wolfram values, read results back. Run from
 * a checkout:
 *
 *   RAKUPP_LIB=build/librakupp.dylib wolframscript -file bindings/wolfram/examples/calc.wls
 *
 * (.so on Linux; with rakupp on PATH the RAKUPP_LIB is not needed at all.)
 *)

$here = DirectoryName[
  If[StringQ[$InputFileName] && $InputFileName =!= "",
    $InputFileName, First[$ScriptCommandLine]]];
Get[FileNameJoin[{$here, "..", "RakuLang.wl"}]];

(* RakuEval runs source in the mainline scope and keeps it, exactly like the REPL. *)
Print["2 + 2 = ", RakuEval["2 + 2"]];

(* So loading a file of subs is just an eval — they stay callable below. *)
RakuEval[ReadString[FileNameJoin[{$here, "..", "..", "examples", "calc.raku"}]]];

(* RakuCall passes Wolfram values as arguments and returns Wolfram values. *)
Print["area(3, 4) = ", RakuCall["area", 3, 4]];

primes = RakuCall["primes-below", 30];        (* a Raku list -> a Wolfram List *)
Print["primes below 30: ", StringRiffle[ToString /@ primes, " "]];

s = RakuCall["stats", {3, 1, 4, 1, 5, 9, 2, 6}];   (* a List -> a Raku list *)
Print["stats: count=", s["count"], " sum=", s["sum"],   (* a Raku hash -> an Association *)
  " mean=", s["mean"], " max=", s["max"]];

Print["greet: ", RakuCall["greet", <|"name" -> "Ada", "age" -> 36|>]];

(* Raku integers do not overflow; past 64 bits this one hands back digits. *)
Print["30! = ", RakuCall["factorial", 30]];

(* A die inside Raku crosses as Failure["RakuError", ...], the host's own kind. *)
err = RakuCall["checked-div", 10, 0];
Print["died: ", err["Message"]];
