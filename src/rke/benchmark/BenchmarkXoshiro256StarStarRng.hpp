// SPDX-FileCopyrightText: 2023 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef BENCHMARKXOSHIRO256STARSTARRNG_HPP
#define BENCHMARKXOSHIRO256STARSTARRNG_HPP

#include <benchmark/benchmark.h>

namespace RKE::Benchmark {
    void BM_Xoshiro256StarStarNextInt64(benchmark::State& state);
    void BM_Xoshiro256StarStarNext(benchmark::State& state);
    void BM_Xoshiro256StarStarBoxMullerGaussianNext(benchmark::State& state);
    void BM_Xoshiro256StarStarCLGaussianNext(benchmark::State& state);
    void BM_Xoshiro256StarStarZigguratGaussianNext(benchmark::State& state);

    void BM_MersenneTwisterNextInt32(benchmark::State& state);
    void BM_MersenneTwisterNext(benchmark::State& state);
    void BM_MersenneTwisterBoxMullerGaussianNext(benchmark::State& state);
    void BM_MersenneTwisterCLGaussianNext(benchmark::State& state);

    void BM_InverseCumulativeRngNext(benchmark::State& state);
}

#endif // BENCHMARKXOSHIRO256STARSTARRNG_HPP
