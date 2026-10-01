// SPDX-FileCopyrightText: 2023 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#include "BenchmarkXoshiro256StarStarRng.hpp"
#include <ql/math/distributions/normaldistribution.hpp>
#include <ql/math/randomnumbers/boxmullergaussianrng.hpp>
#include <ql/math/randomnumbers/centrallimitgaussianrng.hpp>
#include <ql/math/randomnumbers/inversecumulativerng.hpp>
#include <ql/math/randomnumbers/mt19937uniformrng.hpp>
#include <ql/math/randomnumbers/xoshiro256starstaruniformrng.hpp>
#include <ql/math/randomnumbers/zigguratgaussianrng.hpp>

// File scope so each iteration advances one continuous stream; a generator constructed inside
// the loop would time construction and re-measure the same first draw.
static auto xoshiro256StarStar = QuantLib::Xoshiro256StarStarUniformRng();
static auto xoshiro256StarStarBoxMullerGaussian =
    QuantLib::BoxMullerGaussianRng(xoshiro256StarStar);
static auto xoshiro256StarStarCLGaussian = QuantLib::CLGaussianRng(xoshiro256StarStar);
static auto xoshiro256StarStarZigguratGaussian = QuantLib::ZigguratGaussianRng(xoshiro256StarStar);

static auto mersenneTwister = QuantLib::MersenneTwisterUniformRng();
static auto mersenneTwisterBoxMullerGaussian = QuantLib::BoxMullerGaussianRng(mersenneTwister);
static auto mersenneTwisterCLGaussian = QuantLib::CLGaussianRng(mersenneTwister);

static auto inverseCumulativeRng =
    QuantLib::InverseCumulativeRng<QuantLib::MersenneTwisterUniformRng,
                                   QuantLib::InverseCumulativeNormal>(mersenneTwister);

namespace RKE::Benchmark {
    void BM_Xoshiro256StarStarNextInt64(benchmark::State& state) {
        for (const auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
            xoshiro256StarStar.nextInt64();
        }
    }

    void BM_Xoshiro256StarStarNext(benchmark::State& state) {
        for (const auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
            xoshiro256StarStar.next();
        }
    }

    void BM_Xoshiro256StarStarBoxMullerGaussianNext(benchmark::State& state) {
        for (const auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
            xoshiro256StarStarBoxMullerGaussian.next();
        }
    }

    void BM_Xoshiro256StarStarCLGaussianNext(benchmark::State& state) {
        for (const auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
            xoshiro256StarStarCLGaussian.next();
        }
    }

    void BM_Xoshiro256StarStarZigguratGaussianNext(benchmark::State& state) {
        for (const auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
            xoshiro256StarStarZigguratGaussian.next();
        }
    }

    void BM_MersenneTwisterNextInt32(benchmark::State& state) {
        for (const auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
            mersenneTwister.nextInt32();
        }
    }

    void BM_InverseCumulativeRngNext(benchmark::State& state) {
        for (const auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
            inverseCumulativeRng.next();
        }
    }

    void BM_MersenneTwisterNext(benchmark::State& state) {
        for (const auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
            mersenneTwister.next();
        }
    }

    void BM_MersenneTwisterBoxMullerGaussianNext(benchmark::State& state) {
        for (const auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
            mersenneTwisterBoxMullerGaussian.next();
        }
    }

    void BM_MersenneTwisterCLGaussianNext(benchmark::State& state) {
        for (const auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
            mersenneTwisterCLGaussian.next();
        }
    }
}
