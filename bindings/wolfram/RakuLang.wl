(* ::Package:: *)

(* RakuLang — Raku from the Wolfram Language, over librakupp's C ABI.
 *
 * A pure Wolfram Language package: no compiled glue. The loader is
 * ForeignFunctionLoad (Wolfram Language 13.3+), values cross through
 * include/rakupp/rakupp.h, and the grammar logic lives in the Raku shim that
 * ships INSIDE the library (rk_grammar_shim) — this package is invocation,
 * results and lifetime, nothing else.
 *
 *   Get["/path/to/RakuLang.wl"];
 *   RakuEval["my $x = [+] 1..10"]                       (* 55; state persists.
 *       NB: never write Raku's Whatever-star method form in a comment here —
 *       its open-paren-star is a NESTED comment to the Wolfram reader. *)
 *   g = RakuGrammarFromFile["log.raku", "Name" -> "Log"];
 *   m = RakuParse[g, text];                             (* a handle, or None *)
 *   RakuStr[m["line"][1]["ip"]]                         (* lazy: one call per leaf *)
 *   RakuTree[m]                                         (* eager, opt-in *)
 *   RakuClose[m]                                        (* a rooted value — free it *)
 *
 * One interpreter per Wolfram kernel process, created on first use; one
 * kernel thread talks to it (Raku code inside threads freely). Parts count
 * from 1, as everything in the Wolfram Language does.
 *)

BeginPackage["RakuLang`"];

RakuLoad::usage = "RakuLoad[] loads librakupp and starts the interpreter (done on first use anyway); RakuLoad[path] names the library file to use, which is then used as given. Returns the library path.";
RakuShutdown::usage = "RakuShutdown[] frees the interpreter. Every RakuGrammar and RakuMatch from it is dead afterwards; a fresh one may then be started.";
RakuEval::usage = "RakuEval[src] evaluates Raku source in the interpreter's mainline scope and returns the last statement's value. State persists across calls, exactly like the REPL.";
RakuCall::usage = "RakuCall[name, args...] calls a Raku routine by name with Wolfram arguments, returning a Wolfram value. A die inside it returns a Failure[\"RakuError\", ...].";
RakuCan::usage = "RakuCan[name] answers whether a routine of this name is visible in the mainline scope.";
RakuVersion::usage = "RakuVersion[] returns the engine's version string.";
RakuGrammarFromSource::usage = "RakuGrammarFromSource[src, \"Name\" -> ..., \"Actions\" -> ...] compiles Raku grammar source and returns a RakuGrammar. \"Name\" is the grammar's name in the source and may be omitted only when the grammar declaration is the source's last statement; \"Actions\" names an actions class in the same source.";
RakuGrammarFromFile::usage = "RakuGrammarFromFile[path, \"Name\" -> ..., \"Actions\" -> ...] compiles a grammar from a .raku file.";
RakuParse::usage = "RakuParse[grammar, text] parses text and returns a RakuMatch, or None when the parse fails. \"Rule\" -> name parses a fragment with one rule; \"Strict\" -> True returns a diagnosed Failure[\"RakuParseError\", ...] instead of None.";
RakuGrammar::usage = "RakuGrammar[id, label] is a compiled grammar. Get one from RakuGrammarFromFile or RakuGrammarFromSource.";
RakuMatch::usage = "RakuMatch[...] is a successful parse, held as a rooted value in the interpreter. m[\"name\"] and m[i] build a lazy path (i counts from 1); nothing crosses the boundary until a terminal: RakuStr, RakuInt, RakuNum, RakuMade, RakuElems, RakuMatchedQ, RakuTree. Free it with RakuClose.";
RakuStr::usage = "RakuStr[node] returns the matched text at this node.";
RakuInt::usage = "RakuInt[node] returns the matched text at this node as an integer.";
RakuNum::usage = "RakuNum[node] returns the matched text at this node as a real.";
RakuMade::usage = "RakuMade[node] returns what the actions class made here, or Null.";
RakuElems::usage = "RakuElems[node] returns the number of sub-matches at this node.";
RakuMatchedQ::usage = "RakuMatchedQ[node] answers whether anything matched at this node — the way to probe for a capture that a terminal would report as an error.";
RakuTree::usage = "RakuTree[node] converts everything below this node to Wolfram data, eagerly. Prefer the lazy path when you want less than roughly half of a match.";
RakuClose::usage = "RakuClose[m] releases the engine value a RakuMatch holds. There is no garbage-collection hook to lean on: close what you open.";

