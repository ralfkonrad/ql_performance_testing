//
// Created by ralf.eckel on 08.08.2023.
//

#include "ql_xoshiro256starstarrng_benchmarking.hpp"
#include <ql/math/randomnumbers/boxmullergaussianrng.hpp>
#include <ql/math/randomnumbers/centrallimitgaussianrng.hpp>
#include <ql/math/randomnumbers/mt19937uniformrng.hpp>
#include <ql/math/randomnumbers/xoshiro256starstaruniformrng.hpp>

auto xoshiro256StarStar = QuantLib::Xoshiro256StarStarUniformRng();
auto xoshiro256StarStarBoxMullerGaussian = QuantLib::BoxMullerGaussianRng(xoshiro256StarStar);
auto xoshiro256StarStarCLGaussian = QuantLib::CLGaussianRng(xoshiro256StarStar);

auto mersenneTwister = QuantLib::MersenneTwisterUniformRng();
auto mersenneTwisterBoxMullerGaussian = QuantLib::BoxMullerGaussianRng(mersenneTwister);
auto mersenneTwisterCLGaussian = QuantLib::CLGaussianRng(mersenneTwister);

void BM_Xoshiro256StarStarNextInt64(benchmark::State& state) {
  for (auto _ : state) {
    xoshiro256StarStar.nextInt64();
  }
}

void BM_Xoshiro256StarStarNext(benchmark::State& state) {
  for (auto _ : state) {
    xoshiro256StarStar.next();
  }
}

void BM_Xoshiro256StarStarBoxMullerGaussianNext(benchmark::State& state) {
  for (auto _ : state) {
    xoshiro256StarStarBoxMullerGaussian.next();
  }
}

void BM_Xoshiro256StarStarCLGaussianNext(benchmark::State& state) {
  for (auto _ : state) {
    xoshiro256StarStarCLGaussian.next();
  }
}

void BM_MersenneTwisterNextInt32(benchmark::State& state) {
  for (auto _ : state) {
    mersenneTwister.nextInt32();
  }
}

void BM_MersenneTwisterNext(benchmark::State& state) {
  for (auto _ : state) {
    mersenneTwister.next();
  }
}

void BM_MersenneTwisterBoxMullerGaussianNext(benchmark::State& state) {
  for (auto _ : state) {
    mersenneTwisterBoxMullerGaussian.next();
  }
}

void BM_MersenneTwisterCLGaussianNext(benchmark::State& state) {
  for (auto _ : state) {
    mersenneTwisterCLGaussian.next();
  }
}
