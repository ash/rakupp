#!/bin/sh
# Run every modinfo command under Rakudo and under Raku++ and diff STDOUT
# byte-for-byte. This is the claim the showcase makes, made checkable.
#
#   RAKUPP=../../build/rakupp sh compare.sh
#
# The distributions modinfo itself depends on are not installed by this script:
# point MODLIB at a comma-separated list of `lib` directories holding them (or
# `zef install` them, in which case MODLIB can be left unset).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
cd "$HERE"

R="${RAKUPP:?set RAKUPP=<path to the rakupp binary>}"
RAKU="${RAKU:-raku}"

# Both engines accept a comma-separated RAKULIB, so one setting serves both.
if [ -n "$MODLIB" ]; then
    RAKULIB="$MODLIB"; export RAKULIB
fi

# Every command except `about`, with colour off so the diff is about content.
# `about` is excluded on purpose: it reports the engine it is running under, so
# the two runs are SUPPOSED to differ there.
# `check` exits non-zero by design (the fixture set contains a broken dist).
run_all() {
    engine=$1
    for cmd in \
        "list" \
        "graph" \
        "rank" \
        "show Gadget" \
        "show Cor" \
        "deps Gadget" \
        "rdeps Corelib" \
        "deps Loopy" \
        "path Gadget" \
        "path Corelib::Util" \
        "check" \
        "check Rusty" \
        "export --format=json" \
        "export --format=yaml" \
        "export --format=xml"
    do
        echo "===== $cmd"
        # STDOUT only: Rakudo writes deprecation notices about the vendored
        # library paths to STDERR, and they are about the corpus, not modinfo.
        # shellcheck disable=SC2086
        $engine modinfo.raku $cmd --no-color 2>/dev/null || true
    done
}

run_all "$RAKU" > /tmp/modinfo-rakudo.out
run_all "$R"    > /tmp/modinfo-rakupp.out

if diff -u /tmp/modinfo-rakudo.out /tmp/modinfo-rakupp.out > /tmp/modinfo.diff; then
    echo "MATCH — byte-identical under Rakudo and Raku++ ($(wc -l < /tmp/modinfo-rakudo.out) lines)"
    exit 0
fi
echo "DIFF — see /tmp/modinfo.diff"
head -60 /tmp/modinfo.diff
exit 1
