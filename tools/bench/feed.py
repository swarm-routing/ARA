#!/usr/bin/env python3
# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
"""Feeds campaign plans to completion under the cap, and stops if the cell
workflow's dependencies move (#289).

`ns3-campaign dispatch` sends cells until the cap and exits; a campaign of 40
cells at cap 4 needs it re-run for hours. This does that, for one or more
plans sharing the cap, one line per plan per round:

    python3 -I tools/bench/feed.py PLAN [PLAN...] [--cap 4] [--every 600]

Each round it fetches the plans' dispatch ref, and stops (exit 3) when the
dependency commits `.github/workflows/benchmark-cell.yml` names there differ
from those of the first round: cells dispatched after such a move build on
another dependency set, which `ns3-campaign collect` rejects (#288). Then,
per plan not done: `dispatch PLAN --cap N`, `status PLAN --brief`. A plan
with no cell queued, running or not dispatched is collected into
`.campaign/<generation>/cells` and its cells checked with
check_plan_deps.py's `--cells` check; its last line is printed. Exit 0 once
every plan is collected, 2 after `--rounds` rounds with work left.

PLAN is a path inside this repository (check_plan_deps.py's rule, #126).
`ns3-campaign` is $NS3_CAMPAIGN (a command line), else
../ns3-tools/python/ns3_tool/campaign.py (or ../sim-tools/...) next to this
repository. Standard library, plus PyYAML for a .yaml plan (as ns3-campaign).

    python3 -I tools/bench/feed.py --self-test
"""

import argparse
import importlib.util
import os
import re
import shlex
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
BUSY = re.compile(r"queued|in_progress|not-dispatched")


def plan_deps_module():
    """check_plan_deps.py, by path (python3 -I keeps this directory off sys.path)."""
    sys.dont_write_bytecode = True  # no __pycache__ beside the scripts
    spec = importlib.util.spec_from_file_location(
        "check_plan_deps", os.path.join(HERE, "check_plan_deps.py")
    )
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def campaign_command():
    if os.environ.get("NS3_CAMPAIGN"):
        return shlex.split(os.environ["NS3_CAMPAIGN"])
    for name in ("ns3-tools", "sim-tools"):
        path = os.path.join(os.path.dirname(REPO), name, "python", "ns3_tool", "campaign.py")
        if os.path.isfile(path):
            return [sys.executable, path]
    sys.exit("feed: no ns3-campaign: set NS3_CAMPAIGN, or check out ns3-tools next to this repository")


class Live:
    """The real world: ns3-campaign, git and the clock."""

    def __init__(self, root):
        self.root = root
        self.cmd = campaign_command()

    def campaign(self, *args):
        p = subprocess.run(
            [*self.cmd, *args], cwd=self.root, capture_output=True, text=True, check=False
        )
        return p.returncode, (p.stdout + p.stderr).strip().splitlines()

    def deps(self, cpd, ref):
        subprocess.run(["git", "-C", self.root, "fetch", "-q", "origin", ref], check=False)
        wf = cpd.git(self.root, "show", f"origin/{ref}:.github/workflows/benchmark-cell.yml")
        return tuple(sorted(cpd.DEP_LINE.findall(wf or "")))

    def sleep(self, seconds):
        time.sleep(seconds)


def feed(root, plans, cap, every, rounds, world, out=print):
    cpd = plan_deps_module()
    todo = {}
    for p in plans:
        real = cpd.inside(root, p)
        plan, why = cpd.load_plan(real) if real and os.path.isfile(real) else (None, "not a plan file")
        if plan is None or "generation" not in plan:
            out(f"feed: {p}: {why or 'no generation'}")
            return 1
        todo[p] = plan
    first = None
    for _ in range(rounds):
        for ref in sorted({str(pl.get("ref") or "main") for pl in todo.values()}):
            deps = world.deps(cpd, ref)
            first = first or {}
            if first.setdefault(ref, deps) != deps:
                out(f"feed: the dependencies at origin/{ref} moved; stop (one set per campaign, #288)")
                return 3
        for p in list(todo):
            world.campaign("dispatch", p, "--cap", str(cap))
            _rc, lines = world.campaign("status", p, "--brief")
            last = lines[-1] if lines else "?"
            out(f"{time.strftime('%H:%M', time.gmtime())} {todo[p]['generation']}: {last}")
            if BUSY.search(last) or last == "?":
                continue
            cells = os.path.join(".campaign", str(todo.pop(p)["generation"]), "cells")
            _rc, got = world.campaign("collect", p, "--out", cells)
            out(f"  collect: {got[-1] if got else '?'}")
            lines = []
            cpd.check(root, p, cells=cells, out=lines.append)
            out(f"  {lines[-1] if lines else 'no check output'}")
        if not todo:
            return 0
        world.sleep(every)
    return 2


