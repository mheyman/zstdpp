#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// Only standard-library types cross the separately namespaced implementations.
struct batch_api
{
    int level;
    std::size_t pledge;
    std::array<unsigned, 7> parameters;
    void* (*create)(std::span<std::uint8_t const>);
    void (*destroy)(void*);
    void (*run)(void*, std::uint64_t);
    std::span<std::uint8_t const> (*output)(void*);
    std::array<std::size_t, 2> (*allocations)();
    void (*verify_decode)(std::span<std::uint8_t const>, std::span<std::uint8_t const>);
};

extern "C" batch_api const* batch_baseline_api();
extern "C" batch_api const* batch_candidate_api();
