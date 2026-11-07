#pragma once

#include <chrono>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Psapi.h>
#else
#include <sys/resource.h>
#endif

namespace sph::zstd::eval
{
    struct worker_result
    {
        std::uint64_t elapsed_nanoseconds{};
        std::uint64_t peak_resident_bytes{};
        std::uint64_t input_bytes{};
        std::uint64_t output_bytes{};
        std::uint64_t iterations{};
    };

    inline auto read_binary(std::filesystem::path const& path) -> std::vector<std::uint8_t>
    {
        std::ifstream input{path, std::ios::binary};
        if (!input)
        {
            throw std::runtime_error{"cannot open input file: " + path.string()};
        }
        input.seekg(0, std::ios::end);
        auto const end{input.tellg()};
        if (end < 0)
        {
            throw std::runtime_error{"cannot determine input size: " + path.string()};
        }
        auto const size{static_cast<std::uintmax_t>(end)};
        if (size > std::numeric_limits<std::size_t>::max())
        {
            throw std::runtime_error{"input file is too large: " + path.string()};
        }
        input.seekg(0, std::ios::beg);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        if (!bytes.empty())
        {
            input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!input)
            {
                throw std::runtime_error{"cannot read input file: " + path.string()};
            }
        }
        return bytes;
    }

    inline void write_binary(std::filesystem::path const& path, std::span<std::uint8_t const> bytes)
    {
        std::ofstream output{path, std::ios::binary | std::ios::trunc};
        if (!output)
        {
            throw std::runtime_error{"cannot open output file: " + path.string()};
        }
        if (!bytes.empty())
        {
            output.write(reinterpret_cast<char const*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
        }
        if (!output)
        {
            throw std::runtime_error{"cannot write output file: " + path.string()};
        }
    }

    inline auto parse_iterations(std::string_view text) -> std::uint64_t
    {
        std::size_t consumed{};
        auto const result{std::stoull(std::string{text}, &consumed)};
        if (consumed != text.size() || result == 0)
        {
            throw std::invalid_argument{"iterations must be a positive integer"};
        }
        return result;
    }

    inline auto parse_linux_peak_resident_bytes(std::istream& status) -> std::uint64_t
    {
        std::string line;
        while (std::getline(status, line))
        {
            if (!line.starts_with("VmHWM:")) continue;
            auto value{std::string_view{line}.substr(6U)};
            auto const first{value.find_first_not_of(" \t")};
            if (first == std::string_view::npos) break;
            value.remove_prefix(first);
            std::uint64_t kib{};
            auto const parsed{std::from_chars(value.data(), value.data() + value.size(), kib)};
            if (parsed.ec != std::errc{} || kib == 0U ||
                kib > std::numeric_limits<std::uint64_t>::max() / 1024U) break;
            auto units{std::string_view{parsed.ptr,
                static_cast<std::size_t>(value.data() + value.size() - parsed.ptr)}};
            auto const unit_begin{units.find_first_not_of(" \t\r")};
            auto const unit_end{units.find_last_not_of(" \t\r")};
            if (unit_begin == std::string_view::npos ||
                units.substr(unit_begin, unit_end - unit_begin + 1U) != "kB") break;
            return kib * 1024U;
        }
        throw std::runtime_error{"missing or invalid VmHWM in /proc/self/status"};
    }

    inline auto peak_resident_bytes() -> std::uint64_t
    {
#if defined(_WIN32)
        PROCESS_MEMORY_COUNTERS counters{};
        counters.cb = sizeof(counters);
        if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) == 0)
        {
            throw std::runtime_error{"GetProcessMemoryInfo failed"};
        }
        return static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
#elif defined(__linux__)
        // getrusage can retain a launcher's pre-exec high-water mark. VmHWM
        // belongs to the worker's current memory image instead.
        std::ifstream status{"/proc/self/status"};
        if (!status) throw std::runtime_error{"cannot open /proc/self/status"};
        return parse_linux_peak_resident_bytes(status);
#else
        rusage usage{};
        if (getrusage(RUSAGE_SELF, &usage) != 0)
        {
            throw std::runtime_error{"getrusage failed"};
        }
#if defined(__APPLE__)
        return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
        return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024U;
#endif
#endif
    }

    inline void write_result(std::filesystem::path const& path, worker_result const& result)
    {
        std::ofstream output{path, std::ios::trunc};
        if (!output)
        {
            throw std::runtime_error{"cannot open metrics file: " + path.string()};
        }
        output << result.elapsed_nanoseconds << ',' << result.peak_resident_bytes << ','
            << result.input_bytes << ',' << result.output_bytes << ',' << result.iterations << '\n';
    }

    inline auto read_result(std::filesystem::path const& path) -> worker_result
    {
        std::ifstream input{path};
        worker_result result{};
        char separator{};
        input >> result.elapsed_nanoseconds >> separator;
        if (separator != ',') throw std::runtime_error{"invalid worker metrics: " + path.string()};
        input >> result.peak_resident_bytes >> separator;
        if (separator != ',') throw std::runtime_error{"invalid worker metrics: " + path.string()};
        input >> result.input_bytes >> separator;
        if (separator != ',') throw std::runtime_error{"invalid worker metrics: " + path.string()};
        input >> result.output_bytes >> separator;
        if (separator != ',') throw std::runtime_error{"invalid worker metrics: " + path.string()};
        input >> result.iterations;
        if (!input || result.iterations == 0)
        {
            throw std::runtime_error{"invalid worker metrics: " + path.string()};
        }
        return result;
    }

    using clock = std::chrono::steady_clock;

    inline auto elapsed_nanoseconds(clock::time_point start, clock::time_point end) -> std::uint64_t
    {
        auto const elapsed{std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()};
        if (elapsed < 0)
        {
            throw std::runtime_error{"steady clock moved backwards"};
        }
        return static_cast<std::uint64_t>(elapsed);
    }
}