def self_test():
    import tempfile

    failures = []

    class Fake:
        def __init__(self, statuses, deps):
            self.statuses, self.deps_seq, self.calls, self.slept = statuses, deps, [], 0

        def campaign(self, *args):
            self.calls.append(args)
            if args[0] == "status":
                return 0, ["...", self.statuses[args[1]].pop(0)]
            return 0, ["wrote 2 file(s), skipped 0"]

        def deps(self, _cpd, _ref):
            return self.deps_seq.pop(0) if len(self.deps_seq) > 1 else self.deps_seq[0]

        def sleep(self, _s):
            self.slept += 1

    with tempfile.TemporaryDirectory() as tmp:
        for name in ("a", "b"):
            with open(os.path.join(tmp, f"{name}.json"), "w") as f:
                f.write(f'{{"generation": "g-{name}", "ref": "main"}}')
        lines = []
        world = Fake(
            {"a.json": ["2 in_progress, 1 success", "3 success"],
             "b.json": ["1 not-dispatched", "1 queued", "2 success"]},  # fmt: skip
            [("x",)],
        )
        rc = feed(tmp, ["a.json", "b.json"], 4, 0, 5, world, lines.append)
        if rc != 0 or world.slept != 2:
            failures.append(f"two plans to completion: rc {rc}, slept {world.slept}: {lines}")
        collected = [c for c in world.calls if c[0] == "collect"]
        if [c[1] for c in collected] != ["a.json", "b.json"] or collected[0][3] != os.path.join(
            ".campaign", "g-a", "cells"
        ):
            failures.append(f"each collected once, into .campaign/<generation>/cells: {collected}")
        if any(c[0] == "dispatch" and c[1] == "a.json" for c in world.calls[6:]):
            failures.append(f"a collected plan is not dispatched again: {world.calls}")
        lines = []
        world = Fake({"a.json": ["1 in_progress"] * 5}, [("x",), ("y",)])
        rc = feed(tmp, ["a.json"], 4, 0, 5, world, lines.append)
        if rc != 3 or not any("moved" in x for x in lines):
            failures.append(f"a dependency move stops it: rc {rc}: {lines}")
        lines = []
        world = Fake({"a.json": ["1 in_progress"] * 3}, [("x",)])
        rc = feed(tmp, ["a.json"], 4, 0, 3, world, lines.append)
        if rc != 2:
            failures.append(f"rounds exhausted: rc {rc}")
        lines = []
        rc = feed(tmp, ["../x.json"], 4, 0, 1, Fake({}, [("x",)]), lines.append)
        if rc != 1:
            failures.append(f"a plan outside the repository is refused: rc {rc}: {lines}")
    for f in failures:
        print(f"FAIL {f}")
    print(f"self-test: {'FAIL' if failures else 'ok'} (6 cases)")
    return 1 if failures else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("plans", nargs="*", help="campaign plans inside this repository")
    ap.add_argument("--cap", type=int, default=4, help="cell runs at once, all plans (default 4)")
    ap.add_argument("--every", type=int, default=600, help="seconds between rounds (default 600)")
    ap.add_argument("--rounds", type=int, default=80, help="rounds before giving up (default 80)")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    if not a.plans:
        ap.error("a plan is required")
    return feed(REPO, a.plans, a.cap, a.every, a.rounds, Live(REPO))


if __name__ == "__main__":
    sys.exit(main())
