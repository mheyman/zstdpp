#include "eval_common.h"
#include "eval_corpus.h"
#include "eval_statistics.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef SPH_EVAL_BINARY_DIR
#error "SPH_EVAL_BINARY_DIR must be defined"
#endif
#ifndef SPH_EVAL_CORPUS_SIZE
#error "SPH_EVAL_CORPUS_SIZE must be defined"
#endif

namespace
{
    namespace fs = std::filesystem;
    using sph::zstd::eval::worker_result;

    struct options
    {
        std::optional<fs::path> input;
        std::optional<fs::path> output;
        unsigned level{};
        std::uint64_t iterations{1000U};
        unsigned trials{6U};
    };

    struct trial_result
    {
        worker_result cpp;
        worker_result reference;
        bool cpp_first{};
    };

    auto usage() -> std::string_view
    {
        return "usage: zstdpp_pair --level N [--input FILE] [--iterations N] "
            "[--trials N] [--output DIR]\n"
            "The C++ worker requires an input of exactly "
            "the build's SPH_ZSTDPP_EVAL_CORPUS_SIZE bytes. "
            "Omit --input to generate the deterministic mixed corpus.\n";
    }

    auto parse_unsigned(std::string_view value, std::string_view name) -> std::uint64_t
    {
        if (value.empty() || value.find_first_not_of("0123456789") != std::string_view::npos)
            throw std::invalid_argument{std::string{name} + " must be a positive integer"};
        return sph::zstd::eval::parse_iterations(value);
    }

    auto parse_options(int argc, char** argv) -> options
    {
        options result;
        for (int index{1}; index < argc; ++index)
        {
            auto const argument{std::string_view{argv[index]}};
            if (argument == "--help")
            {
                std::cout << usage();
                std::exit(0);
            }
            if (index + 1 >= argc)
                throw std::invalid_argument{std::string{usage()}};
            auto const value{std::string_view{argv[++index]}};
            if (argument == "--level")
            {
                auto const parsed{parse_unsigned(value, "level")};
                if (parsed > 22U) throw std::invalid_argument{"level must be in [1, 22]"};
                result.level = static_cast<unsigned>(parsed);
            }
            else if (argument == "--input") result.input = fs::path{value};
            else if (argument == "--output") result.output = fs::path{value};
            else if (argument == "--iterations") result.iterations = parse_unsigned(value, "iterations");
            else if (argument == "--trials")
            {
                auto const parsed{parse_unsigned(value, "trials")};
                if (parsed > 101U) throw std::invalid_argument{"trials must be in [1, 101]"};
                result.trials = static_cast<unsigned>(parsed);
            }
            else throw std::invalid_argument{std::string{usage()}};
        }
        if (result.level == 0U) throw std::invalid_argument{"--level is required\n" + std::string{usage()}};
        return result;
    }

    auto quote(fs::path const& path) -> std::string
    {
        auto value{path.string()};
        if (value.find('"') != std::string::npos)
            throw std::invalid_argument{"benchmark paths may not contain a quotation mark"};
        return '"' + value + '"';
    }

    void run_worker(fs::path const& executable, std::initializer_list<fs::path> arguments)
    {
        std::string command{quote(executable)};
        for (auto const& argument : arguments) command += ' ' + quote(argument);
#if defined(_WIN32)
        command = '"' + command + '"';
#endif
        if (std::system(command.c_str()) != 0)
            throw std::runtime_error{"benchmark worker failed: " + executable.string()};
    }

    auto worker_path(fs::path const& directory, std::string const& name,
        std::string const& extension) -> fs::path
    {
        auto path{directory / name};
        path += extension;
        if (!fs::is_regular_file(path))
            throw std::runtime_error{"worker is not built: " + path.string()};
        return path;
    }

    auto output_directory(options const& configuration) -> fs::path
    {
        if (configuration.output)
        {
            auto const path{fs::absolute(*configuration.output)};
            if (fs::exists(path) && !fs::is_empty(path))
                throw std::invalid_argument{"output directory must be empty: " + path.string()};
            fs::create_directories(path);
            return path;
        }
        auto const parent{fs::path{SPH_EVAL_BINARY_DIR} / "eval" / "pair-results"};
        fs::create_directories(parent);
        auto const stamp{std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()};
        for (unsigned attempt{}; attempt < 100U; ++attempt)
        {
            auto const path{parent / ("run-" + std::to_string(stamp) + "-" +
                std::to_string(attempt))};
            if (fs::create_directory(path)) return path;
        }
        throw std::runtime_error{"cannot create a unique benchmark output directory"};
    }

