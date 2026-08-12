\appendix

# The Tag Sets

The three closed enumerations everything else switches on.

## `VT` — runtime value tags

`src/Value.h`. Eighteen tags. Several Raku types are an existing tag plus a
marker rather than a tag of their own; those are listed in the second table.

| Tag | Live fields | Notes |
|---|---|---|
| `Nil` | — | the absent value |
| `Any` | — | the default undefined value |
| `Bool` | `b` | |
| `Int` | `i`, or `big` when it overflows | `enumName`/`enumType` make it an enum value |
| `Num` | `n` | |
| `Str` | `s` | |
| `Array` | `arr`, `isList`, `itemized`, `shape`, `ext` | List, Seq, Junction and lazy sequences all live here |
| `Hash` | `hash`, `hashKind`, `objKeyed` | Set, Bag, Mix, Proxy, Failure, Date and more |
| `Code` | `code` | sub, block, method, builtin, WhateverCode |
| `Range` | `rFrom`, `rTo`, `rExFrom`, `rExTo`, `rNum`, `ext` | `ext` holds the original endpoint objects |
| `Pair` | `s` or `pairKey`, `pairVal`, `namedArg` | |
| `Type` | `s` | a type object; `s` is the type name |
| `Whatever` | — | `*` |
| `Object` | `obj` | a user class instance |
| `Rat` | `ratN`, `ratD`, `fatRat` | normalised, denominator positive |
| `Regex` | `s` (pattern), `hashKind` (flags) | |
| `Match` | `s`, `rFrom`, `rTo`, `arr`, `hash` | five fields live at once |
| `Complex` | `n` (real), `im` (imaginary) | |

### Types with no tag of their own

| Raku type | Representation |
|---|---|
| `List`, `Seq` | `Array` with `isList` |
| an itemized array | `Array` with `itemized` |
| `Junction` | `Array` with `enumName` in `any`/`all`/`one`/`none` |
| a lazy or infinite sequence | `Array` with a `LazySeqState` in `ext` |
| `Set`, `Bag`, `Mix`, and their mutable forms | `Hash` with `hashKind` |
| `Proxy` | `Hash` with `hashKind == "Proxy"`, holding `FETCH`/`STORE` |
| `Failure` | `Hash` with `hashKind == "Failure"` |
| `Date`, `DateTime` | `Hash` with `hashKind` |
| `Buf` | `Str` with `hashKind == "Buf"` and an identity token in `ext` |
| `Blob` | `Str` with `hashKind`, compared by value |
| `Instant`, `Duration` | `Num` with `hashKind` and an identity token |
| `IntStr`, `RatStr`, `NumStr`, `ComplexStr` | the numeric tag plus `s` and `hashKind` |
| an enum value | `Int` with `enumName` and `enumType` |
| `FatRat` | `Rat` with `fatRat` |
| a native integer container | any numeric tag with `natBits` and `natSigned` |
| `Promise`, `Channel`, `Supply`, `Tap`, `Lock` | a tag plus state in `ext` |

## `NK` — AST node kinds

`src/Ast.h`. Forty-nine kinds.

**Expressions.** `IntLit`, `NumLit`, `StrLit`, `InterpStr`, `BoolLit`,
`VarExpr`, `ListExpr`, `Assign`, `Binary`, `Unary`, `Call`, `MethodCall`,
`Index`, `Ternary`, `Range`, `Pair`, `BlockExpr`, `ArrayLit`, `HashLit`,
`NameTerm`, `RegexLit`, `SubstLit`, `ChainExpr`, `SymbolicRef`, `AllomorphLit`,
`NqpOp`, `Whatever`, `SelfTerm`.

**Statements.** `ExprStmt`, `VarDecl`, `SubDecl`, `IfStmt`, `WhileStmt`,
`ForStmt`, `LoopStmt`, `Block`, `ReturnStmt`, `LastStmt`, `NextStmt`,
`RedoStmt`, `UseStmt`, `EmptyStmt`, `GivenStmt`, `WhenStmt`, `RepeatStmt`,
`ClassDecl`, `EnumDecl`, `NamedRegexDecl`, `SubsetDecl`.

A great deal of Raku's surface syntax lowers into a few of these. Every operator
— built-in, meta, hyper, set, and every user-declared one — is a `Binary`,
`Unary` or `Call`. `class`, `role`, `grammar`, `module` and `package` are all
`ClassDecl`. A phaser is a `Block` with a name.

## `K` — regex node kinds

`src/Regex.h`. Twenty kinds.

| Kind | Construct |
|---|---|
| `Lit` | a literal string |
| `Any` | `.` |
| `Class` | `<[…]>`, `\d`, `<:Nd>`, a named class |
| `Seq` | concatenation |
| `Alt` | `\|` (ranked) and `\|\|` (first match) |
| `Conj` | `&` |
| `Rep` | `*`, `+`, `?`, `** N..M`, with `%` separators |
| `Group` | `[ ]`, `( )`, `$<x>=[…]` |
| `AnchorStart`, `AnchorEnd` | `^`, `$`, `^^`, `$$` |
| `WBLeft`, `WBRight` | `<<`, `>>` word boundaries |
| `Nop` | an empty branch |
| `Subrule` | `<name>`, `<.name>`, `<alias=rule>`, `<r($x)>` |
| `Look` | `<?…>`, `<!…>`, `<?after …>` |
| `Code` | `{…}`, `<?{…}>`, `<!{…}>`, `:my` |
| `VarMatch` | a `$var` atom evaluated at match time; backreferences |
| `CapStart`, `CapEnd` | `<(` and `)>` |
| `CondRef` | `(?(N)yes\|no)` under `:P5` — branch on group N's state |

## `NqpOpc` — the `use nqp` subset

`src/Ast.h`. About fifty operation codes in seven groups: lazy control forms,
64-bit integer operations, codepoint-indexed string operations, list and hash
primitives, object and attribute helpers, buffer read and write, and folded
constants. The full list is in Chapter 33.

## Exception and control structs

`src/Interpreter.h`.

| Struct | Meaning |
|---|---|
| `RakuError` | every user-visible Raku exception: payload plus message |
| `ReturnEx` | `return` across a callable boundary |
| `LastEx`, `NextEx`, `RedoEx` | labelled or cross-frame loop control |
| `BreakGivenEx` | `when` / `succeed` across a boundary |
| `ProceedEx` | `proceed` |
| `LeaveEx` | `leave` |
| `ResumeEx` | `.resume` inside a `CATCH` |
| `DoneEx` | `done` in a supply, whenever or react body |
| `StopGatherEx` | a lazy `gather` has produced enough |
| `ExitEx` | `exit` |
| `WorkerAbortEx` | unwind a background worker at shutdown — deliberately **not** a `RakuError` |
| `ParseError` | a compile-time diagnostic, optionally typed |
| `CodegenError` | `--exe` cannot transpile this; bundle instead |
| `AstEmitError` | `--aot` cannot rebuild this; bundle instead |
| `AstSerialError` | a cache entry is unreadable; parse instead |
| `StepLimitExceeded` | a regex exceeded its backtracking budget |
| `ObsoleteEscape` | a retired Perl 5 metacharacter in a pattern |
