# YAMLish 0.1.2, vendored

An unmodified copy of [YAMLish](https://github.com/leont/yamlish) 0.1.2
(`zef:leont`, Leon Timmermans, Artistic-2.0; see `LICENSE`): `lib/`, `t/`,
`META6.json`. Its runtime dependency MIME::Base64 is only `require`d for
`!!binary` values, which neither the suite nor our checks use, so it is not
vendored.

It is here as a regression fixture (issue #100):

- `t/run.raku` runs its `t/*.t` with the rakupp under test, and fails a file
  that prints anything on stderr.
- `t/regression/yamlish-issue-100.raku` loads it in-process and pins the
  inputs that broke.

Do not edit the module to make a test pass. The point is to run it as
shipped.
