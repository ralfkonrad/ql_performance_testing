<!--
SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
SPDX-License-Identifier: MIT
-->

# Profiling

Read this when profiling or reading a profile. Profiling runs the dedicated
executables under `src/rke/profile/`, one workload each, not the google-benchmark
binary; timing is [`benchmarking.md`](benchmarking.md). Their code lives in
`RKE::Profile`; only `main` stays global.

## 1. Build

Profile from the `profile` preset, time from `release`. `profile` is `release`
plus debug info and frame pointers on every target, QuantLib included, so a stack
unwinds through `ql/` frames; it costs a few percent, which is why its timings are
not quoted.

```bash
cmake --workflow profile
PROG=./build/profile/src/rke/profile/<executable>
OUT=build/profile/prof/$(basename "$PROG") && mkdir -p "$OUT"
```

Every recipe ends in a text file, so an agent reads the result rather than a
picture.

## 2. perf

```bash
# sampled CPU time: flat hotspots, call graph, folded stacks
perf record -e cpu-clock:u -F 999 --call-graph fp -o "$OUT/perf.data" -- "$PROG"
perf report --no-inline -i "$OUT/perf.data" --stdio --no-children -g none \
    --sort symbol --percent-limit 1 > "$OUT/flat.txt"
perf report --no-inline -i "$OUT/perf.data" --stdio --no-children \
    --percent-limit 1 > "$OUT/perf.txt"
perf script --no-inline -i "$OUT/perf.data" | stackcollapse-perf.pl > "$OUT/perf.folded"

# before/after: fold both runs and diff; flamegraph.pl turns either into an SVG for people
difffolded.pl before.folded after.folded > "$OUT/diff.folded"
```

Always pass `--no-inline`. Without it perf resolves inlined frames through
addr2line: `perf script` goes from under a second to over a minute, and the folded
stacks lose their `QuantLib::` qualification and gain frames that were never on the
call path.

## 3. valgrind

valgrind simulates the CPU and runs the whole executable that way, far slower
than natively, so size the workload for it.

```bash
# exact instruction counts per function, inclusive of callees
valgrind --tool=callgrind --callgrind-out-file="$OUT/callgrind.out" "$PROG"
callgrind_annotate --inclusive=yes "$OUT/callgrind.out" > "$OUT/callgrind.txt"

# simulated cache misses
valgrind --tool=cachegrind --cache-sim=yes --cachegrind-out-file="$OUT/cachegrind.out" "$PROG"
cg_annotate "$OUT/cachegrind.out" > "$OUT/cachegrind.txt"

# allocation sites (tbk: blocks, tb: bytes, fs: indices into ftbl), and heap over time
valgrind --tool=dhat --dhat-out-file="$OUT/dhat.json" "$PROG"
jq -r '.ftbl as $f | .pps | sort_by(-.tbk) | .[:10][]
       | "\(.tbk) blocks, \(.tb) bytes\n  " + ([.fs[:12][] | $f[.]] | join("\n  "))' \
    "$OUT/dhat.json" > "$OUT/dhat.txt"
valgrind --tool=massif --massif-out-file="$OUT/massif.out" "$PROG"
ms_print "$OUT/massif.out" > "$OUT/massif.txt"
```

## 4. Hardware Counters

Under WSL2 the VM has no hardware performance counters: `perf` samples
`cpu-clock` only, and `cycles`, IPC and cache-miss events do not exist. Cache and
instruction figures come from cachegrind and callgrind, which simulate them. A
native Linux box has the counters. Say which kind of machine a profile came from.
