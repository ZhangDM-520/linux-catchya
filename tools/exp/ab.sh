#!/usr/bin/env bash
# Interleaved A/B driver: runs bench.sh alternately for two tags and keeps the
# pairs together so time-varying noise (thermal drift, background activity)
# hits both sides of a comparison.
#
#   ab.sh --a TAG_A --b TAG_B [--rounds N] [--secs S] [--no-build] [--perf]
#
# Round order alternates A,B then B,A so a slow monotonic drift cannot favour
# either side. The pair is written as a single line to the run's JSONL file.
set -uo pipefail

EXP_DIR="${EXP_DIR:-$HOME/Projects/linux-catchya/.logs/exp}"
BENCH="$HOME/Projects/linux-catchya/tools/exp/bench.sh"
A=""; B=""; ROUNDS=10; SECS=30; EXTRA=()

while [ $# -gt 0 ]; do
	case "$1" in
		--a) A="$2"; shift 2 ;;
		--b) B="$2"; shift 2 ;;
		--rounds) ROUNDS="$2"; shift 2 ;;
		--secs) SECS="$2"; shift 2 ;;
		--no-build) EXTRA+=(--no-build); shift ;;
		--perf) EXTRA+=(--perf); shift ;;
		*) echo "unknown arg $1" >&2; exit 2 ;;
	esac
done
[ -n "$A" ] && [ -n "$B" ] || { echo "--a and --b required" >&2; exit 2; }

# One A/B run at a time: two concurrent runs share the machine and the build
# tree and silently produce garbage (learned the hard way).
exec 9>"$EXP_DIR/.ab.lock"
flock -n 9 || { echo "another ab.sh run holds $EXP_DIR/.ab.lock" >&2; exit 3; }

RUN="$EXP_DIR/raw/ab-$(date +%Y%m%dT%H%M%S)-${A}-vs-${B}"
mkdir -p "$RUN"
echo "{\"a\":\"$A\",\"b\":\"$B\",\"rounds\":$ROUNDS,\"secs\":$SECS,\"kernel\":\"$(uname -r)\"}" > "$RUN/meta.json"

for r in $(seq 1 "$ROUNDS"); do
	if [ $((r % 2)) -eq 1 ]; then order=("$A" "$B"); else order=("$B" "$A"); fi
	for tag in "${order[@]}"; do
		out="$RUN/${tag}-r${r}.json"
		"$BENCH" --tag "$tag" --secs "$SECS" "${EXTRA[@]}" --out "$out" \
			> "$RUN/${tag}-r${r}.log" 2>&1
		if [ ! -f "$out" ]; then
			echo "FATAL: bench.sh produced no JSON for $tag round $r (see $RUN/${tag}-r${r}.log)" >&2
			exit 4
		fi
	done
	python3 - "$RUN" "$r" <<'PY'
import json, sys, glob, os
run, rnd = sys.argv[1], sys.argv[2]
out = {}
for f in glob.glob(os.path.join(run, "*-r%s.json" % rnd)):
    d = json.load(open(f))
    out[d["tag"]] = d
with open(os.path.join(run, "rounds.jsonl"), "a") as fh:
    fh.write(json.dumps(out) + "\n")
print("round %s: %s" % (rnd, " ".join(
    "%s p99=%s rps=%s" % (t, d["schbench"]["wakeup_p99"], d["schbench"]["rps"])
    for t, d in sorted(out.items()))))
PY
done

echo "raw: $RUN"
