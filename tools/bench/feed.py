#!/usr/bin/env python3
# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
"""Feeds campaign plans to completion under the cap, and stops if the cell
workflow's dependencies move (#289).

`ns3-campaign dispatch` sends cells until the cap and exits; a campaign of 40
cells at cap 4 needs it re-run for hours. This does that, for one or more
plans sharing the cap, one line per plan per round:

    python3 -I tools/bench/feed.py PLAN [PLAN...] [--cap 4] [--every 600]

First, check_plan_deps.py runs on every plan; a failure dispatches nothing
(exit 1) unless `--allow-plan-deps-fail` (a change test whose base predates
a pin bump fails it by design, AGENTS.md). Each round it fetches the plans'
dispatch ref, and stops (exit 3) when the dependency commits
`.github/workflows/benchmark-cell.yml` names there differ from those of the
first round, or (exit 4) when the fetch fails or the workflow names none:
cells dispatched after such a move build on another dependency set, which
`ns3-campaign collect` rejects (#288). Then, per plan not done: `dispatch
PLAN --cap N`, `status PLAN --brief`. A plan with no cell queued, running or
not dispatched is collected into `.campaign/<generation>/cells`; a collect
that fails or skips a cell keeps the plan for the next round (the files it
wrote stay). A complete one gets check_plan_deps.py's `--cells` check, whose
last line is printed. Exit 0 once every plan is collected and checked ok, 1
if a cell check failed, 2 after `--rounds` rounds with work left. A plan
with `freeze: true` whose state records no frozen ref after a dispatch (an
ns3-campaign from before swarm-routing/sim-tools#368 ignores the key and
dispatches at the branch) stops it at once, exit 5 (#125).

PLAN is a path inside this repository (check_plan_deps.py's rule, #126).
`ns3-campaign` is $NS3_CAMPAIGN (a command line), else
../ns3-tools/python/ns3_tool/campaign.py (or ../sim-tools/...) next to this
repository. Standard library, plus PyYAML for a .yaml plan (as ns3-campaign).

    python3 -I tools/bench/feed.py --self-test
"""

import argparse
import importlib.util
import json
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
    sys.exit(
        "feed: no ns3-campaign: set NS3_CAMPAIGN, or check out ns3-tools next to this repository"
    )


class Live:
    """The real world: ns3-campaign, git, the plan check and the clock."""

    def __init__(self, root):
        self.root = root
        self.cmd = campaign_command()

    def campaign(self, *args):
        p = subprocess.run(
            [*self.cmd, *args], cwd=self.root, capture_output=True, text=True, check=False
        )
        return p.returncode, (p.stdout + p.stderr).strip().splitlines()

    def deps(self, cpd, ref):
        """The workflow's dependency commits at origin/<ref>, fetched now; None
        when the fetch fails or the workflow cannot be read (never a stale or
        empty set)."""
        fetched = subprocess.run(  # noqa: S603 (git, a ref from the plan, no shell)
            ["git", "-C", self.root, "fetch", "-q", "origin", "--", ref],
            capture_output=True, check=False,
        )  # fmt: skip
        if fetched.returncode != 0:
            return None
        wf = cpd.git(self.root, "show", f"origin/{ref}:.github/workflows/benchmark-cell.yml")
        found = cpd.DEP_LINE.findall(wf or "")
        return tuple(sorted(found)) if found else None

    def check(self, cpd, plan, cells=None):
        lines = []
        return cpd.check(self.root, plan, cells=cells, out=lines.append), lines

    def frozen(self, generation):
        """The tag the campaign's state.json records as its frozen ref, or None."""
        path = os.path.join(self.root, ".campaign", generation, "state.json")
        try:
            with open(path) as f:
                ref = json.load(f).get("ref")
        except (OSError, ValueError):
            return None
        return ref.get("tag") if isinstance(ref, dict) else None

    def sleep(self, seconds):
        time.sleep(seconds)


COLLECTED = re.compile(r"wrote (\d+) file\(s\), skipped (\d+)")


