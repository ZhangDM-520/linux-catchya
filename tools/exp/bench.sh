#!/usr/bin/env bash
# One measurement point for the Zen 5 A/B campaign. Emits JSON.
#
#   bench.sh --tag NAME [--out FILE] [--secs N] [--no-build] [--perf]
#
# Design notes
#   - Every workload is fixed in shape; only the kernel under test changes.
#   - The environment (governor, EPP, boost, scx state, load, thermals) is
#     recorded per round so a confounded round can be discarded afterwards
#     rather than silently averaged in.
#   - The build workload rebuilds a fixed object set from a pre-warmed tree;
#     it measures compile throughput, not correctness.
set -uo pipefail

EXP_DIR="${EXP_DIR:-$HOME/Projects/linux-catchya/.logs/exp}"
BUILD_TREE="${BUILD_TREE:-$HOME/Workspace/exp/bt}"
TAG=""; OUT=""; SECS=30; DO_BUILD=1; DO_PERF=0; DO_FUTEX=0
# Pinning the benchmark keeps desktop activity off the measured CPUs; without
# it the wakeup-latency noise exceeds any plausible kernel effect.
BENCH_CPUS="${BENCH_CPUS:-0-11}"
# Thread count follows the pinned set, not nproc: oversubscribing 12 CPUs with
# 24 threads measures queueing, not the kernel.
BENCH_THREADS=$(python3 -c "import sys
s=sys.argv[1]; n=0
for part in s.split(','):
    a,_,b = part.partition('-')
    n += (int(b)-int(a)+1) if b else 1
print(n)" "$BENCH_CPUS")

while [ $# -gt 0 ]; do
	case "$1" in
		--tag)      TAG="$2"; shift 2 ;;
		--out)      OUT="$2"; shift 2 ;;
		--secs)     SECS="$2"; shift 2 ;;
		--no-build) DO_BUILD=0; shift ;;
		--perf)     DO_PERF=1; shift ;;
		--futex)    DO_FUTEX=1; shift ;;
		*) echo "unknown arg $1" >&2; exit 2 ;;
	esac
done
[ -n "$TAG" ] || { echo "--tag required" >&2; exit 2; }
[ -n "$OUT" ] || OUT="$EXP_DIR/raw/$(date +%Y%m%dT%H%M%S)-$TAG.json"
mkdir -p "$(dirname "$OUT")"

TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT

temp_now() { cat /sys/class/hwmon/hwmon6/temp1_input 2>/dev/null || echo 0; }
load_now() { cut -d' ' -f1 /proc/loadavg; }

# ---------------------------------------------------------------- environment
{
	echo "load=$(load_now)"
	echo "temp_c=$(( $(temp_now) / 1000 ))"
	echo "governor=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null)"
	echo "epp=$(cat /sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference 2>/dev/null)"
	echo "boost=$(cat /sys/devices/system/cpu/cpufreq/boost 2>/dev/null)"
	echo "scx=$(dmesg 2>/dev/null | grep -o 'sched_ext: scheduler [a-z_]*' | tail -1 | awk '{print $3}')"
	echo "kernel=$(uname -r)"
	echo "on_ac=$(cat /sys/class/power_supply/AC0/online 2>/dev/null)"
} > "$TMP/env.txt"

# ------------------------------------------------------------------- schbench
# -t = threads (one per CPU), -r = seconds. Both wakeup and request latencies
# are reported by schbench; wakeup p99 is the interactive number.
schbench -m 1 -t "$BENCH_THREADS" -r "$SECS" -W "$BENCH_CPUS" > "$TMP/schbench.txt" 2>&1

# ---------------------------------------------------------------- cyclictest
# A short run: the max latency is what interactive audio/input cares about.
sudo -n cyclictest -q -m -t "$BENCH_THREADS" -a "$BENCH_CPUS" -p 95 -i 200 -D "${SECS}s" > "$TMP/cyclictest.txt" 2>&1

# ----------------------------------------------------------------- hackbench
# 24 groups x 24 fds: a few seconds on this machine, socket-bound.
hb_start=$(date +%s.%N)
hackbench -s 1024 -l 1000 -g 24 -f 24 -P > "$TMP/hackbench.txt" 2>&1
hb_end=$(date +%s.%N)
echo "wall=$(echo "$hb_end - $hb_start" | bc)" > "$TMP/hackbench_time.txt"

