#!/bin/sh
# rakupp-bench-sweep.sh — re-measure every Raku++ release, every Rakudo release
# and a sample of mutsu releases on ONE machine in ONE sitting, and emit rows
# shaped like the raku.online dashboard's data files.
#
# Written to be run WITHOUT any assistance, on any machine, so the numbers can
# be checked independently. Everything it needs is fetched; nothing is
# installed system-wide; all state lives under --workdir.
#
#   ./rakupp-bench-sweep.sh --dry-run            # print the plan, touch nothing
#   ./rakupp-bench-sweep.sh                      # Raku++ minors + Rakudo eras
#   ./rakupp-bench-sweep.sh --mutsu-src=$HOME/mutsu --machine=m4
#   ./rakupp-bench-sweep.sh --rakudo-mode=single --rakudo=/usr/bin/rakudo
#
# What is measured, and how often:
#   * Raku++: one release per MINOR version (the first released X.Y.Z of each
#     X.Y; --with-patches adds the rest), lanes `interp` and `--exe` from
#     tools/run-bench.raku, and `--exe` vs `--exe -O` from
#     tools/run-optbench.raku. --tag-passes passes, alternating order, minimum.
#     Rakudo is NOT run per tag (--oracle turns it back on as the correctness
#     oracle): it ships monthly, so per-tag readings only chart noise.
#   * Rakudo: once per RELEASE, every release interleaved, RakuAST frontend
#     only (RAKUDO_RAKUAST=1 before 2026.09, the default from 2026.09). A
#     release that ignores the variable is measured as it is and its rows say
#     `legacy`.
#   * mutsu (opt-in, --mutsu-src): the mutsu tag current at the start of the
#     series and at each Rakudo release date, plus the newest, each built from
#     source; interpreted lane only (mutsu has no --exe).
#
# Send back: <workdir>/out/*.rows, <workdir>/out/series.tsv and
#            <workdir>/environment.txt
set -eu

# ---------------------------------------------------------------- configuration
REPO_SLUG=ash/rakupp
REPO_URL=https://github.com/ash/rakupp.git
RAKUDO_URL=https://github.com/rakudo/rakudo.git

# Pinned so every tag runs IDENTICAL kernel sources and harness: varying the
# workload with the engine would confound engine changes with benchmark changes.
# The harness of this ref also drives itself (DRIVER_TAG's released binary).
REPO_REF=v5.11.0
DRIVER_TAG=v5.11.0

WORKDIR=$(pwd)/rakupp-sweep
# The dashboard's kernels. 'all' = every tools/bench/*.raku of REPO_REF.
KERNELS=fib,loopsum,strcat,hash,hashfill,bigint,sortnums,regex,arrayops,streq,startup,sortby,textsplit,arraypush,rats,objects,multiwhere,mainwhen,intcat
RAKUDO_MODE=source          # source | single
RAKUDO_SINGLE=
ONLY_TAGS=                  # comma-separated subset, e.g. v3.22.0,v3.23.0
WITH_PATCHES=0
TAG_PASSES=2                # passes over the Raku++ tags, alternating order
PASSES=4                    # interleaved passes for the per-RELEASE Rakudo
ORACLE=0                    # 1 = run each tag's era Rakudo as the oracle
WANT_OPT=1                  # the -O lane
MUTSU_SRC=                  # a mutsu git checkout; empty = no mutsu lane
MUTSU_TAGS=auto             # auto | comma-separated tags
MACHINE=                    # label written into the rows, e.g. m4
DRY_RUN=0
JOBS=$( (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) )

# Rakudo releases from the start of the series: version, release date. A gap is
# a gap (there is no 2026.10). Each Raku++ tag belongs to the newest release
# dated on or before it.
ERAS='
2026.06 2026-06-27
2026.07 2026-07-25
2026.08 2026-08-22
2026.09 2026-09-28
2026.11 2026-11-14
'

