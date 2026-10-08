// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef SAMPLESOPTION_HPP
#define SAMPLESOPTION_HPP

#include <ql/types.hpp>
#include <CLI/CLI.hpp>
#include <limits>

namespace RKE::Cli {
    // --samples, the paths per pricing: any positive count, leaving samples at its default when
    // not given, which is how the benchmark and the profile executable agree on it.
    inline void addSamplesOption(CLI::App& app, QuantLib::Size& samples) {
        app.add_option("--samples", samples, "Paths per pricing")
            ->check(CLI::Range(QuantLib::Size{1}, std::numeric_limits<QuantLib::Size>::max()))
            ->capture_default_str();
    }
}

#endif // SAMPLESOPTION_HPP