# ------------------------------------------------------------ build workload
if [ "$DO_BUILD" = 1 ] && [ -d "$BUILD_TREE" ]; then
	# kernel/sched + mm only: ~18 s of real compile work. fs/ was measured at
	# 140 s and made a round too long and too drift-prone.
	find "$BUILD_TREE/kernel/sched" "$BUILD_TREE/mm" -name '*.o' -delete 2>/dev/null
	bt_start=$(date +%s.%N)
	make -s -C "$HOME/Projects/linux-catchya" O="$BUILD_TREE" LLVM=1 LLVM_IAS=1 \
		-j"$(nproc)" kernel/sched/ mm/ > "$TMP/build.txt" 2>&1
	bt_end=$(date +%s.%N)
	echo "wall=$(echo "$bt_end - $bt_start" | bc)" > "$TMP/build_time.txt"
fi

# ----------------------------------------------------------- futex wake cost
# Wake-to-run latency of a futex pair. The waker and the waiter are pinned to
# different CPUs on purpose: the number is the cross-CPU path, which is what a
# cache-affine policy would change.
if [ "$DO_FUTEX" = 1 ]; then
	FUTEX_BIN="$EXP_DIR/bin/futex_ping"
	mkdir -p "$EXP_DIR/bin"
	cc -O2 -o "$FUTEX_BIN" \
		"$HOME/Projects/linux-catchya/tools/exp/futex_ping.c" || exit 1
	"$FUTEX_BIN" --iters 2000 \
		--waker-cpu "${FUTEX_WAKER_CPU:-0}" \
		--waiter-cpu "${FUTEX_WAITER_CPU:-1}" \
		--json > "$TMP/futex.json"
fi

# --------------------------------------------------------------- perf counters
if [ "$DO_PERF" = 1 ]; then
	sudo -n perf stat -x, \
		-e cycles,instructions,ic_tag_hit_miss.instruction_cache_miss,de_src_op_disp.op_cache \
		-o "$TMP/perf.csv" -- sleep "$SECS" > /dev/null 2>&1
fi

# ---------------------------------------------------------------------- emit
TAG="$TAG" OUT="$OUT" TMP="$TMP" python3 - <<'PY'
import json, os, re, subprocess

tmp = os.environ["TMP"]
def read(name):
    try:
        return open(os.path.join(tmp, name)).read()
    except OSError:
        return ""

def num(text, pattern, group=1, cast=float):
    m = re.search(pattern, text)
    return cast(m.group(group)) if m else None

env = dict(l.split("=", 1) for l in read("env.txt").split("\n") if "=" in l)

sch = read("schbench.txt")
# schbench can print the "Request Latencies" banner more than once; split on
# the first occurrence instead of unpacking split(), which assumes exactly one.
_i = sch.find("Request Latencies")
wake, req = (sch[:_i], sch[_i:]) if _i >= 0 else (sch, "")
schb = {
    "wakeup_p50":  num(wake, r"50\.0th:\s*(\d+)", cast=int),
    "wakeup_p99":  num(wake, r"\*\s*99\.0th:\s*(\d+)", cast=int),
    "wakeup_max":  num(wake, r"max=(\d+)", cast=int),
    "req_p50":     num(req, r"50\.0th:\s*(\d+)", cast=int),
    "req_p99":     num(req, r"\*\s*99\.0th:\s*(\d+)", cast=int),
    "req_p999":    num(req, r"99\.9th:\s*(\d+)", cast=int),
    "rps":         num(sch, r"average rps:\s*([\d.]+)"),
}

cyc = read("cyclictest.txt")
maxes = [int(m) for m in re.findall(r"Max:\s*(\d+)", cyc)]
avgs  = [int(m) for m in re.findall(r"Avg:\s*(\d+)", cyc)]
cycli = {
    "max": max(maxes) if maxes else None,
    "avg": (sum(avgs) / len(avgs)) if avgs else None,
}

hack = read("hackbench.txt")
hb = {"time": num(hack, r"Time:\s*([\d.]+)"),
      "wall": num(read("hackbench_time.txt"), r"wall=([\d.]+)")}

build = {"wall": num(read("build_time.txt"), r"wall=([\d.]+)")}

perf = {}
for line in read("perf.csv").split("\n"):
    f = line.split(",")
    if len(f) >= 3 and f[2]:
        try:
            perf[f[2]] = float(f[0].replace(" ", ""))
        except ValueError:
            pass

def jload(name):
    try:
        return json.loads(read(name))
    except (OSError, ValueError):
        return {}

out = {
    "tag": os.environ["TAG"],
    "ts": subprocess.check_output(["date", "-Is"]).decode().strip(),
    "env": env,
    "schbench": schb,
    "cyclictest": cycli,
    "hackbench": hb,
    "build": build,
    "perf": perf,
    "futex": jload("futex.json"),
}
path = os.environ["OUT"]
os.makedirs(os.path.dirname(path), exist_ok=True)
with open(path, "w") as f:
    json.dump(out, f, indent=1)
print(json.dumps({k: out[k] for k in ("tag", "schbench", "cyclictest", "hackbench", "build")}))
PY
