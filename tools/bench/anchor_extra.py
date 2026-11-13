#!/usr/bin/env python3
# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
"""The anchor's reported (not gated) overhead metrics, paired by seed.

docs/fidelity.md "Anchor candidate" gates NRL (routing packets per delivered
data packet) and reports two more next to it, because NRL alone does not
establish [G02]'s orderings: the raw number of routing packets sent (Fig. 8
plots counts) and `nrl_bytes`, routing bytes per delivered data byte (the
nearest to Fig. 6's routing bits per data bit). `ns3-campaign verdicts`
prints PDR, p99 delay and NRL only, so this prints the other two, for a
protocol against a base protocol in the same cell, seed by seed:

    anchor_extra.py [--proto ara] [--base aodv] CELL...

One line per cell file (both seed blocks in one file, as `ns3-campaign
collect` writes them): each protocol's mean, and the paired 95 % CI and
two-sided Wilcoxon p of PROTO minus BASE, from swarm-routing/ns3-tools'
stats_util (the t interval and Wilcoxon `ns3-campaign verdicts` uses), so
the statistics are the org's, not this script's.

- routing packets: `ctrlTx=` on the harness's `# diag <arm> seed=<n> ...`
  lines (control packets the arm transmitted in the run);
- nrl_bytes: the last field of `##RUN## <seed> <arm> pdr delay delay99
  thrput nrl jitter off50 off90 nrl_bytes` (ns3-tools docs/markers.md).

stats_util is found at $NS3_TOOL_PY, else ../ns3-tools/python/ns3_tool next
to this repository. `--self-test` runs on fixtures.
"""

import argparse
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))


def load_stats():
    path = os.environ.get("NS3_TOOL_PY") or os.path.join(
        os.path.dirname(REPO), "ns3-tools", "python", "ns3_tool"
    )
    if not os.path.isfile(os.path.join(path, "stats_util.py")):
        sys.exit(f"no stats_util.py in {path}: set NS3_TOOL_PY to ns3-tools' python/ns3_tool")
    sys.path.insert(0, path)
    import stats_util  # noqa: PLC0415 (found at run time, by design)

    return stats_util


def read_cell(path):
    """{arm: {seed: {"ctrlTx": float, "nrl_bytes": float}}} from one cell file."""
    runs = {}
    with open(path) as f:
        for line in f:
            if line.startswith("##RUN## "):
                tok = line.split()
                if len(tok) < 12:
                    sys.exit(f"{path}: a ##RUN## line without nrl_bytes: {line.strip()}")
                seed, arm = int(tok[1]), tok[2]
                runs.setdefault(arm, {}).setdefault(seed, {})["nrl_bytes"] = float(tok[11])
            elif line.startswith("# diag "):
                tok = line.split()
                arm = tok[2]
                kv = dict(t.split("=", 1) for t in tok[3:] if "=" in t)
                if "seed" in kv and "ctrlTx" in kv:
                    runs.setdefault(arm, {}).setdefault(int(kv["seed"]), {})["ctrlTx"] = float(
                        kv["ctrlTx"]
                    )
    return runs


def compare(stats_util, runs, proto, base):
    """{metric: (mean proto, mean base, n, dmean, lo, hi, p)} over common seeds."""
    out = {}
    for metric in ("ctrlTx", "nrl_bytes"):
        a, b = runs.get(proto, {}), runs.get(base, {})
        seeds = sorted(s for s in set(a) & set(b) if metric in a[s] and metric in b[s])
        if len(seeds) < 2:
            continue
        xa = [a[s][metric] for s in seeds]
        xb = [b[s][metric] for s in seeds]
        diffs = [p - q for p, q in zip(xa, xb)]
        dmean, hw = stats_util.t_ci(diffs)
        _, _, p, _ = stats_util.wilcoxon(diffs)
        out[metric] = (
            sum(xa) / len(xa),
            sum(xb) / len(xb),
            len(seeds),
            dmean,
            dmean - hw,
            dmean + hw,
            p,
        )
    return out


def line(name, proto, base, res):
    parts = []
    for metric, fmt in (("ctrlTx", "{:.1f}"), ("nrl_bytes", "{:.4f}")):
        if metric not in res:
            parts.append(f"{metric} -")
            continue
        ma, mb, n, d, lo, hi, p = res[metric]
        f = fmt.format
        parts.append(
            f"{metric} {proto} {f(ma)} {base} {f(mb)}: d {f(d)} [{f(lo)},{f(hi)}] (n={n}) p={p:.3g}"
        )
    return f"{name}\t{proto} - {base}\t" + "  ".join(parts)


def self_test():
    stats_util = load_stats()
    cell = "##CONFIG## scenario=paper protocols=ara,aodv\n" + "".join(
        f"##RUN## {s} ara 80 3 20 5 2.0 2 1 inf {0.9 + s / 100:.4f}\n"
        f"##RUN## {s} aodv 90 3 40 5 52.0 2 1 inf {1.6 + s / 50:.4f}\n"
        f"# diag ara seed={s} pdr=80 ctrlTx={13000 + 10 * s} flowsNoDelivery=0\n"
        f"# diag aodv seed={s} pdr=90 ctrlTx={60000 + 30 * s} flowsNoDelivery=0\n"
        for s in range(1, 21)
    )
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "cell.txt")
        with open(p, "w") as f:
            f.write(cell)
        runs = read_cell(p)
        assert sorted(runs) == ["aodv", "ara"] and len(runs["ara"]) == 20, runs.keys()
        res = compare(stats_util, runs, "ara", "aodv")
        ma, mb, n, dm, lo, hi, pv = res["ctrlTx"]
        assert n == 20 and hi < 0 and abs(ma - 13105.0) < 1e-9, res["ctrlTx"]
        ma, mb, n, dm, lo, hi, pv = res["nrl_bytes"]
        assert n == 20 and hi < 0, res["nrl_bytes"]
        out = line("cell", "ara", "aodv", res)
        assert (
            out.startswith("cell\tara - aodv\tctrlTx ara 13105.0 aodv 60315.0") and "(n=20)" in out
        ), out
        # A ##RUN## line cut short is refused, never read as a value.
        bad = os.path.join(d, "bad.txt")
        with open(bad, "w") as f:
            f.write("##RUN## 1 ara 80 3 20 5 2.0\n")
        try:
            read_cell(bad)
        except SystemExit as e:
            assert "without nrl_bytes" in str(e)
        else:
            raise AssertionError("a short ##RUN## line was read")
    print("anchor_extra: self-test ok")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("cells", nargs="*")
    ap.add_argument("--proto", default="ara")
    ap.add_argument("--base", default="aodv")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test()
    if not a.cells:
        ap.error("give one or more cell files")
    stats_util = load_stats()
    for path in a.cells:
        name = os.path.basename(path).removesuffix(".txt")
        print(line(name, a.proto, a.base, compare(stats_util, read_cell(path), a.proto, a.base)))
    return None


if __name__ == "__main__":
    sys.exit(main())
