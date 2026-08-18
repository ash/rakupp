#!/bin/sh
# Two oracles for the SQLite client.
#
#   1. against the real thing — the same queries through `sqlite3 -csv -header`
#      and `sqlite3 -json`, diffed byte for byte. Those two formats are
#      reproduced exactly on purpose, so a difference means the binding or the
#      value marshalling is wrong.
#   2. against the other engine — the same program under Rakudo and under
#      Raku++, in every output format, plus the dot commands and the error
#      paths. This is the modinfo/jsonreq check.
#
#   RAKUPP=../../build/rakupp sh compare.sh
#
# Leg 1 is skipped when there is no `sqlite3` on PATH, leg 2 when there is no
# `raku`. Queries live one per line, so any newline inside a value is written
# as char(10) rather than typed literally.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
START=$(pwd)
cd "$HERE"

R="${RAKUPP:?set RAKUPP=<path to the rakupp binary>}"
# RAKUPP=build/rakupp from the repo root is the natural spelling — resolve it
# against the CALLER's directory, since everything below runs from $HERE.
case "$R" in /*) ;; */*) R="$START/$R" ;; esac
RAKU="${RAKU:-raku}"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
DB="$WORK/demo.db"

# The fixture is built by the showcase itself: `.read` hands the whole script
# to sqlite3_exec(), so nothing external is needed to get a database.
"$R" "$HERE/sqlite.raku" "$DB" ".read $HERE/seed.sql"

# Queries chosen to walk every storage class and every awkward text case:
# NULLs, blobs, floats, a comma, embedded quotes, an embedded newline,
# non-ASCII, aggregates, a join, expressions, and an empty result.
cat > "$WORK/queries" <<'Q'
select * from artists;
select * from albums;
select * from tracks;
select id, name, country from artists where country is not null order by name;
select a.name, b.title, b.year from artists a join albums b on b.artist_id = a.id order by b.id;
select artist_id, count(*) as albums, avg(rating) as avg_rating from albums group by artist_id;
select title, length(title) as len, upper(title) as loud from albums order by len desc, title;
select cover from albums where cover is not null;
select notes from albums where notes like '%"%';
select notes from albums where instr(notes, char(10)) > 0;
select 1 as one, 2.5 as two_and_a_half, 'text' as t, NULL as nada, x'01ff10' as raw_bytes;
select * from tracks where seconds > 100000;
select seconds / 60 as minutes, seconds % 60 as rest from tracks where seconds is not null limit 5;
select name, founded, founded * 2 as doubled from artists order by id;
Q

# Leg 1 runs one process per query, which is cheap for both a C binary and
# Raku++. Leg 2 uses the `-` batch mode instead: Rakudo needs about seven
# seconds to compile this file, so one process per query there would dominate
# the whole run.

run_ours() {     # $1 = engine, $2 = format
    while IFS= read -r q; do
        [ -n "$q" ] || continue
        printf '===== [%s] %s\n' "$2" "$q"
        st=0
        "$1" "$HERE/sqlite.raku" --format="$2" "$DB" "$q" 2>&1 || st=$?
        printf '(exit %s)\n' "$st"
    done < "$WORK/queries"
}

run_sqlite3() {  # $1 = flags, $2 = label
    while IFS= read -r q; do
        [ -n "$q" ] || continue
        printf '===== [%s] %s\n' "$2" "$q"
        st=0
        # shellcheck disable=SC2086
        sqlite3 $1 "$DB" "$q" 2>&1 || st=$?
        printf '(exit %s)\n' "$st"
    done < "$WORK/queries"
}

run_batch() {    # $1 = engine, $2 = format, $3 = statement file
    printf '===== batch [%s] %s\n' "$2" "$(basename "$3")"
    st=0
    "$1" "$HERE/sqlite.raku" --format="$2" --echo "$DB" - < "$3" 2>&1 || st=$?
    printf '(exit %s)\n' "$st"
}

cases=$(grep -c . "$WORK/queries")
status=0

# ---- leg 1: against the real sqlite3 shell ------------------------------
if command -v sqlite3 >/dev/null 2>&1; then
    { run_ours "$R" csv;                 run_ours "$R" json;     } > "$WORK/ours.out"
    { run_sqlite3 "-csv -header" csv;    run_sqlite3 "-json" json; } > "$WORK/real.out"
    if diff -au "$WORK/real.out" "$WORK/ours.out" > "$WORK/real.diff"; then
        echo "MATCH vs sqlite3 — csv and json byte-identical ($cases queries x 2 formats)"
    else
        echo "DIFF vs sqlite3:"
        head -40 "$WORK/real.diff"
        status=1
    fi
else
    echo "SKIP vs sqlite3 — no sqlite3 on PATH"
fi

# ---- leg 2: Raku++ against Rakudo ---------------------------------------
# The dot commands and the failing statements ride through the same batch
# mode, so leg 2 costs five processes per engine rather than sixty.
cat > "$WORK/extras" <<'X'
.tables
.schema
.schema albums
.schema nope
.version
.help
select * from nope;
not sql at all;
select 1 as recovered_after_errors;
select x'00ff10' as blob_with_a_nul;
X

both_engines() {  # $1 = engine
    for fmt in table csv json list; do
        run_batch "$1" "$fmt" "$WORK/queries"
    done
    run_batch "$1" table "$WORK/extras"
}

if command -v "$RAKU" >/dev/null 2>&1; then
    both_engines "$R"    > "$WORK/rakupp.out"
    both_engines "$RAKU" > "$WORK/rakudo.out"
    if diff -au "$WORK/rakudo.out" "$WORK/rakupp.out" > "$WORK/engines.diff"; then
        echo "MATCH vs Rakudo — byte-identical under both engines ($(wc -l < "$WORK/rakupp.out" | tr -d ' ') lines)"
    else
        echo "DIFF vs Rakudo:"
        head -40 "$WORK/engines.diff"
        status=1
    fi
else
    echo "SKIP vs Rakudo — no $RAKU on PATH"
fi

exit $status
