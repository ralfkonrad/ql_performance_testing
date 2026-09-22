# ql_performance_testing

A QuantLib playground: `rke/ql/ext` is a small extension library — currently a
`BonusClassicOption` with a Monte Carlo engine — with its own Boost.Test suite,
and `benchmark/` measures it and QuantLib itself with google-benchmark. QuantLib
(the `ralfkonrad` fork) and google-benchmark are git submodules under `external/`.

## Building

CMake 3.25 or newer, Ninja, Boost, a C++17 compiler.

```bash
git clone --recurse-submodules \
    https://github.com/ralfkonrad/ql_performance_testing
cd ql_performance_testing
cmake --preset release          # or: debug
cmake --build --preset release
ctest --preset release
./build/release/benchmark/ql_performance_testing
```

The test presets filter on `^rke_`, so `ctest` runs the extension tests only.
QuantLib's own suite is built as well and stays reachable with
`ctest --preset release -R quantlib`, which takes far longer.

## Development

Warnings are errors by default (`RKE_COMPILE_WARNING_AS_ERROR`). CI builds
macOS/clang, Ubuntu/clang, Ubuntu/gcc and Windows/MSVC at C++17, 20 and 23.
clang-format and clang-tidy run as dispatch-only workflows that open a pull
request with their fixes.

[`AGENTS.md`](AGENTS.md) carries the conventions and the pitfalls in full, for AI
agents and humans alike.

## License

MIT, see [LICENSE](LICENSE).