Begin["`Private`"];

(* ---- session state ---------------------------------------------------------
 * One interpreter per kernel, like every binding: rk_new refuses a second
 * while one is live, so the state is package-level and created on first use.
 *)

$ff = <||>;        (* name -> ForeignFunction *)
$rk = None;        (* RkInterp *)
$ctx = None;       (* RkCtx *)
$libPath = None;

(* ---- the C declarations ----------------------------------------------------
 * Mirrors include/rakupp/rakupp.h + rakupp_ext.h one-to-one, so it can be
 * diffed against them when the ABI grows. RkInterp/RkCtx/RkValue are all
 * opaque pointers; C strings go in as NUL-terminated UnsignedInteger8
 * buffers (RawMemoryExport makes those) and come out the same way.
 *)

cbytes = "RawPointer"::["UnsignedInteger8"];

$signatures = <|
  (* rakupp.h — lifecycle, eval, errors *)
  "rk_new"          -> ({"OpaqueRawPointer"} -> "OpaqueRawPointer"),
  "rk_free"         -> ({"OpaqueRawPointer"} -> "Void"),
  "rk_version"      -> ({} -> cbytes),
  "rk_ctx"          -> ({"OpaqueRawPointer"} -> "OpaqueRawPointer"),
  "rk_eval"         -> ({"OpaqueRawPointer", cbytes, "RawPointer"::["OpaqueRawPointer"]} -> "CInt"),
  "rk_last_error"   -> ({"OpaqueRawPointer"} -> cbytes),
  "rk_grammar_shim" -> ({} -> cbytes),

  (* rakupp_ext.h — constructing *)
  "rk_any"    -> ({"OpaqueRawPointer"} -> "OpaqueRawPointer"),
  "rk_bool"   -> ({"OpaqueRawPointer", "CInt"} -> "OpaqueRawPointer"),
  "rk_int"    -> ({"OpaqueRawPointer", "Integer64"} -> "OpaqueRawPointer"),
  "rk_int_s"  -> ({"OpaqueRawPointer", cbytes} -> "OpaqueRawPointer"),
  "rk_num"    -> ({"OpaqueRawPointer", "CDouble"} -> "OpaqueRawPointer"),
  "rk_rat_s"  -> ({"OpaqueRawPointer", cbytes, cbytes} -> "OpaqueRawPointer"),
  "rk_str"    -> ({"OpaqueRawPointer", cbytes, "UnsignedInteger64"} -> "OpaqueRawPointer"),
  "rk_array"  -> ({"OpaqueRawPointer"} -> "OpaqueRawPointer"),
  "rk_push"   -> ({"OpaqueRawPointer", "OpaqueRawPointer", "OpaqueRawPointer"} -> "Void"),
  "rk_hash"   -> ({"OpaqueRawPointer"} -> "OpaqueRawPointer"),
  "rk_set"    -> ({"OpaqueRawPointer", "OpaqueRawPointer", cbytes, "UnsignedInteger64", "OpaqueRawPointer"} -> "Void"),

  (* rakupp_ext.h — inspecting *)
  "rk_type"    -> ({"OpaqueRawPointer", "OpaqueRawPointer"} -> "CInt"),
  "rk_truthy"  -> ({"OpaqueRawPointer", "OpaqueRawPointer"} -> "CInt"),
  "rk_int_get" -> ({"OpaqueRawPointer", "OpaqueRawPointer"} -> "Integer64"),
  "rk_num_get" -> ({"OpaqueRawPointer", "OpaqueRawPointer"} -> "CDouble"),
  "rk_str_get" -> ({"OpaqueRawPointer", "OpaqueRawPointer", "RawPointer"::["UnsignedInteger64"]} -> cbytes),
  "rk_elems"   -> ({"OpaqueRawPointer", "OpaqueRawPointer"} -> "UnsignedInteger64"),
  "rk_at_pos"  -> ({"OpaqueRawPointer", "OpaqueRawPointer", "UnsignedInteger64"} -> "OpaqueRawPointer"),
  "rk_key_at"  -> ({"OpaqueRawPointer", "OpaqueRawPointer", "UnsignedInteger64", "RawPointer"::["UnsignedInteger64"]} -> cbytes),
  "rk_val_at"  -> ({"OpaqueRawPointer", "OpaqueRawPointer", "UnsignedInteger64"} -> "OpaqueRawPointer"),

  (* rakupp_ext.h — calling back into Raku, errors, roots *)
  "rk_call"        -> ({"OpaqueRawPointer", cbytes, "RawPointer"::["OpaqueRawPointer"], "UnsignedInteger64"} -> "OpaqueRawPointer"),
  "rk_can"         -> ({"OpaqueRawPointer", cbytes} -> "CInt"),
  "rk_error"       -> ({"OpaqueRawPointer"} -> cbytes),
  "rk_clear_error" -> ({"OpaqueRawPointer"} -> "Void"),
  "rk_root"        -> ({"OpaqueRawPointer", "OpaqueRawPointer"} -> "OpaqueRawPointer"),
  "rk_unroot"      -> ({"OpaqueRawPointer", "OpaqueRawPointer"} -> "Void")
