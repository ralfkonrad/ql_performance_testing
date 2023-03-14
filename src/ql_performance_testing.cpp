#include <benchmark/benchmark.h>
#include <iostream>
#include <ql/time/date.hpp>

int main() {
  auto date = QuantLib::Date::todaysDate();
  std::cout << "hello world at " << date << "." << std::endl;
}
