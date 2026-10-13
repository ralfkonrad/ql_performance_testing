<!--
SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
SPDX-License-Identifier: MIT
-->

# Profiling

Read this when profiling or reading a profile. Profiling runs the dedicated
executables under `src/rke/profile/`, not the google-benchmark binary; timing is
[`benchmarking.md`](benchmarking.md).

A workload gets its own directory, `src/rke/profile/<workload>/`, building
`rke_profile_<workload>`, all of them in `RKE::Profile`; only `main` stays global.
It builds the instrument, market and engine from `rke_common`, and calls the same
per-iteration step as its benchmark, so a hotspot in a profile is a hotspot in the
benchmark. Never give a profile its own copy of that setup. Keep setup out of the
loop here too, or the profile shows construction rather than pricing.

## 1. Build

Profile from the `profile` preset, time from `release`. `profile` is `release`
plus debug info and frame pointers on every target, QuantLib included, so a stack
unwinds through `ql/` frames; it costs a few percent, which is why its timings are
not quoted.

```bash
cmake --workflow profile
PROG=./build/profile/src/rke/profile/<workload>/rke_profile_<workload>
OUT=build/profile/prof/$(basename "$PROG") && mkdir -p "$OUT"
```

`rke_profile_bonusclassicoption` takes `--monitoring discrete|continuous`,
`--iterations <pricings>`, `--engine mc|fd|binomial|analytic` and
`--market flat|smile-bilinear|smile-bicubic`, and under `mc` also
`--path-generation cached|uncached` and `--samples=<paths per pricing>`. None has a
default: the first four are always required, the last two under `mc`, and the other engines
refuse them.
`cached` is the step cache of the market's branch, `CachedStepSingleVariate` on `flat`
and `LocalVolStepSingleVariate` on the smiles; `uncached` is `QuantLib::SingleVariate`,
which asks the process for every step, the baseline the step cache is measured against. It
prints the final NPV at full precision, the same at any iteration count, since every
iteration reprices the same low-discrepancy paths, and the same under either path
generation, except on the smile markets where the compiler contracts to FMAs, as on arm64:
there the two differ by up to 2e-6 relative. The setup is sized like a production run, 1Y with daily steps and 2^16
paths, so this is not the test suite's 5M regression lock. The recipes below
take the workload from `ARGS` and add the iteration count; this one is that production
run:

```bash
ARGS=(--monitoring discrete --engine mc --market flat --path-generation cached --samples=65536)
```

The smile markets take the Euler branch through `LocalVolSurface`. An uncached
`smile-bicubic` repricing runs about a minute natively, far too long for callgrind at
2^16 paths. Ir per step does not depend on the path count, so count a smile run with
fewer paths, `--samples=256` as the smoke tests do, and quote it per step.

`fd` is `FdBlackScholesBonusClassicEngine` on `rke_common`'s `fdTimeGrid` by
`fdSpaceGrid`, monitored on the Monte-Carlo grid under `discrete` and continuously
otherwise, and under the local volatility on the smiles, where every time step asks
`LocalVolSurface` for a volatility at every node: tens of milliseconds a pricing on
`smile-bilinear`, a few hundred on `smile-bicubic`. `binomial` is
`BinomialBonusClassicEngine<CoxRossRubinstein>` at `treeTimeSteps` with Boyle-Lau on,
which monitors on every step and flattens the volatility, so it refuses `continuous`
and prices `flat` only. `analytic` is `AnalyticBonusClassicEngine`, the closed form,
which needs a constant volatility and monitors continuously, so it refuses `discrete`
and prices `flat` only. All three refuse `--path-generation` and `--samples`; the
result line prints the grid `fd` and `binomial` priced on in place of the path count,
the lattice after Boyle-Lau in particular. A flat pricing takes milliseconds,
`analytic` microseconds, so nothing needs shrinking for callgrind.

`rke_profile_barrieroption` prices `RKE::Common::makeBarrierOptionSetup`, the
certificate's put leg under `QuantLib::MCBarrierEngine` on the same markets and grid,
so nothing of `rke_ql_ext` is on its measured path. It takes
`--monitoring discrete|continuous`, `--iterations <pricings>`,
`--market flat|smile-bilinear|smile-bicubic` and `--samples=<paths per pricing>`, all
required, none with a default, and prints the NPV as above. `discrete` prices through
`BiasedBarrierPathPricer`, which never calls the process; `continuous` through
`BarrierPathPricer`, which calls `diffusion()` on every step of every path, on the
smiles a third `LocalVolSurface` lookup at the point the Euler step has just evaluated
twice. Its production run in that mode:

```bash
ARGS=(--monitoring continuous --market flat --samples=65536)
```

Every recipe ends in a text file, so an agent reads the result rather than a
picture.

## 2. perf

```bash
# sampled CPU time: flat hotspots, call graph, folded stacks
perf record -e cpu-clock:u -F 999 --call-graph fp -o "$OUT/perf.data" \
    -- "$PROG" "${ARGS[@]}" --iterations 10
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
than natively, so size the workload for it: callgrind counts instructions
exactly, so one iteration (`--iterations 1`) is enough.

```bash
# exact instruction counts per function, inclusive of callees
valgrind --tool=callgrind --callgrind-out-file="$OUT/callgrind.out" \
    "$PROG" "${ARGS[@]}" --iterations 1
callgrind_annotate --inclusive=yes "$OUT/callgrind.out" > "$OUT/callgrind.txt"

# simulated cache misses
valgrind --tool=cachegrind --cache-sim=yes --cachegrind-out-file="$OUT/cachegrind.out" \
    "$PROG" "${ARGS[@]}" --iterations 1
cg_annotate "$OUT/cachegrind.out" > "$OUT/cachegrind.txt"

# allocation sites (tbk: blocks, tb: bytes, fs: indices into ftbl), and heap over time
valgrind --tool=dhat --dhat-out-file="$OUT/dhat.json" \
    "$PROG" "${ARGS[@]}" --iterations 1
jq -r '.ftbl as $f | .pps | sort_by(-.tbk) | .[:10][]
       | "\(.tbk) blocks, \(.tb) bytes\n  " + ([.fs[:12][] | $f[.]] | join("\n  "))' \
    "$OUT/dhat.json" > "$OUT/dhat.txt"
valgrind --tool=massif --massif-out-file="$OUT/massif.out" \
    "$PROG" "${ARGS[@]}" --iterations 1
ms_print "$OUT/massif.out" > "$OUT/massif.txt"
```

## 4. Hardware Counters

Under WSL2 the VM has no hardware performance counters: `perf` samples
`cpu-clock` only, and `cycles`, IPC and cache-miss events do not exist. Cache and
instruction figures come from cachegrind and callgrind, which simulate them. A
native Linux box has the counters. Say which kind of machine a profile came from.
