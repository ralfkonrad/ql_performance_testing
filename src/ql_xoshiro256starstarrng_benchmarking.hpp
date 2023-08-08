//
// Created by ralf.eckel on 08.08.2023.
//

#ifndef QL_PERFORMANCE_TESTING_QL_XOSHIRO256STARSTARRNG_BENCHMARKING_HPP
#define QL_PERFORMANCE_TESTING_QL_XOSHIRO256STARSTARRNG_BENCHMARKING_HPP

#include <benchmark/benchmark.h>

void BM_TestXoshiro256StarStarRngNextInt64(benchmark::State& state);
void BM_TestXoshiro256StarStarRngNext(benchmark::State& state);
void BM_TestXoshiro256StarStarBoxMullerNext(benchmark::State& state);

void BM_TestMersenneTwisterUniformRngNextInt32(benchmark::State& state);
void BM_TestMersenneTwisterUniformRngNext(benchmark::State& state);
void BM_TestMersenneTwisterBoxMullerRngNext(benchmark::State& state);

#endif  // QL_PERFORMANCE_TESTING_QL_XOSHIRO256STARSTARRNG_BENCHMARKING_HPP