|>;

(* ---- failing ---------------------------------------------------------------
 * Public entry points return Failure objects rather than throwing to top
 * level — the scriptable convention: FailureQ[r] to test, r["Message"] to
 * read. Internally that is a Throw every public wrapper catches.
 *)

rakuFail[msg_String] :=
  Throw[Failure["RakuError",
    <|"MessageTemplate" -> msg, "MessageParameters" -> <||>|>], "RakuLang"];

SetAttributes[public, HoldFirst];
public[body_] := Catch[body, "RakuLang"];

(* ---- finding and loading the library --------------------------------------
 * The same contract as every binding: a library you NAME is used as given —
 * if it cannot be loaded, that is the error, never a reason to quietly use
 * some other library (the wrong-architecture case otherwise looks like a
 * mysteriously stale build). Unnamed, the loader searches beside a rakupp
 * binary on PATH: an installed layout's sibling lib/, or a build directory.
 *)

libraryFileName[] := Switch[$OperatingSystem,
  "MacOSX", "librakupp.dylib",
  "Windows", "rakupp.dll",
  _, "librakupp.so"];

explicitCandidate[given_] := Which[
  StringQ[given], {given, "the path passed to RakuLoad"},
  StringQ[Environment["RAKUPP_LIB"]], {Environment["RAKUPP_LIB"], "RAKUPP_LIB"},
  StringQ[Environment["RAKUPP_HOME"]],
    {FileNameJoin[{Environment["RAKUPP_HOME"], "lib", libraryFileName[]}], "RAKUPP_HOME"},
  True, None];

