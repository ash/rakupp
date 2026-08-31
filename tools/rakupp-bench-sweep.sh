#!/bin/sh
# rakupp-bench-sweep.sh — measure every released Raku++ build against the Rakudo
# that was current on its release date, and emit one TSV.
#
# Written to be run WITHOUT any assistance, on a machine other than the one of
# record, so the numbers can be checked independently. Everything it needs is
# fetched; nothing is installed system-wide; all state lives under --workdir.
#
#   ./rakupp-bench-sweep.sh                        # Linux/macOS, builds Rakudo
#   ./rakupp-bench-sweep.sh --rakudo-mode=single --rakudo=/usr/bin/raku
#   ./rakupp-bench-sweep.sh --kernels=all --workdir=/scratch/sweep
#
# Send back:  <workdir>/series.tsv  and  <workdir>/environment.txt
set -eu

# ---------------------------------------------------------------- configuration
REPO_SLUG=ash/rakupp
REPO_URL=https://github.com/ash/rakupp.git
RAKUDO_URL=https://github.com/rakudo/rakudo.git

# Pinned so every machine measures IDENTICAL kernel sources. The kernels are
# taken from this ref for all tags on purpose: varying the workload with the
# engine would confound engine changes with benchmark changes.
REPO_REF=v3.23.0
DRIVER_TAG=v3.23.0          # which released rakupp interprets the harness itself

WORKDIR=$(pwd)/rakupp-sweep
KERNELS=fib,loopsum,strcat  # 'all' = every kernel in tools/bench
RAKUDO_MODE=source          # source | single
RAKUDO_SINGLE=
ONLY_TAGS=                  # comma-separated subset, e.g. v3.22.0,v3.23.0
PASSES=4                    # interleaved passes for the per-ERA Rakudo measurement
JOBS=$( (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) )

# tag / release-date / the Rakudo current on that date.
# Rakudo releases: 2026.06 -> 2026-06-27, 2026.07 -> 2026-07-25, 2026.08 -> 2026-08-22.
# NOTE the ordering: these are sorted by DATE, and Raku++ version numbers are
# NOT chronological (v3.14.0 shipped 2026-08-11, v3.5.0 on 2026-08-20). Keep
# this list in date order; a version sort would scramble the series.
TAGS='
v0.5.1  2026-07-13 2026.06
v0.7.1  2026-07-16 2026.06
v0.9.0  2026-07-19 2026.06
v0.9.1  2026-07-20 2026.06
v1.0.0  2026-07-22 2026.06
v1.1.0  2026-07-24 2026.06
v1.2.0  2026-07-26 2026.07
v1.2.5  2026-07-28 2026.07
v1.2.6  2026-07-28 2026.07
v1.5.0  2026-07-28 2026.07
v1.5.1  2026-07-29 2026.07
v1.5.2  2026-07-31 2026.07
v1.7.0  2026-07-31 2026.07
v1.8.0  2026-08-03 2026.07
v2.0.0  2026-08-06 2026.07
v3.0.0  2026-08-08 2026.07
v3.0.1  2026-08-09 2026.07
v3.1.0  2026-08-11 2026.07
v3.14.0 2026-08-11 2026.07
v3.5.0  2026-08-20 2026.07
v3.5.1  2026-08-20 2026.07
v3.6.0  2026-08-21 2026.07
v3.7.0  2026-08-24 2026.08
v3.20.0 2026-08-27 2026.08
v3.20.1 2026-08-27 2026.08
v3.21.0 2026-08-29 2026.08
v3.22.0 2026-08-29 2026.08
v3.23.0 2026-08-29 2026.08
'

# -------------------------------------------------------------------- arguments
for arg in "$@"; do
  case "$arg" in
    --workdir=*)      WORKDIR=${arg#*=} ;;
    --kernels=*)      KERNELS=${arg#*=} ;;
    --rakudo-mode=*)  RAKUDO_MODE=${arg#*=} ;;
    --rakudo=*)       RAKUDO_SINGLE=${arg#*=} ;;
    --repo-ref=*)     REPO_REF=${arg#*=} ;;
    --tags=*)         ONLY_TAGS=${arg#*=} ;;
    --passes=*)       PASSES=${arg#*=} ;;
    --jobs=*)         JOBS=${arg#*=} ;;
    -h|--help)        sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

