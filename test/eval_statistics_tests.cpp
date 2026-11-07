#include "eval_statistics.h"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <sstream>

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
        {
            std::istringstream status{"Name:\tworker\nVmRSS:\t100 kB\nVmHWM:\t 1234 kB\n"};
            require(parse_linux_peak_resident_bytes(status) == 1234U * 1024U);
        }
        for (auto const* text : {"VmRSS: 1234 kB\n", "VmHWM:\n", "VmHWM: 0 kB\n",
            "VmHWM: -1 kB\n", "VmHWM: 2 MB\n", "VmHWM: 2 kB extra\n",
            "VmHWM: 18446744073709551616 kB\n", "VmHWM: 18014398509481984 kB\n"})
        {
            std::istringstream status{text};
            bool rejected{};
            try { (void)parse_linux_peak_resident_bytes(status); }
            catch (std::runtime_error const&) { rejected = true; }
            require(rejected);
        }
        std::cout << "Evaluation trial aggregation tests passed\n";
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