def feed(root, plans, cap, every, rounds, world, out=None, allow_plan_fail=False):
    """0: every plan collected and its cells checked ok; 1: a plan or a cell
    check failed; 2: rounds ran out; 3: the dependencies moved; 4: they could
    not be read; 5: a `freeze: true` plan was dispatched unfrozen."""
    out = out or (lambda line: print(line, flush=True))  # a line per round, as it happens
    cpd = plan_deps_module()
    todo = {}
    for p in plans:
        real = cpd.inside(root, p)
        plan, why = (
            cpd.load_plan(real) if real and os.path.isfile(real) else (None, "not a plan file")
        )
        if plan is None or "generation" not in plan:
            out(f"feed: {p}: {why or 'no generation'}")
            return 1
        # check_plan_deps before the first dispatch (#105); a known failure (a
        # change test whose base predates a pin bump) needs --allow-plan-deps-fail.
        fails, lines = world.check(cpd, p)
        if fails:
            for line in lines:
                out(f"  {line}")
            if not allow_plan_fail:
                out(
                    f"feed: {p}: plan deps FAIL; nothing dispatched (--allow-plan-deps-fail to proceed)"
                )
                return 1
            out(f"feed: {p}: plan deps FAIL, proceeding (--allow-plan-deps-fail)")
        todo[p] = plan
    first = {}
    failed = 0
    for _ in range(rounds):
        for ref in sorted({str(pl.get("ref") or "main") for pl in todo.values()}):
            deps = world.deps(cpd, ref)
            if deps is None:
                out(f"feed: cannot fetch or read the dependencies at origin/{ref}; stop")
                return 4
            if first.setdefault(ref, deps) != deps:
                out(
                    f"feed: the dependencies at origin/{ref} moved; stop (one set per campaign, #288)"
                )
                return 3
        for p in list(todo):
            world.campaign("dispatch", p, "--cap", str(cap))
            gen = str(todo[p]["generation"])
            if todo[p].get("freeze") is True and not world.frozen(gen):
                out(
                    f"feed: {p}: freeze: true, but .campaign/{gen}/state.json records no frozen "
                    "ref: this ns3-campaign ignores the key (swarm-routing/sim-tools#368); stop"
                )
                return 5
            _rc, lines = world.campaign("status", p, "--brief")
            last = lines[-1] if lines else "?"
            out(f"{time.strftime('%H:%M', time.gmtime())} {todo[p]['generation']}: {last}")
            if BUSY.search(last) or last == "?":
                continue
            cells = os.path.join(".campaign", str(todo[p]["generation"]), "cells")
            rc, got = world.campaign("collect", p, "--out", cells)
            done = next((m for m in map(COLLECTED.search, got) if m), None)
            out(f"  collect: {done.group(0) if done else (got[-1] if got else '?')}")
            if rc != 0 or done is None or done.group(2) != "0":
                out("  incomplete or rejected: kept for the next round")  # files kept
                continue
            todo.pop(p)
            fails, lines = world.check(cpd, p, cells)
            out(f"  {lines[-1] if lines else 'no check output'}")
            failed += 1 if fails else 0
        if not todo:
            return 1 if failed else 0
        world.sleep(every)
    return 2


