#include "batch_api.h"
#include "eval_common.h"
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>

namespace { std::atomic<std::size_t> ordinary_allocations{}; }
void* operator new(std::size_t size)
{
    if (auto* memory = std::malloc(size == 0U ? 1U : size))
    {
        ordinary_allocations.fetch_add(1U, std::memory_order_relaxed);
        return memory;
    }
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main(int argc, char** argv)
{
    try
    {
        if (argc != 6 && argc != 7)
            throw std::invalid_argument{"usage: zstdpp_batch input csv same|contexts|compare batch_frames pairs [AB|BA construction order]"};
        auto const input = sph::zstd::eval::read_binary(argv[1]);
        std::string_view const mode{argv[3]};
        if (mode != "same" && mode != "contexts" && mode != "compare")
            throw std::invalid_argument{"unknown batch mode"};
        std::string_view const construction = argc == 7 ? argv[6] : "AB";
        if (construction != "AB" && construction != "BA")
            throw std::invalid_argument{"construction order must be AB or BA"};
        auto positive_count = [](std::string_view value) {
            std::uint64_t count{};
            auto const parsed = std::from_chars(value.data(), value.data() + value.size(), count);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || count == 0U)
                throw std::invalid_argument{"batch frames and pairs must be positive integers"};
            return count;
        };
        auto const frames = positive_count(argv[4]);
        auto const pairs = positive_count(argv[5]);
        if (pairs < 2U || pairs % 2U != 0U)
            throw std::invalid_argument{"pairs must be positive, even, and at least two"};
        auto const& a = *batch_baseline_api();
        auto const& b = mode == "compare" ? *batch_candidate_api() : a;
        auto const rp = ZSTD_getCParams(a.level, input.size(), 0U);
        std::array<unsigned, 7> const reference_parameters{rp.windowLog, rp.chainLog,
            rp.hashLog, rp.searchLog, rp.minMatch, rp.targetLength, static_cast<unsigned>(rp.strategy)};
        if (a.level != b.level || a.pledge != input.size() || b.pledge != input.size() ||
            a.parameters != b.parameters || a.parameters != reference_parameters)
            throw std::runtime_error{"resolved compression parameters differ"};
        std::unique_ptr<void, void(*)(void*)> first{nullptr, a.destroy};
        std::unique_ptr<void, void(*)(void*)> second{nullptr, b.destroy};
        if (construction == "BA" && mode != "same") second.reset(b.create(input));
        first.reset(a.create(input));
        if (construction == "AB" && mode != "same") second.reset(b.create(input));
        auto* const second_context = mode == "same" ? first.get() : second.get();
        std::vector<std::uint8_t> expected(ZSTD_compressBound(input.size()));
        auto const size = ZSTD_compress(expected.data(), expected.size(), input.data(), input.size(), a.level);
        if (ZSTD_isError(size)) throw std::runtime_error{ZSTD_getErrorName(size)};
        expected.resize(size);
        auto verify = [&](batch_api const& api, void* c) {
            if (!std::ranges::equal(api.output(c), expected))
                throw std::runtime_error{"compressed bytes differ from reference"};
        };
        a.run(first.get(), 1U); b.run(second_context, 1U);
        verify(a, first.get()); verify(b, second_context);
        a.verify_decode(expected, input); b.verify_decode(expected, input);
        std::vector<std::uint8_t> decoded(input.size());
        auto const decoded_size = ZSTD_decompress(decoded.data(), decoded.size(), expected.data(), expected.size());
        if (ZSTD_isError(decoded_size) || decoded_size != input.size() || decoded != input)
            throw std::runtime_error{"reference decoder mismatch"};
        std::ofstream report{argv[2]};
        if (!report) throw std::runtime_error{"cannot open batch report"};
        report << "pair,order,frames,input_bytes,a_ns,b_ns,b_over_a,construction\n";
        auto measure = [&](batch_api const& api, void* c) {
            auto const aligned = api.allocations();
            auto const ordinary = ordinary_allocations.load(std::memory_order_relaxed);
            auto const start = sph::zstd::eval::clock::now();
            api.run(c, frames);
            auto const elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                sph::zstd::eval::clock::now() - start).count();
            if (api.allocations() != aligned || ordinary_allocations.load(std::memory_order_relaxed) != ordinary)
                throw std::runtime_error{"allocation changed inside batch"};
            verify(api, c);
            if (elapsed <= 0) throw std::runtime_error{"batch duration is not measurable"};
            return elapsed;
        };
        for (std::uint64_t i{}; i < pairs; ++i)
        {
            std::int64_t ta{}, tb{};
            if (i % 2U == 0U) { ta = measure(a, first.get()); tb = measure(b, second_context); }
            else { tb = measure(b, second_context); ta = measure(a, first.get()); }
            report << i << ',' << (i % 2U == 0U ? "AB" : "BA") << ',' << frames << ','
                   << input.size() << ',' << ta << ',' << tb << ','
                   << static_cast<double>(ta) / static_cast<double>(tb) << ',' << construction << '\n';
        }
        report.flush();
        if (!report) throw std::runtime_error{"cannot write batch report"};
        std::cout << "Batch diagnostic passed: " << mode << ", " << pairs
                  << " pairs, construction " << construction
                  << "; exact reference bytes, reciprocal decoding, and allocation guards.\n"
                     "Combined-process timings only; use isolated workers for published RSS/code size.\n";
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