if [ -n "$ONLY_TAGS" ]; then
  TAGS=$(echo "$TAGS" | awk -v want=",$ONLY_TAGS," \
           '$1 != "" && index(want, "," $1 ",") > 0')
  [ -n "$TAGS" ] || { echo "ERROR: --tags matched nothing" >&2; exit 2; }
fi

say() { printf '%s\n' "$*"; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

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
[ -f "$HARNESS" ] || die "harness not found at $HARNESS"

# ------------------------------------------------------- Raku++ release binaries
# GitHub release download URLs are predictable, so no API calls or JSON parsing.
fetch_tag() {
  tag=$1
  [ -x "$WORKDIR/rakupp/$tag/bin/rakupp" ] && return 0
  mkdir -p "$WORKDIR/rakupp/$tag"
  url="https://github.com/$REPO_SLUG/releases/download/$tag/$ASSET"
  curl -fsSL "$url" -o "$WORKDIR/rakupp/$tag/asset.tar.gz" \
    || { echo "  $tag: download FAILED" >&2; return 1; }
  tar xzf "$WORKDIR/rakupp/$tag/asset.tar.gz" -C "$WORKDIR/rakupp/$tag" --strip-components=1
  rm -f "$WORKDIR/rakupp/$tag/asset.tar.gz"
  [ -x "$WORKDIR/rakupp/$tag/bin/rakupp" ] || { echo "  $tag: no bin/rakupp" >&2; return 1; }
}

say "fetching Raku++ release binaries ..."
echo "$TAGS" | while read -r tag date era; do
  [ -z "$tag" ] && continue
  fetch_tag "$tag" && printf '  %-8s ok\n' "$tag"
done

# ------------------------------------------------------------------- the Rakudo
# On x86_64 MoarVM has its JIT; on arm64 it compiles src/jit/stub.o and has NO
# JIT backend. That is a real difference between platforms, not a bug, and it is
# why cross-machine comparison of the rakudo column must say which it was.
build_rakudo() {
  ver=$1
  [ -x "$WORKDIR/rakudo-$ver/bin/raku" ] && return 0
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
  [ -x "$WORKDIR/rakudo-$ver/bin/raku" ] || die "Rakudo $ver did not install"
}

rakudo_for() {   # era -> path
  if [ "$RAKUDO_MODE" = single ]; then printf '%s' "$RAKUDO_SINGLE"
  else printf '%s' "$WORKDIR/rakudo-$1/bin/raku"; fi
}

if [ "$RAKUDO_MODE" = single ]; then
  [ -n "$RAKUDO_SINGLE" ] || die "--rakudo-mode=single needs --rakudo=/path/to/raku"
  command -v "$RAKUDO_SINGLE" >/dev/null 2>&1 || [ -x "$RAKUDO_SINGLE" ] \
    || die "not executable: $RAKUDO_SINGLE"
  say "Rakudo: single build for every tag -> $RAKUDO_SINGLE"
  say "  NOTE: not era-matched; the rakudo column is then a constant reference,"
  say "  not the engine each release was compared against at the time."
else
  for v in $(echo "$TAGS" | awk '$3 != "" {print $3}' | sort -u); do build_rakudo "$v"; done
fi

# ----------------------------------------------------------------- environment
{
  say "date          : $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  say "os / arch     : $OS / $ARCH"
  say "cpu           : $( (sysctl -n machdep.cpu.brand_string 2>/dev/null) \
                       || (grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2-) \
                       || echo unknown)"
  say "cores         : $JOBS"
  say "compiler      : $($CXX --version 2>&1 | head -1)"
  say "kernels ref   : $REPO_REF"
  say "driver rakupp : $DRIVER_TAG"
  say "rakudo mode   : $RAKUDO_MODE"
  for v in 2026.06 2026.07 2026.08; do
    p=$WORKDIR/rakudo-$v/bin/raku
    [ -x "$p" ] && say "rakudo $v : $("$p" --version 2>&1 | head -1)"
  done
  [ "$RAKUDO_MODE" = single ] && say "rakudo single : $("$RAKUDO_SINGLE" --version 2>&1 | head -1)"
  say "load          : $(uptime)"
} > "$WORKDIR/environment.txt"
cat "$WORKDIR/environment.txt"

# ---------------------------------------------------------------------- kernels
if [ "$KERNELS" = all ]; then
  KERNELS=$(ls "$WORKDIR/repo/tools/bench"/*.raku | sed 's|.*/||; s|\.raku$||' | tr '\n' ',' | sed 's/,$//')
fi
say "kernels: $KERNELS"

# ------------------------------------------------------------- repeatability
# Nine consecutive runs of one kernel. max/min is the honest noise figure for
# this box; the reference machine reads ~1.03 when quiet. Record it either way:
# a sweep whose noise floor is unknown cannot be compared with another machine's.
DRIVER=$WORKDIR/rakupp/$DRIVER_TAG/bin/rakupp
[ -x "$DRIVER" ] || die "driver rakupp missing: $DRIVER"
say "repeatability probe ..."
# Timed by rakupp itself rather than the shell: 'date +%s%N' is a GNU extension
# and yields nothing usable on BSD/macOS, and this way the probe uses the same
# clock the harness does.
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
PROBE=$("$DRIVER" "$WORKDIR/probe.raku" "$DRIVER" "$WORKDIR/repo/tools/bench/hash.raku" 2>/dev/null || echo "n/a")
say "  max/min = $PROBE"
echo "probe         : $PROBE" >> "$WORKDIR/environment.txt"

# ------------------------------------------------------------------- the sweep
mkdir -p "$WORKDIR/tsv" "$WORKDIR/logs"
say "sweeping ..."
echo "$TAGS" | while read -r tag date era; do
  [ -z "$tag" ] && continue
  RK=$WORKDIR/rakupp/$tag/bin/rakupp
  RD=$(rakudo_for "$era")
  [ -x "$RK" ] || { say "  SKIP $tag (no binary)"; continue; }
  [ -x "$RD" ] || command -v "$RD" >/dev/null 2>&1 || { say "  SKIP $tag (no rakudo $era)"; continue; }
  t0=$(date +%s)
  RAKUPP=$RK RAKUDO=$RD "$DRIVER" "$HARNESS" \
      --tsv="$WORKDIR/tsv/$tag.tsv" --only="$KERNELS" \
      > "$WORKDIR/logs/$tag.log" 2>&1 || true   # exit 1 = output mismatch, recorded in the TSV
  printf '  %-8s rakudo %s  %ss\n' "$tag" "$era" "$(( $(date +%s) - t0 ))"
  # the harness does not unlink its --exe output; 28 tags x N kernels would pile up
  rm -f /tmp/rakupp-bench-* 2>/dev/null || true
done

# ----------------------------------------------------------------- aggregation
OUT=$WORKDIR/series.tsv
printf 'tag\tdate\trakudo\tkernel\tinterp_min\tinterp_med\tnative_min\tnative_med\trakudo_min\trakudo_med\tflags\n' > "$OUT"
echo "$TAGS" | while read -r tag date era; do
  [ -z "$tag" ] && continue
  f=$WORKDIR/tsv/$tag.tsv
  [ -f "$f" ] || continue
  # Resolve columns by HEADER NAME, never by position: the harness grew a mutsu
  # lane after v3.23.0, so rakudo_min_ms is field 6 with a v3.23.0-pinned
  # harness and field 8 with a newer one. Hardcoding positions silently
  # produced zero rows.
  awk -F'\t' -v tag="$tag" -v date="$date" -v era="$era" '
    /^#/ { next }
    $1 == "kernel" { for (i = 1; i <= NF; i++) col[$i] = i; hdr = 1; next }
    hdr {
      printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n",
             tag, date, era, $1,
             g("interp_min_ms"), g("interp_med_ms"),
             g("native_min_ms"), g("native_med_ms"),
             g("rakudo_min_ms"), g("rakudo_med_ms"), g("flags")
    }
    function g(n) { return (n in col) ? $(col[n]) : "" }
  ' "$f" >> "$OUT"
done

# ---------------------------------------------- Rakudo, once per RAKUDO release
# The sweep above measures the rakudo lane once per RAKU++ tag, because the
# harness needs it as the correctness oracle. Those numbers must NOT be charted:
# Rakudo ships monthly, so it cannot change between two Raku++ tags cut on the
# same day, and every difference in that column is run-to-run noise. On the
# reference machine it wobbled up to 19% on sortby (whose Rakudo lane is bimodal
# there, ~168/198ms) and 2-10% elsewhere.
#
# So measure each Rakudo ONCE, properly, and key it by DATE: 20 timed runs after
# a discarded warm-up, every era interleaved inside each round, $PASSES such
# passes, and the minimum across all of them. Two passes is not enough and not
# a check either — run as sequential blocks they track the machine's drift and
# disagreed by up to 10% on the reference machine. Convergence is reported
# below: compare the first half of the passes against the second.
ERAS_OUT=$WORKDIR/rakudo-eras.tsv
say ""
say "measuring Rakudo once per release ($PASSES interleaved passes) ..."
{
  echo 'my $out = @*ARGS[0]; my $passes = +@*ARGS[1]; my @kernels = @*ARGS[2].split(",");'
  echo 'my @eras; for @*ARGS[3..*] -> $spec { my ($n, $p) = $spec.split("="); @eras.push: { name => $n, path => $p } }'
  echo 'sub t($b, $k) { my $t0 = now; run($b, $k, :out, :err).out.slurp(:close); (now - $t0) * 1000 }'
  echo 'my %best; my %half;'
  echo 'for ^$passes -> $pass {'
  echo '  for @kernels -> $k {'
  echo '    my $path = "REPO/tools/bench/$k.raku";'
  echo '    for ^21 -> $r {'
  echo '      for @eras -> %e {'
  echo '        my $ms = t(%e<path>, $path);'
  echo '        next if $r == 0;'
  echo '        my $key = %e<name> ~ "\t" ~ $k;'
  echo '        %best{$key} = $ms if !(%best{$key}:exists) || $ms < %best{$key};'
  echo '        my $h = $pass < $passes / 2 ?? "A" !! "B";'
  echo '        %half{$h}{$key} = $ms if !(%half{$h}{$key}:exists) || $ms < %half{$h}{$key};'
  echo '      }'
  echo '    }'
  echo '  }'
  echo '  note "  pass {$pass + 1}/$passes done";'
  echo '}'
  echo 'my $fh = $out.IO.open(:w);'
  echo '$fh.say: "era\tkernel\tms\tconverge_pct";'
  echo 'my $worst = 0;'
  echo 'for %best.keys.sort -> $key {'
  echo '  my $a = %half<A>{$key} // %best{$key}; my $b = %half<B>{$key} // %best{$key};'
  echo '  my $d = ((($a max $b) / ($a min $b)) - 1) * 100;'
  echo '  $worst = $d if $d > $worst;'
  echo '  $fh.say: ($key, sprintf("%.1f", %best{$key}), sprintf("%.1f", $d)).join("\t");'
  echo '}'
  echo '$fh.close;'
  echo 'printf "  halves agree to within %.1f%% (>2%% means the floor has NOT converged)\n", $worst;'
} | sed "s#REPO#$WORKDIR/repo#" > "$WORKDIR/eras.raku"

ERA_ARGS=""
for v in $(echo "$TAGS" | awk '$3 != "" {print $3}' | sort -u); do
  rp=$(rakudo_for "$v")
  [ -x "$rp" ] || command -v "$rp" >/dev/null 2>&1 || continue
  ERA_ARGS="$ERA_ARGS $v=$rp"
done
# shellcheck disable=SC2086
"$DRIVER" "$WORKDIR/eras.raku" "$ERAS_OUT" "$PASSES" "$KERNELS" $ERA_ARGS

say ""
say "done."
say "  $(( $(wc -l < "$OUT") - 1 )) rows -> $OUT   (per-tag; rakudo column is ORACLE ONLY)"
say "  $(( $(wc -l < "$ERAS_OUT") - 1 )) rows -> $ERAS_OUT   (the rakudo reference, one per release)"
say "send back: $OUT, $ERAS_OUT and $WORKDIR/environment.txt"
