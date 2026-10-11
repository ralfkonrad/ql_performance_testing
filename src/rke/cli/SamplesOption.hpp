// SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
// SPDX-License-Identifier: MIT

#ifndef SAMPLESOPTION_HPP
#define SAMPLESOPTION_HPP

#include <ql/types.hpp>
#include <CLI/CLI.hpp>
#include <limits>

namespace RKE::Cli {
    // --samples, the paths per pricing: any positive count. Left out, samples keeps its value,
    // which is how the benchmark falls back to productionSamples; the option is returned for a
    // caller that requires or rejects it instead, as the profile executable does.
    inline CLI::Option* addSamplesOption(CLI::App& app, QuantLib::Size& samples) {
        return app.add_option("--samples", samples, "Paths per pricing")
            ->check(CLI::Range(QuantLib::Size{1}, std::numeric_limits<QuantLib::Size>::max()));
    }
}

#endif // SAMPLESOPTION_HPP
