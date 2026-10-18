#include "eval_statistics.h"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

int main()
{
    using namespace sph::zstd::eval;
    auto require = [](bool condition) {
        if (!condition) throw std::runtime_error{"evaluation statistics mismatch"};
    };
    try
    {
        std::array<worker_result, 3> trials{{{300U, 10U, 1000U, 300U, 20U},
            {100U, 90U, 1000U, 300U, 20U}, {200U, 40U, 1000U, 300U, 20U}}};
        auto const summary{summarize_trials(trials)};
        require(summary.elapsed_nanoseconds == 200U && summary.peak_resident_bytes == 40U &&
            summary.input_bytes == 1000U && summary.output_bytes == 300U && summary.iterations == 20U);
        require(summarize_trials(std::span{trials}.first(1U)).elapsed_nanoseconds == 300U);
        auto const even{summarize_trials(std::span{trials}.first(2U))};
        require(even.elapsed_nanoseconds == 200U && even.peak_resident_bytes == 50U);
        auto const maximum{std::numeric_limits<std::uint64_t>::max()};
        require(median({maximum - 2U, maximum}) == maximum - 1U);
        require(median({2U, 1U}) == 1U);
        auto rejects = [&require](auto action) {
            bool rejected{};
            try { action(); } catch (std::invalid_argument const&) { rejected = true; }
            require(rejected);
        };
        rejects([] { (void)summarize_trials({}); });
        for (auto member : {&worker_result::input_bytes, &worker_result::output_bytes,
            &worker_result::iterations})
        {
            auto mismatch{trials};
            ++(mismatch[1].*member);
            rejects([&] { (void)summarize_trials(mismatch); });
        }
        trials[1].elapsed_nanoseconds = 0U;
        rejects([&] { (void)summarize_trials(trials); });
        std::cout << "Evaluation trial aggregation tests passed\n";
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
