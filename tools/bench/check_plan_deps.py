#!/usr/bin/env python3
# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
"""A campaign plan and the cell workflow's dependencies name the same commits (#105).

A cell checks out the plan's `source_sha`, but builds its dependencies
(swarm-core, routing-bench) at the commits `.github/workflows/benchmark-cell.yml`
names at the dispatch ref, not at anything the plan says. `ns3-campaign collect`
checks `source_sha` and the image only. So a plan can describe one swarm-core
commit and run on another, with nothing refusing it. Run this before a
dispatch, and again on the collected cells:

    python3 -I tools/bench/check_plan_deps.py PLAN              # before `dispatch`
    python3 -I tools/bench/check_plan_deps.py PLAN --cells DIR  # after `collect`

It reads the cell workflow at the plan's dispatch ref (`origin/<ref>`; another
revision with --rev, e.g. HEAD) and fails unless:

  1. core/CMakeLists.txt at every `source_sha` of the plan pins, as
     SWARM_CORE_REF, the swarm-core commit the workflow builds: the commit
     under test was written and tested by CI with that swarm-core. The plan
     is parsed as ns3-campaign parses it (PyYAML for .yaml/.yml, tomllib,
     json), and `source_sha` is read where ns3-campaign reads it: the plan's
     own and every `pins.<arm>.source_sha`. So a key or a value is what the
     parser makes of it, however it is spelled (quoted, escaped, aliased); a
     value that is not a 40-char commit fails, and none is skipped;
  2. the plan names every dependency of the workflow with its commit, as
     `<name>@<commit>` (the form of ##PROV## `modules=`) or as the workflow's
     own line `<name>=<owner>/<repo>@<commit>`, in a field or in its header,
     and each is the commit the workflow builds (7 to 40 hex digits; a prefix
     matches). A plan that names another commit of a dependency fails;
  3. with --cells: every ##PROV## line of every DIR/*.txt names those commits
     in `modules=`.

PLAN and DIR are paths inside this repository (relative to its root, or
absolute; tools/bench/campaigns/, .campaign/, results/cells/); any other is
refused, never read (#126). The last line is the verdict (`plan deps: ok` or
`plan deps: FAIL (n)`); the exit status is 0 or 1. Standard library, plus
PyYAML for a .yaml/.yml plan (as ns3-campaign; without it such a plan fails,
it is never read by pattern); it needs `git` and the plan's commits in the
local repository (`git fetch origin` first).

    python3 -I tools/bench/check_plan_deps.py --self-test
"""

import argparse
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import tomllib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
SHA = r"[0-9a-f]{40}"
DEP_LINE = re.compile(rf"^\s+([a-z0-9][a-z0-9-]*)=[\w.-]+/[\w.-]+@({SHA})(?::\S+)?\s*$", re.M)
PIN = re.compile(rf'set\(SWARM_CORE_REF\s+"({SHA})"')


def inside(root, path):
    """`path`'s real path if it lies inside `root` (relative paths are taken from
    `root`), else None: an argument cannot make the script read a file elsewhere."""
    base = os.path.realpath(root)
    real = os.path.realpath(os.path.join(base, path))
    return real if real.startswith(base + os.sep) else None


def git(root, *args):
    r = subprocess.run(["git", "-C", str(root), *args], capture_output=True, text=True)
    return r.stdout if r.returncode == 0 else None


def load_plan(path):
    """The plan as ns3-campaign loads it: (mapping, None) or (None, why not)."""
    path = pathlib.Path(path)
    ext = path.suffix.lower()
    try:
        if ext in (".yaml", ".yml"):
            try:
                import yaml
            except ImportError:
                return None, "a YAML plan needs PyYAML (pip install pyyaml), as ns3-campaign does"
            plan = yaml.safe_load(path.read_text(encoding="utf-8"))
        elif ext == ".toml":
            plan = tomllib.loads(path.read_text(encoding="utf-8"))
        elif ext == ".json":
            plan = json.loads(path.read_text(encoding="utf-8"))
        else:
            return None, "a plan is a .yaml, .yml, .toml or .json file"
    except Exception as e:  # noqa: BLE001  (each parser has its own error type)
        return None, f"the plan does not parse: {type(e).__name__}: {' '.join(str(e).split())}"
    return (plan, None) if isinstance(plan, dict) else (None, "the plan is not a mapping")