findRakupp[] := Module[{path = Environment["PATH"], sep, exe},
  If[!StringQ[path], Return[None]];
  sep = If[$OperatingSystem === "Windows", ";", ":"];
  exe = If[$OperatingSystem === "Windows", "rakupp.exe", "rakupp"];
  SelectFirst[FileNameJoin[{#, exe}] & /@ StringSplit[path, sep],
    FileExistsQ, None]];

searchCandidates[] := Module[{exe = findRakupp[], dirs},
  If[exe === None, Return[{}]];
  (* both where the name points and where it resolves (a Homebrew keg's bin/,
     whose sibling lib/ is the one) *)
  dirs = DeleteDuplicates[DirectoryName /@ DeleteCases[
    {exe, Quiet[AbsoluteFileName[exe]]}, Except[_String]]];
  Flatten[{FileNameJoin[{#, "..", "lib", libraryFileName[]}],
           FileNameJoin[{#, libraryFileName[]}]} & /@ dirs]];

loadLibrary[path_String] := Module[{ff},
  If[!FileExistsQ[path], Return[$Failed]];
  ff = Association @ KeyValueMap[
    #1 -> Quiet[ForeignFunctionLoad[path, #1, #2]] &, $signatures];
  If[AllTrue[ff, MatchQ[#, _ForeignFunction] &], ff, $Failed]];

initInterp[given_] := Module[{exp, ff = $Failed, used, tried = {}, out},
  If[$VersionNumber < 13.3,
    rakuFail["RakuLang needs the Wolfram foreign function interface (Wolfram Language 13.3 or newer); this kernel is " <> ToString[$VersionNumber]]];
  exp = explicitCandidate[given];
  If[exp =!= None,
    ff = loadLibrary[exp[[1]]];
    If[ff === $Failed,
      rakuFail[exp[[2]] <> " names " <> exp[[1]] <>
        ", which could not be loaded (missing, the wrong architecture, or not a librakupp with the rk_* surface).\n" <>
        "It is used as given \[LongDash] unset it to search for a library instead."]];
    used = exp[[1]],
    (* nothing named: search *)
    Do[
      ff = loadLibrary[cand];
      If[ff === $Failed, AppendTo[tried, cand], used = cand; Break[]],
      {cand, searchCandidates[]}];
    If[ff === $Failed,
      rakuFail["librakupp not found. Set RAKUPP_LIB to the library file, or RAKUPP_HOME to an install prefix containing lib/" <> libraryFileName[] <>
        ". A build directory configured without -DRAKUPP_BUILD_SHARED=ON is static-only and has no library." <>
        If[tried === {}, "", "\nTried:\n  " <> StringRiffle[tried, "\n  "]]]]];
  $ff = ff; $libPath = used;
  $rk = $ff["rk_new"][OpaqueRawPointer[0]];
  If[NullRawPointerQ[$rk],
    $rk = None;
    rakuFail["rk_new refused: an interpreter is already live in this process"]];
  $ctx = $ff["rk_ctx"][$rk];
  (* the grammar shim ships inside the library, already NUL-terminated — its
     pointer goes straight back in as rk_eval's source *)
  out = RawMemoryAllocate["OpaqueRawPointer", 1];
  If[$ff["rk_eval"][$rk, $ff["rk_grammar_shim"][], out] =!= 0,
    rakuFail["the grammar shim failed to load: " <> lastError[]]];
];

ensure[] := If[$rk === None, initInterp[None]];

(* ---- strings across the boundary ------------------------------------------ *)

cstr[s_String] := RawMemoryExport[s];                    (* NUL-terminated UTF-8 *)
utf8len[s_String] := Length[StringToByteArray[s]];       (* bytes, without the NUL *)

readCStr[p_] := If[NullRawPointerQ[p], None, RawMemoryImport[p, "String"]];

lastError[] := Replace[readCStr[$ff["rk_last_error"][$rk]], None -> "rk_eval failed"];

(* rk_str_get hands back a pointer AND a byte length, so embedded NULs
   survive — never read it as a NUL-terminated string *)
readStr[v_] := Module[{lenBuf = RawMemoryAllocate["UnsignedInteger64", 1], p, n},
  p = $ff["rk_str_get"][$ctx, v, lenBuf];
  n = RawMemoryRead[lenBuf];
  If[NullRawPointerQ[p] || n === 0, "", RawMemoryImport[p, {"String", n}]]];

readKey[v_, i_] := Module[{lenBuf = RawMemoryAllocate["UnsignedInteger64", 1], p, n},
  p = $ff["rk_key_at"][$ctx, v, i, lenBuf];
  n = RawMemoryRead[lenBuf];
  If[NullRawPointerQ[p] || n === 0, "", RawMemoryImport[p, {"String", n}]]];

(* ---- value conversion ------------------------------------------------------
 * fromWL results, like all unrooted values, live until the next eval/call on
 * this interpreter — build them, pass them, let go.
 *)

fromWL[Null] := $ff["rk_any"][$ctx];
fromWL[None] := $ff["rk_any"][$ctx];
fromWL[True] := $ff["rk_bool"][$ctx, 1];
fromWL[False] := $ff["rk_bool"][$ctx, 0];
fromWL[n_Integer] := If[-2^63 <= n < 2^63,
  $ff["rk_int"][$ctx, n],
  $ff["rk_int_s"][$ctx, cstr[ToString[n]]]];               (* any width: Raku Ints do not overflow *)
fromWL[x_Real] := $ff["rk_num"][$ctx, x];
fromWL[x_Rational] := $ff["rk_rat_s"][$ctx,                 (* exact in: a Rational becomes a Rat *)
  cstr[ToString[Numerator[x]]], cstr[ToString[Denominator[x]]]];
fromWL[s_String] := $ff["rk_str"][$ctx, cstr[s], utf8len[s]];
fromWL[l_List] := Module[{a = $ff["rk_array"][$ctx]},
  Do[$ff["rk_push"][$ctx, a, fromWL[x]], {x, l}]; a];
fromWL[a_Association] := Module[{h = $ff["rk_hash"][$ctx]},
  KeyValueMap[Function[{k, val}, With[{ks = ToString[k]},
    $ff["rk_set"][$ctx, h, cstr[ks], utf8len[ks], fromWL[val]]]], a];
  h];
fromWL[x_] := rakuFail["cannot pass a " <> ToString[Head[x]] <> " to Raku"];

(* RkType: 0 Any, 1 Bool, 2 Int, 3 Num, 4 Rat, 5 Str, 6 Array, 7 Hash, 8 Other *)
toWL[v_] := Switch[$ff["rk_type"][$ctx, v],
  0, Null,
  1, $ff["rk_truthy"][$ctx, v] =!= 0,
  2, $ff["rk_int_get"][$ctx, v],
  3 | 4, $ff["rk_num_get"][$ctx, v],
  6, Table[toWL[$ff["rk_at_pos"][$ctx, v, i]],
       {i, 0, $ff["rk_elems"][$ctx, v] - 1}],
  7, Association @ Table[
       readKey[v, i] -> toWL[$ff["rk_val_at"][$ctx, v, i]],
       {i, 0, $ff["rk_elems"][$ctx, v] - 1}],
  _, readStr[v]];                                          (* RK_STR / RK_OTHER stringify *)

(* ---- rk_call over raw engine values ---------------------------------------- *)

callRaw[name_String, handles_List] := Module[
  {n = Length[handles], argv, r},
  argv = RawMemoryAllocate["OpaqueRawPointer", Max[n, 1]];
  Do[RawMemoryWrite[argv, handles[[i]], i - 1], {i, n}];
  r = $ff["rk_call"][$ctx, cstr[name], argv, n];
  If[NullRawPointerQ[r],
    With[{msg = Replace[readCStr[$ff["rk_error"][$ctx]], None :> name <> " failed"]},
      $ff["rk_clear_error"][$ctx];
      rakuFail[msg]]];
  r];

(* ---- running Raku ---------------------------------------------------------- *)

RakuLoad[] := public[ensure[]; $libPath];
RakuLoad[path_String] := public[
  If[$rk =!= None,
    rakuFail["an interpreter is already live in this process; RakuShutdown[] first"]];
  initInterp[path]; $libPath];

RakuShutdown[] := (
  If[$rk =!= None, $ff["rk_free"][$rk]];
  $rk = None; $ctx = None; $ff = <||>; $libPath = None;);

RakuEval[src_String] := public[Module[{out, status},
  ensure[];
  out = RawMemoryAllocate["OpaqueRawPointer", 1];
  status = $ff["rk_eval"][$rk, cstr[src], out];
  If[status =!= 0, rakuFail[lastError[]]];
  toWL[RawMemoryRead[out]]]];

RakuCall[name_String, args___] := public[(
  ensure[];
  toWL[callRaw[name, fromWL /@ {args}]])];

RakuCan[name_String] := public[(ensure[]; $ff["rk_can"][$ctx, cstr[name]] =!= 0)];

RakuVersion[] := public[(ensure[]; Replace[readCStr[$ff["rk_version"][]], None -> ""])];

(* ---- parsing with grammars ------------------------------------------------- *)

Options[RakuGrammarFromSource] = {"Name" -> "", "Actions" -> ""};
RakuGrammarFromSource[src_String, OptionsPattern[]] := public[Module[
  {name = OptionValue["Name"], actions = OptionValue["Actions"], gid},
  ensure[];
  If[actions =!= "" && name === "", rakuFail["\"Actions\" needs \"Name\" as well"]];
  gid = toWL[callRaw["rk-grammar-compile", fromWL /@ {src, name, actions}]];
  RakuGrammar[gid, If[name === "", "<anonymous>", name]]]];

Options[RakuGrammarFromFile] = {"Name" -> "", "Actions" -> ""};
RakuGrammarFromFile[path_String, OptionsPattern[]] := public[Module[{src, g},
  src = Quiet[ReadString[path]];
  If[!StringQ[src], rakuFail["could not read " <> path]];
  g = RakuGrammarFromSource[src,
    "Name" -> OptionValue["Name"], "Actions" -> OptionValue["Actions"]];
  If[FailureQ[g], Throw[g, "RakuLang"]];
  RakuGrammar[g[[1]], FileNameTake[path] <>
    If[OptionValue["Name"] === "", "", "#" <> OptionValue["Name"]]]]];

Options[RakuParse] = {"Rule" -> "", "Strict" -> False};
RakuParse[RakuGrammar[gid_, label_], text_String, OptionsPattern[]] := public[Module[{raw},
  ensure[];
  raw = callRaw["rk-grammar-parse", fromWL /@ {gid, text, OptionValue["Rule"]}];
  If[$ff["rk_type"][$ctx, raw] =!= 0,
    RakuMatch[$ff["rk_root"][$ctx, raw]],
    If[OptionValue["Strict"], parseFailure[label, text], None]]]];

parseFailure[label_, text_] := Module[
  {d = toWL[callRaw["rk-grammar-diagnosis", {fromWL[text]}]]},
  If[AssociationQ[d],
    Failure["RakuParseError", <|
      "MessageTemplate" -> label <> ": no match \[LongDash] failed at line " <>
        ToString[d["line"]] <> " column " <> ToString[d["col"]] <>
        " while trying <" <> d["rule"] <> ">",
      "MessageParameters" -> <||>,
      "Line" -> d["line"], "Column" -> d["col"],
      "Position" -> d["pos"], "Rule" -> d["rule"]|>],
    Failure["RakuParseError", <|
      "MessageTemplate" -> label <> ": no match",
      "MessageParameters" -> <||>|>]]];

(* ---- walking a match -------------------------------------------------------
 * m["item"] and m[i] build a lazy path; every terminal is ONE rk_call. The
 * index counts from 1, as everything in the Wolfram Language does — the shim
 * underneath counts from 0, and the subtraction happens here.
 *)

RakuMatch[p_][k_String] := RakuMatch[p, {k}];
RakuMatch[p_][i_Integer /; i >= 1] := RakuMatch[p, {i - 1}];
RakuMatch[p_, steps_List][k_String] := RakuMatch[p, Append[steps, k]];
RakuMatch[p_, steps_List][i_Integer /; i >= 1] := RakuMatch[p, Append[steps, i - 1]];

walk[m_RakuMatch, op_String] := With[
  {p = First[m], steps = If[Length[m] > 1, m[[2]], {}]},
  callRaw["rk-match-walk", {p, fromWL[steps], fromWL[op]}]];

RakuStr[m_RakuMatch]      := public[(ensure[]; toWL[walk[m, "str"]])];
RakuInt[m_RakuMatch]      := public[(ensure[]; toWL[walk[m, "int"]])];
RakuNum[m_RakuMatch]      := public[(ensure[]; toWL[walk[m, "num"]])];
RakuMade[m_RakuMatch]     := public[(ensure[]; toWL[walk[m, "made"]])];
RakuTree[m_RakuMatch]     := public[(ensure[]; toWL[walk[m, "tree"]])];
RakuElems[m_RakuMatch]    := public[(ensure[]; toWL[walk[m, "elems"]])];
RakuMatchedQ[m_RakuMatch] := public[(ensure[]; toWL[walk[m, "bool"]])];

RakuClose[m_RakuMatch] := public[(ensure[]; $ff["rk_unroot"][$ctx, First[m]]; Null)];

End[];
EndPackage[];