# Every release that ships a binary: tag, GitHub release date, minor|patch.
# Sorted by DATE: Raku++ version numbers are NOT chronological (v3.14.0 shipped
# 2026-08-11, v3.5.0 on 2026-08-20); a version sort would scramble the series.
# `minor` is the first released tag of its X.Y (v0.5.0 and v0.7.0 shipped no
# binary, so v0.5.1 and v0.7.1 stand for them). v0.1.0, v0.5.0 and v1.1.5 ship
# no binary at all.
TAGS='
v0.5.1   2026-07-13 minor
v0.7.1   2026-07-16 minor
v0.9.0   2026-07-19 minor
v0.9.1   2026-07-20 patch
v1.0.0   2026-07-22 minor
v1.1.0   2026-07-24 minor
v1.2.0   2026-07-26 minor
v1.2.5   2026-07-28 patch
v1.2.6   2026-07-28 patch
v1.5.0   2026-07-28 minor
v1.5.1   2026-07-29 patch
v1.5.2   2026-07-31 patch
v1.7.0   2026-07-31 minor
v1.8.0   2026-08-03 minor
v2.0.0   2026-08-06 minor
v3.0.0   2026-08-08 minor
v3.0.1   2026-08-09 patch
v3.1.0   2026-08-11 minor
v3.14.0  2026-08-11 minor
v3.5.0   2026-08-20 minor
v3.5.1   2026-08-20 patch
v3.6.0   2026-08-21 minor
v3.7.0   2026-08-24 minor
v3.20.0  2026-08-27 minor
v3.20.1  2026-08-27 patch
v3.21.0  2026-08-29 minor
v3.22.0  2026-08-29 minor
v3.23.0  2026-08-29 minor
v3.24.0  2026-09-01 minor
v3.25.0  2026-09-03 minor
v3.26.0  2026-09-09 minor
v3.27.0  2026-09-11 minor
v3.28.0  2026-09-12 minor
v4.0.0   2026-09-17 minor
v4.0.1   2026-09-17 patch
v5.0.0   2026-09-29 minor
v5.0.1   2026-09-29 patch
v5.1.0   2026-09-30 minor
v5.2.0   2026-10-02 minor
v5.2.1   2026-10-03 patch
v5.3.0   2026-10-09 minor
v5.4.0   2026-10-13 minor
v5.4.1   2026-10-13 patch
v5.5.0   2026-10-22 minor
v5.6.0   2026-11-04 minor
v5.6.1   2026-11-04 patch
v5.6.2   2026-11-05 patch
v5.6.3   2026-11-06 patch
v5.7.0   2026-11-11 minor
v5.8.0   2026-11-12 minor
v5.9.0   2026-11-18 minor
v5.10.0  2026-11-19 minor
v5.11.0  2026-11-23 minor
'

# -------------------------------------------------------------------- arguments
for arg in "$@"; do
  case "$arg" in
    --workdir=*)      WORKDIR=${arg#*=} ;;
    --kernels=*)      KERNELS=${arg#*=} ;;
    --rakudo-mode=*)  RAKUDO_MODE=${arg#*=} ;;
    --rakudo=*)       RAKUDO_SINGLE=${arg#*=} ;;
    --repo-ref=*)     REPO_REF=${arg#*=} ;;
    --driver-tag=*)   DRIVER_TAG=${arg#*=} ;;
    --tags=*)         ONLY_TAGS=${arg#*=} ;;
    --with-patches)   WITH_PATCHES=1 ;;
    --tag-passes=*)   TAG_PASSES=${arg#*=} ;;
    --passes=*)       PASSES=${arg#*=} ;;
    --oracle)         ORACLE=1 ;;
    --no-opt)         WANT_OPT=0 ;;
    --mutsu-src=*)    MUTSU_SRC=${arg#*=} ;;
    --mutsu-tags=*)   MUTSU_TAGS=${arg#*=} ;;
    --machine=*)      MACHINE=${arg#*=} ;;
    --jobs=*)         JOBS=${arg#*=} ;;
    --dry-run)        DRY_RUN=1 ;;
    -h|--help)        sed -n '2,30p' "$0"; exit 0 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