def plan_source_shas(plan):
    """Every source_sha ns3-campaign reads: the plan's own, then each pin's, in order.

    Returns (commits, problems): a problem is a value that is not a 40-char
    commit, or a `pins` ns3-campaign refuses.
    """
    values, problems = [], []
    if "source_sha" in plan:
        values.append(plan["source_sha"])
    pins = plan.get("pins")
    if pins is None:  # absent, or an empty `pins:`
        pins = {}
    # Stricter than ns3-campaign, which reads [], false and 0 as no pins.
    if not isinstance(pins, dict):
        return [], ["pins is not a mapping arm -> {source_sha, image}"]
    for arm, pin in pins.items():
        if not isinstance(pin, dict):
            problems.append(f"pins.{arm} is not a mapping")
        elif "source_sha" in pin:
            values.append(pin["source_sha"])
    shas = [v for v in values if isinstance(v, str) and re.fullmatch(SHA, v)]
    problems += [
        f"source_sha {v!r} is not a 40-char commit: it is not checked"
        for v in values
        if v not in shas
    ]
    return list(dict.fromkeys(shas)), problems


def plan_names(text, name):
    """The commits the plan names for dependency `name`, comments included."""
    pat = rf"(?<![\w-]){re.escape(name)}(?:=[\w.-]+/[\w.-]+)?@([0-9a-f]{{7,40}})\b"
    return list(dict.fromkeys(re.findall(pat, text)))


def check(root, plan_path, rev=None, cells=None, out=print):
    """Prints one line per check; returns the number of failures."""
    fails = 0

    def fail(msg):
        nonlocal fails
        fails += 1
        out(f"FAIL {msg}")

    real = inside(root, plan_path)
    if real is None or not os.path.isfile(real):
        fail(f"{plan_path}: not a plan file inside {os.path.realpath(root)}")
        return fails
    plan_path = real
    text = pathlib.Path(plan_path).read_text(encoding="utf-8")
    plan, why = load_plan(plan_path)
    if plan is None:
        fail(why)
        return fails
    workflow = str(plan.get("workflow") or "benchmark-cell.yml")
    rev = rev or f"origin/{plan.get('ref') or 'main'}"
    # Both reach `git show`: a revision is never an option, a workflow is a file name.
    if rev.startswith("-") or not re.fullmatch(r"[\w./~^@{}-]+", rev):
        fail(f"{rev!r} is not a revision")
        return fails
    if not re.fullmatch(r"\w[\w.-]*\.ya?ml", workflow):
        fail(f"{workflow!r} is not a workflow file name")
        return fails
    wf_path = f".github/workflows/{workflow}"
    wf = git(root, "show", f"{rev}:{wf_path}")
    if wf is None:
        fail(f"cannot read {wf_path} at {rev} (git fetch origin, or give --rev)")
        return fails
    deps = dict((n, c) for n, c in DEP_LINE.findall(wf))
    if "swarm-core" not in deps:
        fail(f"{wf_path} at {rev} names no swarm-core=<owner>/<repo>@<commit> dependency")
        return fails
    for name, commit in deps.items():
        out(f"     {wf_path} at {rev}: {name}@{commit}")

    shas, problems = plan_source_shas(plan)
    if not shas and not problems:
        fail("the plan has no source_sha")
    for problem in problems:
        fail(problem)
    for sha in shas:
        cmake = git(root, "show", f"{sha}:core/CMakeLists.txt")
        pin = PIN.search(cmake) if cmake else None
        if not pin:
            fail(f"cannot read SWARM_CORE_REF in core/CMakeLists.txt at {sha} (git fetch origin)")
        elif pin.group(1) != deps["swarm-core"]:
            fail(
                f"source_sha {sha[:7]} pins swarm-core {pin.group(1)[:7]} (SWARM_CORE_REF), "
                f"but its cells would build on {deps['swarm-core'][:7]}"
            )
        else:
            out(f"ok   source_sha {sha[:7]} pins swarm-core {pin.group(1)[:7]}, as the workflow")

    for name, commit in deps.items():
        named = plan_names(text, name)
        wrong = [n for n in named if not commit.startswith(n)]
        if not named:
            fail(f"the plan does not name {name}@<commit> (the workflow builds {commit})")
        elif wrong:
            fail(f"the plan names {name}@{', '.join(wrong)}, but the workflow builds {commit}")
        else:
            out(f"ok   the plan names {name}@{named[0]}, as the workflow")

    if cells is not None:
        real = inside(root, cells)
        if real is None or not os.path.isdir(real):
            fail(f"{cells}: not a directory inside {os.path.realpath(root)}")
            return fails
        files = sorted(pathlib.Path(real).glob("*.txt"))
        outside = [f for f in files if inside(root, f) is None]
        for f in outside:
            fail(f"{f.name}: links outside {os.path.realpath(root)}, not read")
        files = [f for f in files if f not in outside]
        lines = 0
        bad = 0
        for f in files:
            provs = [
                x for x in f.read_text(encoding="utf-8").splitlines() if x.startswith("##PROV##")
            ]
            if not provs:
                bad += 1
                fail(f"{f.name}: no ##PROV## line")
            for prov in provs:
                lines += 1
                m = re.search(r"\bmodules=(\S+)", prov)
                mods = dict(x.split("@", 1) for x in m.group(1).split(",") if "@" in x) if m else {}
                for name, commit in deps.items():
                    if mods.get(name) != commit:
                        bad += 1
                        fail(f"{f.name}: ##PROV## has {name}@{mods.get(name)}, not {commit}")
        if not files:
            fail(f"no *.txt cell in {cells}")
        elif not bad:
            both = ", ".join(f"{n}@{c}" for n, c in deps.items())
            out(f"ok   {len(files)} cells, {lines} ##PROV## lines: {both}")
    return fails