def self_test():
    import tempfile

    failures = []

    class Fake:
        def __init__(self, statuses, deps, collects=None, plan_fails=0, cell_fails=0, tag=None):
            self.statuses, self.deps_seq, self.calls, self.slept = statuses, deps, [], 0
            self.tag = tag
            self.collects = collects or {}
            self.plan_fails, self.cell_fails = plan_fails, cell_fails

        def campaign(self, *args):
            self.calls.append(args)
            if args[0] == "status":
                return 0, ["...", self.statuses[args[1]].pop(0)]
            if args[0] == "collect":
                queue = self.collects.get(args[1])
                return queue.pop(0) if queue else (0, ["wrote 2 file(s), skipped 0"])
            return 0, []

        def deps(self, _cpd, _ref):
            return self.deps_seq.pop(0) if len(self.deps_seq) > 1 else self.deps_seq[0]

        def check(self, _cpd, _plan, cells=None):
            n = self.cell_fails if cells else self.plan_fails
            return n, ["FAIL x", "plan deps: FAIL (1)"] if n else ["plan deps: ok"]

        def sleep(self, _s):
            self.slept += 1

        def frozen(self, _generation):
            return self.tag

    def go(tmp, plans, world, rounds=5, **kw):
        lines = []
        return feed(tmp, plans, 4, 0, rounds, world, lines.append, **kw), lines

    with tempfile.TemporaryDirectory() as tmp:
        for name in ("a", "b"):
            with open(os.path.join(tmp, f"{name}.json"), "w") as f:
                f.write(f'{{"generation": "g-{name}", "ref": "main"}}')
        with open(os.path.join(tmp, "z.json"), "w") as f:
            f.write('{"generation": "g-z", "ref": "main", "freeze": true}')
        world = Fake(
            {
                "a.json": ["2 in_progress, 1 success", "3 success"],
                "b.json": ["1 not-dispatched", "1 queued", "2 success"],
            },  # fmt: skip
            [("x",)],
        )
        rc, lines = go(tmp, ["a.json", "b.json"], world)
        if rc != 0 or world.slept != 2:
            failures.append(f"two plans to completion: rc {rc}, slept {world.slept}: {lines}")
        collected = [c for c in world.calls if c[0] == "collect"]
        if [c[1] for c in collected] != ["a.json", "b.json"] or collected[0][3] != os.path.join(
            ".campaign", "g-a", "cells"
        ):
            failures.append(f"each collected once, into .campaign/<generation>/cells: {collected}")
        if any(c[0] == "dispatch" and c[1] == "a.json" for c in world.calls[6:]):
            failures.append(f"a collected plan is not dispatched again: {world.calls}")
        rc, lines = go(tmp, ["a.json"], Fake({"a.json": ["1 in_progress"] * 5}, [("x",), ("y",)]))
        if rc != 3 or not any("moved" in x for x in lines):
            failures.append(f"a dependency move stops it: rc {rc}: {lines}")
        rc, lines = go(tmp, ["a.json"], Fake({"a.json": ["1 in_progress"] * 5}, [None]))
        if rc != 4 or not any("cannot fetch" in x for x in lines):
            failures.append(f"unreadable dependencies stop it: rc {rc}: {lines}")
        rc, _ = go(tmp, ["a.json"], Fake({"a.json": ["1 in_progress"] * 3}, [("x",)]), rounds=3)
        if rc != 2:
            failures.append(f"rounds exhausted: rc {rc}")
        rc, lines = go(tmp, ["../x.json"], Fake({}, [("x",)]), rounds=1)
        if rc != 1:
            failures.append(f"a plan outside the repository is refused: rc {rc}: {lines}")
        world = Fake({"a.json": ["1 success"]}, [("x",)], plan_fails=1)
        rc, lines = go(tmp, ["a.json"], world)
        if rc != 1 or world.calls:
            failures.append(f"a failing plan check dispatches nothing: rc {rc}: {world.calls}")
        world = Fake({"a.json": ["1 success"]}, [("x",)], plan_fails=1)
        rc, lines = go(tmp, ["a.json"], world, allow_plan_fail=True)
        if rc != 0 or not any("proceeding" in x for x in lines):
            failures.append(f"--allow-plan-deps-fail proceeds: rc {rc}: {lines}")
        world = Fake(
            {"a.json": ["1 success", "1 success"]}, [("x",)],
            collects={"a.json": [(0, ["wrote 1 file(s), skipped 1"]), (0, ["wrote 2 file(s), skipped 0"])]},
        )  # fmt: skip
        rc, lines = go(tmp, ["a.json"], world)
        if rc != 0 or len([c for c in world.calls if c[0] == "collect"]) != 2:
            failures.append(f"a partial collect is kept and retried: rc {rc}: {lines}")
        rc, lines = go(tmp, ["a.json"], Fake({"a.json": ["1 success"]}, [("x",)], cell_fails=1))
        if rc != 1:
            failures.append(f"a failing cell check fails the run: rc {rc}: {lines}")
        world = Fake({"z.json": ["1 success"]}, [("x",)])
        rc, lines = go(tmp, ["z.json"], world)
        if rc != 5 or any(c[0] == "status" for c in world.calls):
            failures.append(f"freeze: true dispatched unfrozen stops at once: rc {rc}: {lines}")
        world = Fake({"z.json": ["1 success"]}, [("x",)], tag="campaign/g-z")
        rc, lines = go(tmp, ["z.json"], world)
        if rc != 0:
            failures.append(f"freeze: true with a frozen ref goes on: rc {rc}: {lines}")
    for f in failures:
        print(f"FAIL {f}")
    print(f"self-test: {'FAIL' if failures else 'ok'} (13 cases)")
    return 1 if failures else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("plans", nargs="*", help="campaign plans inside this repository")
    ap.add_argument("--cap", type=int, default=4, help="cell runs at once, all plans (default 4)")
    ap.add_argument("--every", type=int, default=600, help="seconds between rounds (default 600)")
    ap.add_argument("--rounds", type=int, default=80, help="rounds before giving up (default 80)")
    ap.add_argument(
        "--allow-plan-deps-fail", action="store_true",
        help="dispatch although check_plan_deps.py fails on a plan (a change test whose "
        "base predates a pin bump, AGENTS.md)",
    )  # fmt: skip
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    if not a.plans:
        ap.error("a plan is required")
    return feed(
        REPO, a.plans, a.cap, a.every, a.rounds, Live(REPO), allow_plan_fail=a.allow_plan_deps_fail
    )


if __name__ == "__main__":
    sys.exit(main())