say() { printf '%s\n' "$*"; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

# era_for DATE -> the newest Rakudo release dated on or before DATE
era_for() {
  echo "$ERAS" | awk -v d="$1" '$1 != "" && $2 <= d { e = $1 } END { print e }'
}

# The selected tags, as "tag date era" lines, in date order.
if [ -n "$ONLY_TAGS" ]; then
  SEL=$(echo "$TAGS" | awk -v want=",$ONLY_TAGS," '$1 != "" && index(want, "," $1 ",") > 0')
  [ -n "$SEL" ] || die "--tags matched nothing"
elif [ "$WITH_PATCHES" = 1 ]; then
  SEL=$(echo "$TAGS" | awk '$1 != ""')
else
  SEL=$(echo "$TAGS" | awk '$3 == "minor"')
fi
SEL=$(echo "$SEL" | while read -r tag date kind; do
  [ -z "$tag" ] && continue
  printf '%s %s %s\n' "$tag" "$date" "$(era_for "$date")"
done)
NSEL=$(echo "$SEL" | grep -c .)

# The eras any selected tag falls in, oldest first; every one is measured.
USED_ERAS=$(echo "$SEL" | awk '{print $3}' | awk '!seen[$0]++')

# The mutsu tags: explicit, or the newest tag dated on or before the first
# Raku++ tag and each Rakudo release date, plus the newest tag of all.
mutsu_pick() {
  [ -n "$MUTSU_SRC" ] || return 0
  if [ "$MUTSU_TAGS" != auto ]; then echo "$MUTSU_TAGS" | tr ',' '\n'; return 0; fi
  list=$(git -C "$MUTSU_SRC" tag --sort=creatordate --format='%(creatordate:short) %(refname:short)')
  first=$(echo "$SEL" | awk 'NR == 1 {print $2}')
  { echo "$first"
    echo "$ERAS" | awk -v f="$first" '$1 != "" && $2 > f {print $2}'
    echo 9999-12-31
  } | while read -r d; do
    echo "$list" | awk -v d="$d" '$1 <= d { t = $2 } END { if (t != "") print t }'
  done | awk '!seen[$0]++'
}

if [ "$DRY_RUN" = 1 ]; then
  say "DRY RUN — nothing is fetched, built or timed."
  say "kernels ref / driver : $REPO_REF / $DRIVER_TAG"
  say "kernels              : $KERNELS"
  say "Raku++ tags ($NSEL)  : $TAG_PASSES passes, -O lane $( [ "$WANT_OPT" = 1 ] && echo on || echo off ), oracle $( [ "$ORACLE" = 1 ] && echo on || echo off )"
  echo "$SEL" | awk '{ printf "  %-8s %s  rakudo %s\n", $1, $2, $3 }'
  if [ "$RAKUDO_MODE" = single ]; then
    say "Rakudo               : single build $RAKUDO_SINGLE, RakuAST, $PASSES passes"
  else
    say "Rakudo releases      : $(echo $USED_ERAS), built from source, RakuAST, $PASSES passes"
  fi
  if [ -n "$MUTSU_SRC" ]; then
    say "mutsu tags           : $(mutsu_pick | tr '\n' ' ')(from $MUTSU_SRC)"
  else
    say "mutsu                : skipped (no --mutsu-src)"
  fi
  exit 0
fi

# ------------------------------------------------------------ platform / assets
OS=$(uname -s)
ARCH=$(uname -m)
case "$OS" in
  Linux)
    case "$ARCH" in
      x86_64|amd64) ASSET=rakupp-linux-x86_64.tar.gz ;;
      *) die "no Raku++ release binary exists for Linux/$ARCH (x86_64 only).
   Running an x86_64 build here would measure emulation, not the engine." ;;
    esac ;;
  Darwin) ASSET=rakupp-macos-universal.tar.gz ;;
  *) die "unsupported OS '$OS' — use rakupp-bench-sweep.ps1 on Windows" ;;
esac

# ------------------------------------------------------------------- preflight
for t in curl tar git; do
  command -v "$t" >/dev/null 2>&1 || die "missing required tool: $t"
done
CXX=""
for c in c++ g++ clang++; do
  command -v "$c" >/dev/null 2>&1 && { CXX=$c; break; }
done
[ -n "$CXX" ] || die "no C++ compiler found (need c++, g++ or clang++) — the
   'native' lane compiles each kernel with 'rakupp --exe'."
if [ "$RAKUDO_MODE" = source ]; then
  for t in perl make; do
    command -v "$t" >/dev/null 2>&1 || die "missing '$t', required to build Rakudo.
   Alternative: --rakudo-mode=single --rakudo=/path/to/raku"
  done
fi
if [ -n "$MUTSU_SRC" ]; then
  [ -d "$MUTSU_SRC/.git" ] || die "--mutsu-src is not a git checkout: $MUTSU_SRC"
  command -v cargo >/dev/null 2>&1 || die "missing 'cargo', required to build mutsu"