    auto checked_result(fs::path const& metrics, fs::path const& frame,
        std::size_t input_size, std::uint64_t iterations) -> worker_result
    {
        auto const result{sph::zstd::eval::read_result(metrics)};
        if (result.elapsed_nanoseconds == 0U || result.peak_resident_bytes == 0U ||
            result.iterations != iterations ||
            result.input_bytes != input_size || result.output_bytes != fs::file_size(frame))
            throw std::runtime_error{"inconsistent worker metrics: " + metrics.string()};
        return result;
    }

    void check_exact_bytes(fs::path const& cpp_path, fs::path const& reference_path,
        unsigned trial)
    {
        auto const cpp{sph::zstd::eval::read_binary(cpp_path)};
        auto const reference{sph::zstd::eval::read_binary(reference_path)};
        auto const mismatch{std::mismatch(cpp.begin(), cpp.end(), reference.begin(), reference.end())};
        if (mismatch.first != cpp.end() || mismatch.second != reference.end())
        {
            auto const offset{static_cast<std::size_t>(mismatch.first - cpp.begin())};
            throw std::runtime_error{"byte parity failed at trial " + std::to_string(trial) +
                ", byte " + std::to_string(offset) + " (C++ " + std::to_string(cpp.size()) +
                " bytes, reference " + std::to_string(reference.size()) + " bytes)"};
        }
    }

    auto throughput(worker_result const& result) -> double
    {
        return static_cast<double>(result.input_bytes) *
            static_cast<double>(result.iterations) * 1.0e9 /
            static_cast<double>(result.elapsed_nanoseconds) / 1048576.0;
    }

    auto median_result(std::span<trial_result const> trials, bool cpp) -> worker_result
    {
        std::vector<worker_result> values;
        values.reserve(trials.size());
        for (auto const& trial : trials) values.push_back(cpp ? trial.cpp : trial.reference);
        return sph::zstd::eval::summarize_trials(values);
    }

    void write_report(fs::path const& output, std::span<trial_result const> trials,
        fs::path const& cpp_worker, fs::path const& reference_worker,
        unsigned level)
    {
        auto const cpp{median_result(trials, true)};
        auto const reference{median_result(trials, false)};
        auto const cpp_speed{throughput(cpp)};
        auto const reference_speed{throughput(reference)};
        auto const cpp_code{fs::file_size(cpp_worker)};
        auto const reference_code{fs::file_size(reference_worker)};
        std::ofstream csv{output / "trials.csv"};
        if (!csv) throw std::runtime_error{"cannot write trials.csv"};
        csv << "trial,first,cpp_elapsed_ns,reference_elapsed_ns,cpp_mib_s,reference_mib_s,"
            "cpp_peak_bytes,reference_peak_bytes,frame_bytes,exact_bytes\n";
        csv << std::fixed << std::setprecision(4);
        for (std::size_t index{}; index < trials.size(); ++index)
        {
            auto const& trial{trials[index]};
            csv << index + 1U << ',' << (trial.cpp_first ? "cpp" : "reference") << ','
                << trial.cpp.elapsed_nanoseconds << ',' << trial.reference.elapsed_nanoseconds << ','
                << throughput(trial.cpp) << ',' << throughput(trial.reference) << ','
                << trial.cpp.peak_resident_bytes << ',' << trial.reference.peak_resident_bytes << ','
                << trial.cpp.output_bytes << ",true\n";
        }
        if (!csv) throw std::runtime_error{"cannot complete trials.csv"};

        std::ofstream report{output / "SUMMARY.md"};
        if (!report) throw std::runtime_error{"cannot write SUMMARY.md"};
        report << std::fixed << std::setprecision(2);
        report << "# Paired compression benchmark — level " << level << "\n\n"
            << "Input: " << cpp.input_bytes << " bytes; " << trials.size()
            << " alternating trials of " << cpp.iterations << " iterations. "
            << "Frames matched byte-for-byte in every trial; both decoders recovered the input. "
            << "Workers were built with the same optimized CMake configuration.\n\n"
            << "| C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | "
            "C++ memory (MiB) | Reference memory (MiB) | % of Reference | "
            "C++ code (KiB) | Reference code (KiB) | % of Reference |\n"
            << "|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n"
            << "| " << cpp_speed << " | " << reference_speed << " | "
            << cpp_speed / reference_speed * 100.0 << " | "
            << static_cast<double>(cpp.peak_resident_bytes) / 1048576.0 << " | "
            << static_cast<double>(reference.peak_resident_bytes) / 1048576.0 << " | "
            << static_cast<double>(cpp.peak_resident_bytes) /
                static_cast<double>(reference.peak_resident_bytes) * 100.0 << " | "
            << static_cast<double>(cpp_code) / 1024.0 << " | "
            << static_cast<double>(reference_code) / 1024.0 << " | "
            << static_cast<double>(cpp_code) / static_cast<double>(reference_code) * 100.0
            << " |\n";
        if (!report) throw std::runtime_error{"cannot complete SUMMARY.md"};
        std::cout << "Byte parity and both decoders: PASS\n"
                  << "C++: " << std::fixed << std::setprecision(2) << cpp_speed << " MiB/s; "
                  << "reference: " << reference_speed << " MiB/s; "
                  << cpp_speed / reference_speed * 100.0 << "% of reference\n"
                  << "Report: " << (output / "SUMMARY.md").string() << '\n';
    }
}

