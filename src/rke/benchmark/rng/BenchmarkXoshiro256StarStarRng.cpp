// SPDX-FileCopyrightText: 2023 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include <ql/math/distributions/normaldistribution.hpp>
#include <ql/math/randomnumbers/boxmullergaussianrng.hpp>
#include <ql/math/randomnumbers/centrallimitgaussianrng.hpp>
#include <ql/math/randomnumbers/inversecumulativerng.hpp>
#include <ql/math/randomnumbers/mt19937uniformrng.hpp>
#include <ql/math/randomnumbers/xoshiro256starstaruniformrng.hpp>
#include <ql/math/randomnumbers/zigguratgaussianrng.hpp>
#include <benchmark/benchmark.h>

namespace RKE::Benchmark {
    namespace {
        // File scope so each iteration advances one continuous stream; a generator constructed
        // inside the loop would time construction and re-measure the same first draw.
        auto xoshiro256StarStar = QuantLib::Xoshiro256StarStarUniformRng();
        auto xoshiro256StarStarBoxMullerGaussian =
            QuantLib::BoxMullerGaussianRng(xoshiro256StarStar);
        auto xoshiro256StarStarCLGaussian = QuantLib::CLGaussianRng(xoshiro256StarStar);
        auto xoshiro256StarStarZigguratGaussian = QuantLib::ZigguratGaussianRng(xoshiro256StarStar);

        auto mersenneTwister = QuantLib::MersenneTwisterUniformRng();
        auto mersenneTwisterBoxMullerGaussian = QuantLib::BoxMullerGaussianRng(mersenneTwister);
        auto mersenneTwisterCLGaussian = QuantLib::CLGaussianRng(mersenneTwister);

        auto inverseCumulativeRng =
            QuantLib::InverseCumulativeRng<QuantLib::MersenneTwisterUniformRng,
                                           QuantLib::InverseCumulativeNormal>(mersenneTwister);

        // Registrations run in file order, so each Xoshiro256StarStar draw is followed by its
        // MersenneTwister counterpart where one exists, and the pairs read side by side.

        void BM_Xoshiro256StarStarNextInt64(benchmark::State& state) {
            for (const auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
                benchmark::DoNotOptimize(xoshiro256StarStar.nextInt64());
            }
        }
        BENCHMARK(BM_Xoshiro256StarStarNextInt64)->Name("xoshiro256StarStar.nextInt64();");

        void BM_MersenneTwisterNextInt32(benchmark::State& state) {
            for (const auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
                benchmark::DoNotOptimize(mersenneTwister.nextInt32());
            }
        }
        BENCHMARK(BM_MersenneTwisterNextInt32)->Name("mersenneTwister.nextInt32();");

        void BM_Xoshiro256StarStarNext(benchmark::State& state) {
            for (const auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
                benchmark::DoNotOptimize(xoshiro256StarStar.next());
            }
        }
        BENCHMARK(BM_Xoshiro256StarStarNext)->Name("xoshiro256StarStar.next();");

        void BM_MersenneTwisterNext(benchmark::State& state) {
            for (const auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
                benchmark::DoNotOptimize(mersenneTwister.next());
            }
        }
        BENCHMARK(BM_MersenneTwisterNext)->Name("mersenneTwister.next();");

        void BM_Xoshiro256StarStarZigguratGaussianNext(benchmark::State& state) {
            for (const auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
                benchmark::DoNotOptimize(xoshiro256StarStarZigguratGaussian.next());
            }
        }
        BENCHMARK(BM_Xoshiro256StarStarZigguratGaussianNext)
            ->Name("xoshiro256StarStarZigguratGaussianNext.next();");

        void BM_Xoshiro256StarStarBoxMullerGaussianNext(benchmark::State& state) {
            for (const auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
                benchmark::DoNotOptimize(xoshiro256StarStarBoxMullerGaussian.next());
            }
        }
        BENCHMARK(BM_Xoshiro256StarStarBoxMullerGaussianNext)
            ->Name("xoshiro256StarStarBoxMullerGaussian.next();");

        void BM_MersenneTwisterBoxMullerGaussianNext(benchmark::State& state) {
            for (const auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
                benchmark::DoNotOptimize(mersenneTwisterBoxMullerGaussian.next());
            }
        }
        BENCHMARK(BM_MersenneTwisterBoxMullerGaussianNext)
            ->Name("mersenneTwisterBoxMullerGaussian.next();");

        void BM_InverseCumulativeRngNext(benchmark::State& state) {
            for (const auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
                benchmark::DoNotOptimize(inverseCumulativeRng.next());
            }
        }
        BENCHMARK(BM_InverseCumulativeRngNext)->Name("inverseCumulativeRng.next();");

        void BM_Xoshiro256StarStarCLGaussianNext(benchmark::State& state) {
            for (const auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
                benchmark::DoNotOptimize(xoshiro256StarStarCLGaussian.next());
            }
        }
        BENCHMARK(BM_Xoshiro256StarStarCLGaussianNext)
            ->Name("xoshiro256StarStarCLGaussian.next();");

        void BM_MersenneTwisterCLGaussianNext(benchmark::State& state) {
            for (const auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
                benchmark::DoNotOptimize(mersenneTwisterCLGaussian.next());
            }
        }
        BENCHMARK(BM_MersenneTwisterCLGaussianNext)->Name("mersenneTwisterCLGaussian.next();");
    }
}

BENCHMARK_MAIN();