fi

mkdir -p "$WORKDIR"
cd "$WORKDIR"
say "workdir : $WORKDIR"
say "platform: $OS/$ARCH   asset: $ASSET   compiler: $CXX"

# ------------------------------------------------ kernels + harness (pinned ref)
if [ ! -d repo ]; then
  say "cloning $REPO_SLUG at $REPO_REF for the kernels and harness ..."
  git clone --quiet --depth 1 --branch "$REPO_REF" "$REPO_URL" repo
fi
HARNESS=$WORKDIR/repo/tools/run-bench.raku
OPTHARNESS=$WORKDIR/repo/tools/run-optbench.raku
[ -f "$HARNESS" ] || die "harness not found at $HARNESS"
[ "$WANT_OPT" = 0 ] || [ -f "$OPTHARNESS" ] || die "-O harness not found at $OPTHARNESS"
if [ "$KERNELS" = all ]; then
  KERNELS=$(ls "$WORKDIR/repo/tools/bench"/*.raku | sed 's|.*/||; s|\.raku$||' | tr '\n' ',' | sed 's/,$//')
fi
OPTPROGS=$(ls "$WORKDIR/repo/tools/optbench"/*.raku 2>/dev/null | sed 's|.*/||; s|\.raku$||' | tr '\n' ',' | sed 's/,$//')
say "kernels : $KERNELS"
[ "$WANT_OPT" = 1 ] && say "-O progs: $OPTPROGS"

# ------------------------------------------------------- Raku++ release binaries
# GitHub release download URLs are predictable, so no API calls or JSON parsing.
# The .sha256 published beside each archive is checked before unpacking.
sha_of() { if command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1"; else sha256sum "$1"; fi | awk '{print $1}'; }
fetch_tag() {
  tag=$1
  [ -x "$WORKDIR/rakupp/$tag/bin/rakupp" ] && return 0
  d=$WORKDIR/rakupp/$tag
  mkdir -p "$d"
  url="https://github.com/$REPO_SLUG/releases/download/$tag/$ASSET"
  curl -fsSL "$url" -o "$d/asset.tar.gz" || { echo "  $tag: download FAILED" >&2; return 1; }
  if curl -fsSL "$url.sha256" -o "$d/asset.sha256" 2>/dev/null; then
    want=$(awk '{print $1; exit}' "$d/asset.sha256")
    [ "$want" = "$(sha_of "$d/asset.tar.gz")" ] || { echo "  $tag: sha256 MISMATCH" >&2; rm -f "$d/asset.tar.gz"; return 1; }
  fi
  tar xzf "$d/asset.tar.gz" -C "$d" --strip-components=1
  rm -f "$d/asset.tar.gz" "$d/asset.sha256"
  [ -x "$d/bin/rakupp" ] || { echo "  $tag: no bin/rakupp" >&2; return 1; }
}

say "fetching Raku++ release binaries ..."
{ echo "$SEL"; echo "$DRIVER_TAG"; } | awk '!seen[$1]++ {print $1}' | while read -r tag; do
  fetch_tag "$tag" && printf '  %-8s ok\n' "$tag"
done
DRIVER=$WORKDIR/rakupp/$DRIVER_TAG/bin/rakupp
[ -x "$DRIVER" ] || die "driver rakupp missing: $DRIVER"

# ------------------------------------------------------------------- the Rakudo
# On x86_64 MoarVM has its JIT; on arm64 it compiles src/jit/stub.o and has NO
# JIT backend. That is a real difference between platforms, not a bug, and it is
# why cross-machine comparison of the rakudo column must say which it was.
build_rakudo() {
  ver=$1
  [ -x "$WORKDIR/rakudo-$ver/bin/rakudo" ] && return 0
  say "building Rakudo $ver from source (this takes a while) ..."
  [ -d rakudo-src ] || git clone --quiet "$RAKUDO_URL" rakudo-src
  ( cd rakudo-src
    # -ff: a single -f does NOT remove nested git repos (nqp/, nqp/MoarVM/), so
    # objects from a previous version survive and break the link.
    git clean -qxdff
    git checkout -q "$ver"
    perl Configure.pl --gen-moar --gen-nqp --backends=moar \
         --prefix="$WORKDIR/rakudo-$ver" > "$WORKDIR/rakudo-$ver.log" 2>&1
    make -j"$JOBS"  >> "$WORKDIR/rakudo-$ver.log" 2>&1
    make install    >> "$WORKDIR/rakudo-$ver.log" 2>&1
  ) || die "Rakudo $ver build failed — see $WORKDIR/rakudo-$ver.log"
  [ -x "$WORKDIR/rakudo-$ver/bin/rakudo" ] || die "Rakudo $ver did not install"
}

rakudo_for() {   # era -> path
  if [ "$RAKUDO_MODE" = single ]; then printf '%s' "$RAKUDO_SINGLE"
  else printf '%s' "$WORKDIR/rakudo-$1/bin/rakudo"; fi
}

if [ "$RAKUDO_MODE" = single ]; then
  [ -n "$RAKUDO_SINGLE" ] || die "--rakudo-mode=single needs --rakudo=/path/to/rakudo"
  command -v "$RAKUDO_SINGLE" >/dev/null 2>&1 || [ -x "$RAKUDO_SINGLE" ] \
    || die "not executable: $RAKUDO_SINGLE"
  say "Rakudo: single build for every tag -> $RAKUDO_SINGLE"
  say "  NOTE: not era-matched; the rakudo column is then a constant reference,"
  say "  not the engine each release was compared against at the time."
else
  for v in $USED_ERAS; do build_rakudo "$v"; done
fi

# ------------------------------------------------------------------------ mutsu
# Built from a clone of --mutsu-src, never in it: the user's checkout keeps its
# branch and its target/. One shared target/ keeps the per-tag builds incremental.
MUTSU_LIST=$(mutsu_pick)
if [ -n "$MUTSU_SRC" ]; then
  [ -d mutsu-src ] || git clone --quiet "$MUTSU_SRC" mutsu-src
  git -C mutsu-src fetch --quiet --tags "$MUTSU_SRC"
  for mt in $MUTSU_LIST; do
    out=$WORKDIR/mutsu/$mt/mutsu
    [ -x "$out" ] && continue
    say "building mutsu $mt ..."
    mkdir -p "$WORKDIR/mutsu/$mt"
    if ( cd mutsu-src && git clean -qxdf -e target && git checkout -q "$mt" \
         && cargo build --release > "$WORKDIR/mutsu/$mt/build.log" 2>&1 ); then
      cp mutsu-src/target/release/mutsu "$out"
    else
      say "  mutsu $mt: build FAILED (see $WORKDIR/mutsu/$mt/build.log), skipped"
    fi
  done
fi

# ----------------------------------------------------------------- environment
# Which frontend a Rakudo compiles with: RakuAST's compilation unit answers
# RakuAST::CompUnit at BEGIN time; the legacy frontend has no $*CU.
frontend_of() {
  RAKUDO_RAKUAST=1 "$1" -e 'BEGIN print (try $*CU.^name) // "legacy"' 2>/dev/null \
    | grep -q '^RakuAST::CompUnit$' && echo rakuast || echo legacy
}
{
  say "date          : $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  say "os / arch     : $OS / $ARCH"
  say "cpu           : $( (sysctl -n machdep.cpu.brand_string 2>/dev/null) \
                       || (grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2-) \
                       || echo unknown)"
  say "model         : $( (sysctl -n hw.model 2>/dev/null) || echo unknown)"
  say "machine label : ${MACHINE:-(none)}"
  say "cores         : $JOBS"
  say "compiler      : $($CXX --version 2>&1 | head -1)"
  say "kernels ref   : $REPO_REF"
  say "driver rakupp : $DRIVER_TAG"
  say "tags          : $NSEL ($( [ "$WITH_PATCHES" = 1 ] && echo 'minors and patches' || echo 'minors' )), $TAG_PASSES passes"
  say "rakudo mode   : $RAKUDO_MODE"
  for v in $USED_ERAS; do
    p=$(rakudo_for "$v")
    [ -x "$p" ] || command -v "$p" >/dev/null 2>&1 || continue
    say "rakudo $v : $("$p" --version 2>&1 | head -1)  [frontend $(frontend_of "$p")]"
  done
  for mt in $MUTSU_LIST; do
    [ -x "$WORKDIR/mutsu/$mt/mutsu" ] && say "mutsu $mt   : $("$WORKDIR/mutsu/$mt/mutsu" --version 2>&1 | head -1)"
  done
  say "load          : $(uptime)"
} > "$WORKDIR/environment.txt"
cat "$WORKDIR/environment.txt"

# ------------------------------------------------------------- repeatability
# Nine consecutive runs of one kernel. max/min is the honest noise figure for
# this box; the reference machine reads ~1.03 when quiet. Record it either way:
# a sweep whose noise floor is unknown cannot be compared with another machine's.
say "repeatability probe ..."
cat > "$WORKDIR/probe.raku" <<'PROBE_EOF'
my $bin    = @*ARGS[0];
my $kernel = @*ARGS[1];
my @r;
for ^9 {
    my $t0 = now;
    run($bin, $kernel, :out).out.slurp(:close);
    @r.push: (now - $t0) * 1000;
}
printf "%.3f (min %.1fms max %.1fms)\n", @r.max / @r.min, @r.min, @r.max;
PROBE_EOF
probe() {
  P=$("$DRIVER" "$WORKDIR/probe.raku" "$DRIVER" "$WORKDIR/repo/tools/bench/hash.raku" 2>/dev/null || echo "n/a")
  say "  max/min = $P"
  echo "probe ($1) : $P" >> "$WORKDIR/environment.txt"
}
probe start

# -------------------------------------------------------- the Raku++ sweep
# Pass 1 runs the tags oldest first, pass 2 newest first, and so on, so a drift
# in the machine over the hours does not lean the series one way.
mkdir -p "$WORKDIR/tsv" "$WORKDIR/logs" "$WORKDIR/out"
NOEXE=$WORKDIR/no-rakudo   # a RAKUDO that is not Rakudo: the harness skips that lane
say "sweeping $NSEL tags, $TAG_PASSES passes ..."
p=1
while [ "$p" -le "$TAG_PASSES" ]; do
  if [ $((p % 2)) = 1 ]; then ORDER=$SEL; else ORDER=$(echo "$SEL" | awk '{a[NR]=$0} END {for (i=NR;i>0;i--) print a[i]}'); fi
  echo "$ORDER" | while read -r tag date era; do
    [ -z "$tag" ] && continue
    RK=$WORKDIR/rakupp/$tag/bin/rakupp
    [ -x "$RK" ] || { say "  SKIP $tag (no binary)"; continue; }
    RD=$NOEXE
    [ "$ORACLE" = 1 ] && RD=$(rakudo_for "$era")
    t0=$(date +%s)
    RAKUDO_RAKUAST=1 RAKUPP=$RK RAKUDO=$RD "$DRIVER" "$HARNESS" \
        --tsv="$WORKDIR/tsv/$tag.k$p.tsv" --only="$KERNELS" --no-mutsu --no-cnp \
        > "$WORKDIR/logs/$tag.k$p.log" 2>&1 || true   # exit 1 = output mismatch, recorded in the TSV
    if [ "$WANT_OPT" = 1 ]; then
      RAKUDO_RAKUAST=1 RAKUPP=$RK RAKUDO=$RD "$DRIVER" "$OPTHARNESS" \
          --tsv="$WORKDIR/tsv/$tag.o$p.tsv" \
          > "$WORKDIR/logs/$tag.o$p.log" 2>&1 || true
    fi
    printf '  pass %s  %-8s %ss\n' "$p" "$tag" "$(( $(date +%s) - t0 ))"
    # the harness does not unlink its --exe output; N tags x M kernels would pile up
    rm -f /tmp/rakupp-bench-* 2>/dev/null || true
  done
  p=$((p + 1))
done
probe "after tags"

# ------------------------------------------------------------- the mutsu sweep
# The same harness, its interp lane (the driver) riding along and discarded.
if [ -n "$MUTSU_LIST" ]; then
  say "sweeping mutsu: $(echo $MUTSU_LIST) ..."
  p=1
  while [ "$p" -le "$TAG_PASSES" ]; do
    for mt in $MUTSU_LIST; do
      MB=$WORKDIR/mutsu/$mt/mutsu
      [ -x "$MB" ] || continue
      t0=$(date +%s)
      MUTSU=$MB RAKUPP=$DRIVER RAKUDO=$NOEXE "$DRIVER" "$HARNESS" \
          --tsv="$WORKDIR/tsv/mutsu-$mt.m$p.tsv" --only="$KERNELS" --no-native --no-cnp \
          > "$WORKDIR/logs/mutsu-$mt.m$p.log" 2>&1 || true
      printf '  pass %s  mutsu %-8s %ss\n' "$p" "$mt" "$(( $(date +%s) - t0 ))"
    done
    p=$((p + 1))
  done
fi

# ---------------------------------------------- Rakudo, once per RAKUDO release
# Rakudo ships monthly, so it cannot change between two Raku++ tags of the same
# month: measure each release ONCE, properly, and key it by DATE. Every release
# is interleaved inside each round; $PASSES passes; minimum across all of them.
# A file whose warm-up takes over 2 s gets 6 timed rounds instead of 20 (intcat
# is ~7 s a run). Convergence is reported: first half of the passes vs second.
cat > "$WORKDIR/eras.raku" <<'ERAS_EOF'
my ($out, $passes, $machine, $repo, $klist, $olist) = @*ARGS[^6];
$passes = +$passes;
my @specs = @*ARGS[6..*];
my @files = |$klist.split(',').grep(*.chars).map({ %(suite => 'kernel',   name => $_, path => "$repo/tools/bench/$_.raku") }),
            |$olist.split(',').grep(*.chars).map({ %(suite => 'optbench', name => $_, path => "$repo/tools/optbench/$_.raku") });
my %env = %*ENV; %env<RAKUDO_RAKUAST> = '1';
my @eras = @specs.map: { my ($n, $d, $p, $m) = .split('='); %(name => $n, date => $d, path => $p, mode => $m) };
sub t($b, $f) { my $t0 = now; run($b, $f, :out, :err, :%env).out.slurp(:close); (now - $t0) * 1000 }
my (%best, %half);
for ^$passes -> $pass {
    my $h = $pass < $passes / 2 ?? 'A' !! 'B';
    for @files -> %f {
        my $warm = @eras.map({ t(.<path>, %f<path>) }).max;
        my $rounds = $warm > 2000 ?? 6 !! 20;
        for ^$rounds {
            for @eras -> %e {
                my $ms  = t(%e<path>, %f<path>);
                my $key = (%e<name>, %f<suite>, %f<name>).join("\t");
                %best{$key}     = $ms if !(%best{$key}:exists)     || $ms < %best{$key};
                %half{$h}{$key} = $ms if !(%half{$h}{$key}:exists) || $ms < %half{$h}{$key};
            }
        }
    }
    note "  Rakudo pass {$pass + 1}/$passes done";
}
my %era = @eras.map({ .<name> => $_ });
my $k = "$out/rakudo-eras.rows".IO.open(:w);
my $o = "$out/rakudo-optbench.rows".IO.open(:w);
my $c = "$out/rakudo-converge.tsv".IO.open(:w);
$c.say: "era\tsuite\tname\tms\tconverge_pct";
my $worst = 0;
for %best.keys.sort -> $key {
    my ($e, $suite, $name) = $key.split("\t");
    my $a = %half<A>{$key} // %best{$key}; my $b = %half<B>{$key} // %best{$key};
    my $d = ((($a max $b) / ($a min $b)) - 1) * 100;
    $worst = $d if $d > $worst;
    my @row = $e, %era{$e}<date>, $name, sprintf('%.1f', %best{$key}), %era{$e}<mode>;
    @row.push: $machine if $machine;
    ($suite eq 'kernel' ?? $k !! $o).say: @row.join("\t");
    $c.say: ($key, sprintf('%.1f', %best{$key}), sprintf('%.1f', $d)).join("\t");
}
.close for $k, $o, $c;
printf "  halves agree to within %.1f%% (>2%% means the floor has NOT converged)\n", $worst;
ERAS_EOF

say ""
say "measuring Rakudo once per release ($PASSES interleaved passes, RakuAST) ..."
ERA_ARGS=""
for v in $USED_ERAS; do
  rp=$(rakudo_for "$v")
  [ -x "$rp" ] || command -v "$rp" >/dev/null 2>&1 || continue
  mode=$(frontend_of "$rp")
  [ "$mode" = rakuast ] || say "  NOTE: Rakudo $v ignores RAKUDO_RAKUAST=1; its rows say legacy"
  ERA_ARGS="$ERA_ARGS $v=$(echo "$ERAS" | awk -v v="$v" '$1 == v {print $2}')=$rp=$mode"
done
OPTARG=""
[ "$WANT_OPT" = 1 ] && OPTARG=$OPTPROGS
# shellcheck disable=SC2086
"$DRIVER" "$WORKDIR/eras.raku" "$WORKDIR/out" "$PASSES" "$MACHINE" "$WORKDIR/repo" "$KERNELS" "$OPTARG" $ERA_ARGS
probe end

# ----------------------------------------------------------------- aggregation
# Columns are resolved by HEADER NAME, never by position: the harness's TSV has
# grown lanes over time. The figure is the minimum across passes; a cell a
# pass left empty (lane did not run) does not count.
OUT=$WORKDIR/out
agg() {   # agg KIND FILE-GLOB-PREFIX LABEL DATE  — prints "label date name lane ms flags"
  kind=$1 pre=$2 label=$3 date=$4
  for f in "$WORKDIR/tsv/$pre"*.tsv; do
    [ -f "$f" ] || continue
    awk -F'\t' -v kind="$kind" -v label="$label" -v date="$date" '
      /^#/ { next }
      ($1 == "kernel" || $1 == "program") { for (i = 1; i <= NF; i++) col[$i] = i; hdr = 1; next }
      hdr {
        if (kind == "k") { p("interp", g("interp_min_ms")); p("native", g("native_min_ms")) }
        if (kind == "o") { p("exe", g("exe_ms")); p("opt", g("opt_ms")) }
        if (kind == "m") { p("mutsu", g("mutsu_min_ms")) }
      }
      function g(n) { return (n in col) ? $(col[n]) : "" }
      function p(lane, v) { if (v != "") printf "%s\t%s\t%s\t%s\t%s\t%s\n", label, date, $1, lane, v, g("flags") }
    ' "$f"
  done | awk -F'\t' '{ k = $1 FS $2 FS $3 FS $4; if (!(k in m) || $5 + 0 < m[k] + 0) { m[k] = $5; fl[k] = $6 } }
                     END { for (k in m) print k "\t" m[k] "\t" fl[k] }' | sort
}
: > "$OUT/series.tsv.tmp"; : > "$OUT/optbench.tmp"; : > "$OUT/mutsu.tmp"
echo "$SEL" | while read -r tag date era; do
  [ -z "$tag" ] && continue
  agg k "$tag.k" "$tag" "$date" >> "$OUT/series.tsv.tmp"
  agg o "$tag.o" "$tag" "$date" >> "$OUT/optbench.tmp"
done
for mt in $MUTSU_LIST; do
  mdate=$(git -C "$WORKDIR/mutsu-src" log -1 --format=%cs "$mt" 2>/dev/null || echo "")
  agg m "mutsu-$mt.m" "$mt" "$mdate" >> "$OUT/mutsu.tmp"
done
M=""; [ -n "$MACHINE" ] && M="	$MACHINE"
{ printf 'tag\tdate\tname\tlane\tmin_ms\tflags\n'; cat "$OUT/series.tsv.tmp" "$OUT/optbench.tmp"; } > "$OUT/series.tsv"
# Rows in the dashboard's own shapes (bench-backfill.tsv, optbench-m4.tsv):
awk -F'\t' -v m="$M" '{ print $1 "\t" $3 "\t" $4 "\t" $5 m }' "$OUT/series.tsv.tmp" > "$OUT/kernels.rows"
awk -F'\t'           '{ print $1 "\t" $3 "\t" $4 "\t" $5 }'   "$OUT/optbench.tmp"   > "$OUT/optbench.rows"
awk -F'\t' -v m="$M" '{ print $1 "\t" $2 "\t" $3 "\t" $5 m }' "$OUT/mutsu.tmp"      > "$OUT/mutsu.rows"
rm -f "$OUT/series.tsv.tmp" "$OUT/optbench.tmp" "$OUT/mutsu.tmp"

say ""
say "done. In $OUT:"
for f in kernels.rows optbench.rows mutsu.rows rakudo-eras.rows rakudo-optbench.rows; do
  if [ -f "$OUT/$f" ]; then printf '  %-22s %s rows\n' "$f" "$(grep -c . "$OUT/$f" || true)"; fi
done
say "  flags (output mismatches, did-not-run) are in series.tsv's last column."
say "send back: $OUT and $WORKDIR/environment.txt"