int main(int argc, char** argv)
{
    try
    {
        auto const configuration{parse_options(argc, argv)};
#ifndef NDEBUG
        throw std::runtime_error{"zstdpp_pair requires an optimized Release build"};
#endif
        auto const program{fs::absolute(argv[0])};
        auto const directory{program.parent_path()};
        auto const extension{program.extension().string()};
        auto const suffix{"_l" + std::to_string(configuration.level)};
        auto const cpp_worker{worker_path(directory, "sph_compress" + suffix, extension)};
        auto const reference_worker{worker_path(directory, "reference_compress" + suffix, extension)};
        auto const cpp_decoder{worker_path(directory, "sph_decompress" + suffix, extension)};
        auto const reference_decoder{worker_path(directory, "reference_decompress" + suffix, extension)};
        auto const input{configuration.input ? sph::zstd::eval::read_binary(*configuration.input) :
            sph::zstd::eval::make_mixed_corpus(SPH_EVAL_CORPUS_SIZE)};
        if (input.size() != SPH_EVAL_CORPUS_SIZE)
            throw std::invalid_argument{"input has " + std::to_string(input.size()) +
                " bytes; this build requires " + std::to_string(SPH_EVAL_CORPUS_SIZE) +
                " bytes. Reconfigure with -DSPH_ZSTDPP_EVAL_CORPUS_SIZE=<bytes>."};
        auto const output{output_directory(configuration)};
        auto const input_path{output / "input.bin"};
        sph::zstd::eval::write_binary(input_path, input);
        std::vector<trial_result> results;
        results.reserve(configuration.trials);
        auto const iterations{std::to_string(configuration.iterations)};
        for (unsigned trial{1U}; trial <= configuration.trials; ++trial)
        {
            auto const cpp_first{trial % 2U != 0U};
            auto const stem{std::to_string(trial)};
            auto const cpp_frame{output / ("cpp-" + stem + ".zst")};
            auto const reference_frame{output / ("reference-" + stem + ".zst")};
            auto const cpp_metrics{output / ("cpp-" + stem + ".csv")};
            auto const reference_metrics{output / ("reference-" + stem + ".csv")};
            auto run_compress = [&](bool cpp)
            {
                run_worker(cpp ? cpp_worker : reference_worker,
                    {input_path, cpp ? cpp_frame : reference_frame, iterations,
                        cpp ? cpp_metrics : reference_metrics});
            };
            run_compress(cpp_first);
            run_compress(!cpp_first);
            check_exact_bytes(cpp_frame, reference_frame, trial);
            if (trial == 1U)
            {
                run_worker(cpp_decoder, {cpp_frame, input_path, "1", output / "cpp-decode-check.csv"});
                run_worker(reference_decoder,
                    {cpp_frame, input_path, "1", output / "reference-decode-check.csv"});
            }
            results.push_back({checked_result(cpp_metrics, cpp_frame, input.size(),
                configuration.iterations), checked_result(reference_metrics, reference_frame,
                input.size(), configuration.iterations), cpp_first});
            std::cout << "trial " << trial << '/' << configuration.trials << ": exact bytes PASS\n";
        }
        write_report(output, results, cpp_worker, reference_worker, configuration.level);
        return 0;
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
