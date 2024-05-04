//
// Created by ralf.eckel on 08.08.2023.
//

#ifndef QL_PERFORMANCE_TESTING_QL_XOSHIRO256STARSTARRNG_BENCHMARKING_HPP
#define QL_PERFORMANCE_TESTING_QL_XOSHIRO256STARSTARRNG_BENCHMARKING_HPP

#include <benchmark/benchmark.h>

void BM_Xoshiro256StarStarNextInt64(benchmark::State& state);
void BM_Xoshiro256StarStarNext(benchmark::State& state);
void BM_Xoshiro256StarStarBoxMullerGaussianNext(benchmark::State& state);
void BM_Xoshiro256StarStarCLGaussianNext(benchmark::State& state);
void BM_Xoshiro256StarStarZigguratGaussianNext(benchmark::State& state);

void BM_MersenneTwisterNextInt32(benchmark::State& state);
void BM_MersenneTwisterNext(benchmark::State& state);
void BM_MersenneTwisterBoxMullerGaussianNext(benchmark::State& state);
void BM_MersenneTwisterCLGaussianNext(benchmark::State& state);

#endif  // QL_PERFORMANCE_TESTING_QL_XOSHIRO256STARSTARRNG_BENCHMARKING_HPP
