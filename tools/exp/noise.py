#!/usr/bin/env python3
"""Derive the noise floor from a baseline-vs-baseline A/B run.

    noise.py RUN_DIR

For every metric, sigma is the standard deviation of the per-round relative
difference between the two identical-kernel labels. A campaign whose effect is
not several times larger than this number is not measuring the kernel.
"""
import json
import os
import statistics
import sys

from report import METRICS, dig, load_rounds


def main(run):
    rows = load_rounds(run)
    tags = sorted({t for r in rows for t in r})
    a, b = tags[0], tags[1]
    out = {"source": run, "rounds": len(rows), "a": a, "b": b, "metrics": {}}
    print("%-16s %10s %10s %10s" % ("metric", "sigma%", "median%", "max%"))
    for name, (path, _) in METRICS.items():
        deltas = []
        for r in rows:
            av, bv = dig(r.get(a, {}), path), dig(r.get(b, {}), path)
            if av and bv:
                deltas.append((bv - av) / av * 100.0)
        if len(deltas) < 3:
            continue
        sigma = statistics.stdev(deltas)
        out["metrics"][name] = {
            "sigma_pct": round(sigma, 3),
            "median_pct": round(statistics.median(deltas), 3),
            "max_abs_pct": round(max(abs(d) for d in deltas), 3),
        }
        print("%-16s %10.2f %10.2f %10.2f" % (
            name, sigma, statistics.median(deltas), max(abs(d) for d in deltas)))
    dest = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "..", "..", ".logs", "exp", "thresholds.json")
    with open(dest, "w") as fh:
        json.dump(out, fh, indent=1)
    print("\nwritten: %s" % dest)
    worst = max((m["sigma_pct"], n) for n, m in out["metrics"].items())
    print("worst metric: %s at sigma %.2f%% -> a 1%% claim needs sigma well below 0.33%%"
          % (worst[1], worst[0]))


if __name__ == "__main__":
    main(sys.argv[1])
