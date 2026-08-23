(* The Wolfram guide's example: compile a grammar from a .raku file, parse
 * text, and move the results into the Wolfram Language. Run from a checkout:
 *
 *   RAKUPP_LIB=build/librakupp.dylib wolframscript -file bindings/wolfram/examples/shopping.wls
 *
 * (.so on Linux; with rakupp on PATH the RAKUPP_LIB is not needed at all.)
 *)

$here = DirectoryName[
  If[StringQ[$InputFileName] && $InputFileName =!= "",
    $InputFileName, First[$ScriptCommandLine]]];
Get[FileNameJoin[{$here, "..", "RakuLang.wl"}]];

g = RakuGrammarFromFile[
  FileNameJoin[{$here, "..", "..", "examples", "shopping.raku"}],
  "Name" -> "Shopping", "Actions" -> "ShoppingActions"];

m = RakuParse[g, "milk=2\nbread = 1  eggs=12\n"];   (* a RakuMatch, or None *)

items = m["item"];
Print[RakuElems[items], " items"];
Do[                                                  (* lazy: one engine call per leaf *)
  Print[RakuStr[items[i]["name"]], " x ", RakuInt[items[i]["qty"]]],
  {i, RakuElems[items]}];

Print["total, computed in Raku: ", RakuMade[m]];     (* ShoppingActions made this *)

Print["as plain Wolfram data: ", ToString[RakuTree[m], InputForm]];   (* the whole match, eagerly *)

(* A non-match returns None; "Strict" -> True returns a diagnosed Failure instead. *)
r = RakuParse[g, "milk=2\nbread=lots\n", "Strict" -> True];
Print["line ", r["Line"], " column ", r["Column"], " while trying <", r["Rule"], ">"];
