#pragma once

#include "eval_common.h"
#include <algorithm>
#include <span>
#include <utility>
#include <vector>

namespace sph::zstd::eval
{
    inline auto median(std::vector<std::uint64_t> values) -> std::uint64_t
    {
        if (values.empty()) throw std::invalid_argument{"cannot summarize empty trials"};
        std::ranges::sort(values);
        auto const middle{values.size() / 2U};
        if (values.size() % 2U != 0U) return values[middle];
        // Avoid overflow when the two central values are near UINT64_MAX.
        return values[middle - 1U] + (values[middle] - values[middle - 1U]) / 2U;
    }

    inline auto summarize_trials(std::span<worker_result const> trials) -> worker_result
    {
        if (trials.empty()) throw std::invalid_argument{"cannot summarize empty trials"};
        auto result{trials.front()};
        std::vector<std::uint64_t> elapsed, memory;
        for (auto const& trial : trials)
        {
            if (trial.iterations == 0U || trial.elapsed_nanoseconds == 0U ||
                trial.iterations != result.iterations || trial.input_bytes != result.input_bytes ||
                trial.output_bytes != result.output_bytes)
                throw std::invalid_argument{"inconsistent evaluation trials"};
            elapsed.push_back(trial.elapsed_nanoseconds);
            memory.push_back(trial.peak_resident_bytes);
        }
        result.elapsed_nanoseconds = median(std::move(elapsed));
        result.peak_resident_bytes = median(std::move(memory));
        return result;
    }
}
