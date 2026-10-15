# Miscellaneous

*Started 2026-10-15.*

Small items that belong to no plan: gaps found in passing, often while working
on something outside this repo. Each line says how to reproduce the gap and who
needs it fixed. When an item is done, remove its line; when one grows into a
plan, move it to [TODO.md](TODO.md).

---

- [ ] **Imported symbols are missing from `MY::`** (found 2026-10-10; still
  open in the 2026-10-13 build). Calling an imported sub works; only the stash
  view is missing:

  ```raku
  use Test;
  say MY::.keys.grep(*.starts-with("&")).grep(* eq any <&ok &is &plan>).sort;
  say MY::<&ok>:exists;
  ```

  rakupp prints `()` and `False`; Rakudo prints `(&is &ok &plan)` and `True`.
  Imports should land in the importing scope's lexpad the way Rakudo installs
  them, so that `MY::` keys, `:exists`, `MY::<&name>` lookup and the
  pseudo-packages that walk lexical scopes (`OUTER::`, `UNIT::`) all see them.
  The same goes for imported constants, classes and `is export` variables.
  Who needs it: [rakush](https://github.com/ash/rakush), a shell that embeds
  rakupp through the `rk_*` C API, treats every sub in the session's mainline
  `MY::` as a command and finds them with `rk_eval` of `MY::.keys`. With this
  fixed, `use Some::Module` at its prompt makes the module's exported subs
  commands, with no change to rakush. Test that an import done in one
  `rk_eval` call shows up in `MY::` in a later `rk_eval` call on the same
  interpreter, since that is how rakush uses it.
