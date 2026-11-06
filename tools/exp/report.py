#!/usr/bin/env python3
"""Paired statistics for the Zen 5 A/B campaign.

    report.py RUN_DIR [RUN_DIR ...]

Reads rounds.jsonl produced by ab.sh (one JSON object per round, keyed by tag)
and reports, per metric, the paired relative change B/A with a bootstrap 95 %
confidence interval, plus a verdict against the pre-registered thresholds:

    throughput (rps, build wall):  needs >= +1.0 % and > 3 sigma
    latency (wakeup p99, cyclictest max, hackbench): needs <= -2.0 % and > 3 sigma

sigma comes from thresholds.json written by the noise-floor calibration; if it
is absent the report refuses to give a verdict rather than guessing.
"""
import glob
import json
import os
import random
import statistics
import sys

# metric -> (path, direction) ; direction +1 = higher is better
METRICS = {
    "rps":            (("schbench", "rps"), +1),
    "build_wall":     (("build", "wall"), -1),
    "wakeup_p99":     (("schbench", "wakeup_p99"), -1),
    "wakeup_p50":     (("schbench", "wakeup_p50"), -1),
    "req_p99":        (("schbench", "req_p99"), -1),
    "cyclictest_max": (("cyclictest", "max"), -1),
    "hackbench":      (("hackbench", "time"), -1),
    # mm campaign: anonymous fault cost and hugepage fallback behaviour.
    "fault4k_mean":   (("fault", "order0", "mean_ns"), -1),
    "fault4k_p99":    (("fault", "order0", "p99_ns"), -1),
    "faultthp_p99":   (("fault", "thp", "p99_ns"), -1),
    # Exact /proc/vmstat deltas over the round: load-independent, and the
    # only evidence the mm gates trust on a machine that is never quiet.
    "thp_alloc":      (("vmstat", "thp_fault_alloc"), +1),
    "thp_fallback":   (("vmstat", "thp_fault_fallback"), -1),
    "compact_stall":  (("vmstat", "compact_stall"), -1),
    "pgscan_direct":  (("vmstat", "pgscan_direct"), -1),
}

THRESH_PCT = {"throughput": 1.0, "latency": 2.0}
THRESH_SIGMA = 3.0


def dig(d, path):
    for k in path:
        if not isinstance(d, dict) or k not in d:
            return None
        d = d[k]
    return d if isinstance(d, (int, float)) else None


def load_rounds(run):
    rows = []
    with open(os.path.join(run, "rounds.jsonl")) as fh:
        for line in fh:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    return rows


def bootstrap_ci(deltas, n=4000, alpha=0.05):
    if len(deltas) < 3:
        return (None, None)
    rng = random.Random(1234)
    means = []
    for _ in range(n):
        s = [rng.choice(deltas) for _ in deltas]
        means.append(statistics.fmean(s))
    means.sort()
    return means[int(alpha / 2 * n)], means[int((1 - alpha / 2) * n) - 1]


def thresholds_path():
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(here, "..", "..", ".logs", "exp", "thresholds.json")


def main(runs):
    th = None
    tp = thresholds_path()
    if os.path.exists(tp):
        th = json.load(open(tp))

    print("%-16s %8s %8s %10s %10s %s" % ("metric", "A", "B", "delta%", "95% CI", "verdict"))
    for run in runs:
        rows = load_rounds(run)
        tags = sorted({t for r in rows for t in r})
        if len(tags) != 2:
            print("%s: expected 2 tags, got %s" % (run, tags))
            continue
        a, b = tags
        print("== %s (A=%s B=%s, %d rounds)" % (os.path.basename(run), a, b, len(rows)))
        for name, (path, better) in METRICS.items():
            pairs = []
            for r in rows:
                av, bv = dig(r.get(a, {}), path), dig(r.get(b, {}), path)
                if av and bv:
                    pairs.append((av, bv))
            if len(pairs) < 3:
                continue
            deltas = [(bv - av) / av * 100.0 for av, bv in pairs]
            med = statistics.median(deltas)
            lo, hi = bootstrap_ci(deltas)
            kind = "throughput" if better > 0 else "latency"
            sigma = None
            if th and name in th:
                sigma = th[name].get("sigma_pct")
            verdict = "inconclusive"
            if sigma is not None:
                sig = abs(med) > THRESH_SIGMA * sigma
                mag = abs(med) >= THRESH_PCT[kind]
                direction_ok = (med > 0) if better > 0 else (med < 0)
                if sig and mag and direction_ok:
                    verdict = "WIN" if better > 0 else "WIN"
                elif sig and mag:
                    verdict = "LOSS"
                elif not sig:
                    verdict = "below noise"
            ci = "n/a" if lo is None else "%+.2f..%+.2f" % (lo, hi)
            print("%-16s %8.2f %8.2f %+9.2f%% %10s %s%s" % (
                name,
                statistics.median([p[0] for p in pairs]),
                statistics.median([p[1] for p in pairs]),
                med, ci, verdict,
                "" if sigma is None else " (sigma %.2f%%)" % sigma))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    main(sys.argv[1:])
