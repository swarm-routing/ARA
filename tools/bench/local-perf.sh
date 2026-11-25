#!/usr/bin/env bash
# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
#
# One cell's cost with and without an attribute, on the optimized ns-3 image
# the benchmark cells run on (#289). Builds this checkout's `ara` with the
# swarm-adapter-ns3 and routing-bench commits .github/workflows/benchmark-cell.yml
# names (their checkouts must be at those commits), runs the cell twice and
# prints both runs' ##PERF## lines, and whether every other marker line is
# identical. A debug image's timings are no measure; this is the local
# stand-in for a campaign's ##PERF##, not a paired verdict (org rule 6).
#
#   ADAPTER=../swarm-adapter-ns3 BENCH=../ns3-tools tools/bench/local-perf.sh \
#       ns3::ara::RoutingProtocol::LazyDecay=true [routing-compare args...]
#
# The default cell is smoke.yaml's grid-rwp with 5 seeds.
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
attr=${1:?usage: local-perf.sh <ns3::Type::Attribute=value> [routing-compare args...]}
shift
args=${*:-"--scenario=paper --nNodes=50 --time=300 --areaX=1500 --areaY=300 --speed=20 --pause=30 --range=300 --propagation=range --mobility=rwp --transport=udp --diag --firstRun=1 --runs=5 --protocols=ara"}
adapter=$(cd "${ADAPTER:?set ADAPTER to a swarm-adapter-ns3 checkout}" && pwd)
bench=$(cd "${BENCH:?set BENCH to an ns3-tools (sim-tools) checkout}" && pwd)
image=${IMAGE:-ghcr.io/swarm-routing/ns3-dev:latest-optimized}

wf="$root/.github/workflows/benchmark-cell.yml"
want_adapter=$(grep -o 'swarm-core=[^@]*@[0-9a-f]\{40\}' "$wf" | cut -d@ -f2)
want_bench=$(grep -o 'routing-bench=[^@]*@[0-9a-f]\{40\}' "$wf" | cut -d@ -f2)
for pair in "$adapter:$want_adapter" "$bench:$want_bench"; do
    dir=${pair%%:*}
    want=${pair##*:}
    have=$(git -C "$dir" rev-parse HEAD)
    if [ "$have" != "$want" ]; then
        echo "local-perf: $dir is at $have, benchmark-cell.yml builds $want (git -C $dir checkout $want)" >&2
        exit 1
    fi
done

out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
docker run --rm -v "$root:/src/ara:ro" -v "$adapter:/src/adapter:ro" -v "$bench:/src/bench:ro" \
    -v "$out:/out" "$image" bash -c "
set -e
ns3-build-contrib swarm-core=/src/adapter routing-bench=/src/bench/ns3/routing-bench ara=/src/ara/ns3 >/tmp/b.log 2>&1 || { tail -40 /tmp/b.log; exit 1; }
cd /opt/ns-3
./ns3 run --no-build 'routing-compare $args' > /out/off.txt 2>&1 || true
./ns3 run --no-build 'routing-compare $args --$attr' > /out/on.txt 2>&1 || true
"
markers() { grep '^##' "$1" | grep -v '^##PERF##\|^##ARGS##\|^##CONFIG##'; }
perf() {  # a run with no ##PERF## failed (an unknown attribute, say): show why
    grep '^##PERF##' "$1" || { echo "local-perf: no ##PERF## in this run:" >&2; tail -n 15 "$1" >&2; exit 1; }
}
echo "off:"
perf "$out/off.txt"
echo "on ($attr):"
perf "$out/on.txt"
if diff <(markers "$out/off.txt") <(markers "$out/on.txt") >/dev/null; then
    echo "every other marker line identical ($(markers "$out/off.txt" | wc -l) lines)"
else
    echo "marker lines differ: the attribute changes the run's outputs"
fi
