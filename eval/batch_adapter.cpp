#include "batch_api.h"

// Compile this translation unit twice. Renaming the root namespace prevents
// template/inline symbols from different header revisions violating the ODR.
#define sph SPH_BATCH_NAMESPACE
#include <sph/zstd++/zstd_compress.h>
#include <sph/zstd++/zstd_decompress.h>
#undef sph

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace
{
    namespace impl = SPH_BATCH_NAMESPACE::zstd;
    constexpr auto settings = [] {
        auto p = impl::compression_parameters{};
        p.compression_level = SPH_EVAL_LEVEL;
        p.pledged_source_size = SPH_EVAL_CORPUS_SIZE;
        return p;
    }();
    constexpr auto resolved = impl::detail::resolve_parameters(settings);
    struct sink
    {
        std::vector<std::uint8_t>* bytes;
        void operator()(std::span<std::uint8_t const> data) const
        {
            bytes->insert(bytes->end(), data.begin(), data.end());
        }
    };
    struct context
    {
        std::span<std::uint8_t const> input;
        std::vector<std::uint8_t> bytes;
        decltype(impl::make_zstd_compress<settings>(sink{})) compressor;
        explicit context(std::span<std::uint8_t const> data)
            : input{data}, compressor{sink{&bytes}}
        {
            if (data.size() != SPH_EVAL_CORPUS_SIZE)
                throw std::invalid_argument{"input size differs from compiled pledge"};
            bytes.reserve(data.size() + data.size() / 128U + 64U);
        }
    };
    void* create(std::span<std::uint8_t const> input) { return new context{input}; }
    void destroy(void* value) { delete static_cast<context*>(value); }
    void run(void* value, std::uint64_t iterations)
    {
        auto& c = *static_cast<context*>(value);
        for (std::uint64_t i{}; i < iterations; ++i)
        {
            c.bytes.clear();
            c.compressor.reset();
            c.compressor.compress_frame(c.input);
        }
    }
    auto output(void* value) -> std::span<std::uint8_t const>
    {
        return static_cast<context*>(value)->bytes;
    }
    auto allocations() -> std::array<std::size_t, 2>
    {
        return {impl::detail::cache_aligned_allocation_count,
            impl::detail::cache_aligned_deallocation_count};
    }
    void verify_decode(std::span<std::uint8_t const> frame, std::span<std::uint8_t const> expected)
    {
        std::vector<std::uint8_t> restored(expected.size());
        auto decoder = impl::zstd_decompress{[](std::span<std::uint8_t const>) {}};
        decoder.decompress_frame(frame, restored);
        if (decoder.decoded_size() != expected.size() || !std::ranges::equal(restored, expected))
            throw std::runtime_error{"batch adapter decoder mismatch"};
    }
}

extern "C" batch_api const* SPH_BATCH_ENTRY()
{
    static batch_api const api{SPH_EVAL_LEVEL, SPH_EVAL_CORPUS_SIZE,
        {resolved.window_log, resolved.chain_log, resolved.hash_log, resolved.search_log,
            resolved.minimum_match, resolved.target_length, static_cast<unsigned>(resolved.strategy)},
        create, destroy, run, output, allocations, verify_decode};
    return &api;
}
