//
// Created by ralf.eckel on 08.08.2023.
//

#include "ql_xoshiro256starstarrng_benchmarking.hpp"
#include <ql/math/randomnumbers/boxmullergaussianrng.hpp>
#include <ql/math/randomnumbers/mt19937uniformrng.hpp>
#include <ql/math/randomnumbers/xoshiro256starstaruniformrng.hpp>

auto xoshiro256StarStarUniformRng = QuantLib::Xoshiro256StarStarUniformRng(1);
auto boxMullerGaussianXoshiro256StarStarRng =
    QuantLib::BoxMullerGaussianRng(xoshiro256StarStarUniformRng);
auto mersenneTwisterUniformRng = QuantLib::MersenneTwisterUniformRng(1);
auto boxMullerGaussianMersenneTwisterRng =
    QuantLib::BoxMullerGaussianRng(mersenneTwisterUniformRng);

void BM_TestXoshiro256StarStarRngNextInt64(benchmark::State& state) {
  for (auto _ : state) {
    xoshiro256StarStarUniformRng.nextInt64();
  }
}

void BM_TestXoshiro256StarStarRngNext(benchmark::State& state) {
  for (auto _ : state) {
    xoshiro256StarStarUniformRng.next();
  }
}

void BM_TestXoshiro256StarStarBoxMullerNext(benchmark::State& state) {
  for (auto _ : state) {
    boxMullerGaussianXoshiro256StarStarRng.next();
  }
}

void BM_TestMersenneTwisterUniformRngNextInt32(benchmark::State& state) {
  for (auto _ : state) {
    mersenneTwisterUniformRng.nextInt32();
  }
}

void BM_TestMersenneTwisterUniformRngNext(benchmark::State& state) {
  for (auto _ : state) {
    mersenneTwisterUniformRng.next();
  }
}
void BM_TestMersenneTwisterBoxMullerRngNext(benchmark::State& state) {
  for (auto _ : state) {
    boxMullerGaussianMersenneTwisterRng.next();
  }
}