def self_test():
    a, b, rb = "a" * 40, "b" * 40, "c" * 40
    failures = []

    def expect(what, got, want_fails, needle=None):
        n, lines = got
        ok = n == want_fails and (needle is None or any(needle in x for x in lines))
        if not ok:
            failures.append(f"{what}: {n} failures, wanted {want_fails}: {lines}")

    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)

        def run(*args):
            subprocess.run(
                ["git", "-C", tmp, "-c", "user.name=t", "-c", "user.email=t@example.com",
                 "-c", "commit.gpgsign=false", *args],
                check=True, capture_output=True,
            )  # fmt: skip

        def commit(core, wf_core):
            (root / "core").mkdir(exist_ok=True)
            (root / ".github/workflows").mkdir(parents=True, exist_ok=True)
            (root / "core/CMakeLists.txt").write_text(f'set(SWARM_CORE_REF "{core}" CACHE STRING\n')
            (root / ".github/workflows/benchmark-cell.yml").write_text(
                "      dependencies: |\n"
                f"        swarm-core=swarm-routing/ns3-swarm-core@{wf_core}\n"
                f"        routing-bench=swarm-routing/ns3-tools@{rb}:ns3/routing-bench\n"
            )
            run("add", "-A")
            run("commit", "-q", "-m", "c")
            return git(root, "rev-parse", "HEAD").strip()

        run("init", "-q")
        old = commit(a, a)  # the pin and the workflow agree on a
        new = commit(b, b)  # a pin bump: both move to b

        def plan(source, header=""):
            p = root / "plan.yaml"
            p.write_text(
                f"{header}generation: t\nref: main\nworkflow: benchmark-cell.yml\n"
                f"source_sha: {source}\n"
            )
            return p

        def go(p, **kw):
            lines = []
            return check(root, p, rev=kw.pop("rev", "HEAD"), out=lines.append, **kw), lines

        named = f"# swarm-core@{b[:7]} routing-bench@{rb}\n"
        expect("in step", go(plan(new, named)), 0)
        quoted = (
            f"# swarm-core=swarm-routing/ns3-swarm-core@{b}\n"
            f"# routing-bench=swarm-routing/ns3-tools@{rb}:ns3/routing-bench\n"
        )
        expect("the workflow's own lines quoted", go(plan(new, quoted)), 0)
        # #105: a plan pre-registered before a pin bump, dispatched after it.
        expect("stale source_sha", go(plan(old, named)), 1, "pins swarm-core aaaaaaa")
        expect("names another commit", go(plan(new, f"# swarm-core@{a[:7]} routing-bench@{rb}\n")),
               1, "the plan names swarm-core@aaaaaaa")  # fmt: skip
        expect("names nothing", go(plan(new)), 2, "does not name routing-bench@<commit>")
        expect("a source_sha in a comment is not a field",
               go(plan(new, f"# source_sha: {old}\n{named}")), 0)  # fmt: skip
        expect("the workflow before the bump", go(plan(new, named), rev=old), 2)
        expect("no such revision", go(plan(new, named), rev="nope"), 1, "cannot read")
        p = root / "pins.yaml"
        p.write_text(f"{named}ref: main\npins:\n  base: {{source_sha: {old}}}\n"
                     f"  head: {{source_sha: {new}}}\n")  # fmt: skip
        expect("every pin is read", go(p), 1, "pins swarm-core aaaaaaa")
        # A value the regex of the first version skipped: the stale base went unchecked.
        p.write_text(f"{named}ref: main\npins:\n  base: {{source_sha: \"{old}\"}}\n"
                     f"  head: {{source_sha: {new}}}\n")  # fmt: skip
        expect("a quoted source_sha is read", go(p), 1, "pins swarm-core aaaaaaa")
        p.write_text(f"{named}ref: main\npins:\n  base: {{source_sha: {old[:12]}}}\n"
                     f"  head: {{source_sha: {new}}}\n")  # fmt: skip
        expect("a source_sha that is not 40 hex digits", go(p), 1, "is not a 40-char commit")
        # Spellings a pattern does not see and a parser does (review of #111): the
        # stale base must be read in each.
        p.write_text(f"{named}ref: main\npins:\n  base: {{\"source_\\x73ha\": {old}}}\n"
                     f"  head: {{source_sha: {new}}}\n")  # fmt: skip
        expect("an escaped key is read", go(p), 1, "pins swarm-core aaaaaaa")
        p.write_text(f"{named}ref: main\nx-k: &k source_sha\npins:\n  base: {{*k : {old}}}\n"
                     f"  head: {{source_sha: {new}}}\n")  # fmt: skip
        expect("an aliased key is read", go(p), 1, "pins swarm-core aaaaaaa")
        p.write_text(f"{named}ref: main\npins:\n  base: {{note: a#b, source_sha: {old}}}\n"
                     f"  head: {{source_sha: {new}}}\n")  # fmt: skip
        expect("a # inside a value is not a comment", go(p), 1, "pins swarm-core aaaaaaa")
        p.write_text(f"{named}ref: main\npins: [unclosed\n")
        expect("a plan that does not parse", go(p), 1, "does not parse")
        for empty in ("[]", "false", "0", '""'):
            p.write_text(f"{named}ref: main\nsource_sha: {new}\npins: {empty}\n")
            expect(f"pins: {empty} is not a mapping", go(p), 1, "pins is not a mapping")
        p.write_text(f"{named}ref: main\nsource_sha: {new}\npins:\n")
        expect("an empty pins is no pins", go(p), 0)
        j = root / "plan.json"
        j.write_text(json.dumps({"ref": "main", "x-deps": named, "pins": {
            "base": {"source_sha": old}, "head": {"source_sha": new}}}).replace(
            '"source_sha": "' + old, '"source_\\u0073ha": "' + old))  # fmt: skip
        expect("a JSON plan, its key escaped", go(j), 1, "pins swarm-core aaaaaaa")
        expect("a revision that is an option", go(plan(new, named), rev="--output=x"),
               1, "is not a revision")  # fmt: skip
        p.write_text(f"{named}ref: main\nworkflow: --x.yml\nsource_sha: {new}\n")
        expect("a workflow that is not a file name", go(p), 1, "is not a workflow file name")

        cells = root / "cells"
        cells.mkdir()
        prov = "##PROV## commit=x modules=ara@{0},swarm-core@{1},routing-bench@{2} key=k\n"
        (cells / "one.txt").write_text("##RUN## x\n" + prov.format(new, b, rb))
        expect("cells in step", go(plan(new, named), cells=cells), 0, "1 cells, 1 ##PROV## lines")
        (cells / "two.txt").write_text(prov.format(new, a, rb))
        expect("a cell on another swarm-core", go(plan(new, named), cells=cells), 1, "two.txt")
        (cells / "two.txt").write_text("##RUN## x\n")
        expect("a cell with no ##PROV##", go(plan(new, named), cells=cells), 1, "no ##PROV##")
        (cells / "two.txt").unlink()
        # #126: a plan or a cell directory outside the repository is refused, never read.
        with tempfile.TemporaryDirectory() as away:
            far = pathlib.Path(away)
            (far / "plan.yaml").write_text(named + f"ref: main\nsource_sha: {new}\n")
            (far / "x.txt").write_text(prov.format(new, b, rb))
            expect(
                "a plan outside the repository", go(far / "plan.yaml"), 1, "not a plan file inside"
            )
            expect("a plan that climbs out", go(pathlib.Path("..") / far.name / "plan.yaml"),
                   1, "not a plan file inside")  # fmt: skip
            expect("cells outside the repository", go(plan(new, named), cells=far),
                   1, "not a directory inside")  # fmt: skip
            (cells / "far.txt").symlink_to(far / "x.txt")
            expect("a cell that links outside", go(plan(new, named), cells=cells),
                   1, "links outside")  # fmt: skip

    for f in failures:
        print(f"FAIL {f}")
    print(f"self-test: {'FAIL' if failures else 'ok'} (30 cases)")
    return 1 if failures else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("plan", nargs="?", help="a campaign plan (tools/bench/campaigns/*.yaml)")
    ap.add_argument(
        "--rev", help="the revision the cell workflow is read at (default: origin/<ref>)"
    )
    ap.add_argument("--cells", help="a directory of collected cells: check their ##PROV## too")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if not args.plan:
        ap.error("a plan is required")
    fails = check(REPO, args.plan, rev=args.rev, cells=args.cells)
    print(f"plan deps: FAIL ({fails})" if fails else "plan deps: ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
