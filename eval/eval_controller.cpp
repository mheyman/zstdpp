#include "eval_common.h"
#include "eval_corpus.h"
#include "eval_statistics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef SPH_EVAL_SOURCE_DIR
#error "SPH_EVAL_SOURCE_DIR must be defined"
#endif
#ifndef SPH_EVAL_BINARY_DIR
#error "SPH_EVAL_BINARY_DIR must be defined"
#endif
#ifndef SPH_EVAL_COMPILER
#error "SPH_EVAL_COMPILER must be defined"
#endif
#ifndef SPH_EVAL_BUILD_TYPE
#error "SPH_EVAL_BUILD_TYPE must be defined"
#endif
#ifndef SPH_EVAL_CORPUS_SIZE
#error "SPH_EVAL_CORPUS_SIZE must be defined"
#endif
#ifndef SPH_EVAL_LEVELS
#error "SPH_EVAL_LEVELS must be defined"
#endif

namespace
{
    constexpr std::array levels{SPH_EVAL_LEVELS};
    constexpr std::string_view report_begin{"<!-- SPH_ZSTDPP_EVAL_BEGIN -->"};
    constexpr std::string_view report_end{"<!-- SPH_ZSTDPP_EVAL_END -->"};

    struct level_results
    {
        int level{};
        std::uint64_t trial{};
        bool cpp_first{};
        sph::zstd::eval::worker_result sph_compress;
        sph::zstd::eval::worker_result reference_compress;
        sph::zstd::eval::worker_result sph_decompress;
        sph::zstd::eval::worker_result reference_decompress;
        std::uint64_t sph_compress_code{};
        std::uint64_t reference_compress_code{};
        std::uint64_t sph_decompress_code{};
        std::uint64_t reference_decompress_code{};
    };

    struct options
    {
        std::uint64_t iterations{20};
        std::uint64_t trials{6};
        std::filesystem::path output_directory{std::filesystem::path{SPH_EVAL_BINARY_DIR} / "eval" / "results"};
        std::filesystem::path report{std::filesystem::path{SPH_EVAL_SOURCE_DIR} / "BENCHMARKS.md"};
    };

    auto parse_options(int argc, char** argv) -> options
    {
        options result;
        for (int index{1}; index < argc; ++index)
        {
            std::string_view const argument{argv[index]};
            if (argument == "--iterations" && index + 1 < argc)
            {
                result.iterations = sph::zstd::eval::parse_iterations(argv[++index]);
            }
            else if (argument == "--trials" && index + 1 < argc)
            {
                auto const text{std::string_view{argv[++index]}};
                if (text.empty() || text.find_first_not_of("0123456789") != std::string_view::npos)
                    throw std::invalid_argument{"trials must be a positive integer"};
                result.trials = sph::zstd::eval::parse_iterations(text);
            }
            else if (argument == "--output" && index + 1 < argc)
            {
                result.output_directory = argv[++index];
            }
            else if ((argument == "--report" || argument == "--readme") && index + 1 < argc)
            {
                result.report = argv[++index];
            }
            else
            {
                throw std::invalid_argument{"usage: zstdpp_eval [--iterations N] [--trials N] [--output DIR] [--report FILE]"};
            }
        }
        return result;
    }

    auto quote(std::filesystem::path const& path) -> std::string
    {
        auto text{path.string()};
        if (text.find('"') != std::string::npos)
        {
            throw std::invalid_argument{"benchmark paths may not contain a quotation mark"};
        }
        return '"' + text + '"';
    }

    void run_worker(std::filesystem::path const& executable,
        std::initializer_list<std::filesystem::path> arguments)
    {
        std::string command{quote(executable)};
        for (auto const& argument : arguments)
        {
            command += ' ';
            command += quote(argument);
        }
#if defined(_WIN32)
        // cmd.exe removes the first and last quotation marks when a /c command begins with a
        // quoted executable. An outer pair preserves the quoting of every individual path.
        command = '"' + command + '"';
#endif
        std::cout << "Running " << executable.filename().string() << '\n';
        if (std::system(command.c_str()) != 0)
        {
            throw std::runtime_error{"benchmark worker failed: " + executable.string()};
        }
    }

    auto executable_path(std::filesystem::path const& directory, std::string const& name,
        std::string const& extension) -> std::filesystem::path
    {
        auto result{directory / name};
        result += extension;
        if (!std::filesystem::exists(result))
        {
            throw std::runtime_error{"benchmark executable does not exist: " + result.string()};
        }
        return result;
    }

    auto executable_size(std::filesystem::path const& path) -> std::uint64_t
    {
        auto const size{std::filesystem::file_size(path)};
        if (size > std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error{"executable size does not fit in uint64_t"};
        }
        return static_cast<std::uint64_t>(size);
    }

    auto throughput(std::uint64_t bytes, sph::zstd::eval::worker_result const& result) -> long double
    {
        if (result.elapsed_nanoseconds == 0)
        {
            return 0;
        }
        auto const value{static_cast<long double>(bytes) * static_cast<long double>(result.iterations) *
            1'000'000'000.0L / static_cast<long double>(result.elapsed_nanoseconds) / 1'048'576.0L};
        return value;
    }

    constexpr auto kibibytes(std::uint64_t bytes) -> long double { return static_cast<long double>(bytes) / 1024.0L; }
    constexpr auto mebibytes(std::uint64_t bytes) -> long double { return static_cast<long double>(bytes) / 1'048'576.0L; }

    auto percentage_of_reference(long double value, long double reference) -> std::string
    {
        if (reference == 0.0L) return "N/A";
        std::ostringstream output;
        output << std::fixed << std::setprecision(2) << 100.0L * value / reference << '%';
        return output.str();
    }

    auto make_report(std::vector<level_results> const& results, std::uint64_t iterations,
        std::vector<level_results> const& raw, std::uint64_t trials) -> std::string
    {
        std::vector<long double> sph_compressed_size;
        std::vector<long double> reference_compressed_size;
        std::vector<long double> sph_compress_speed;
        std::vector<long double> reference_compress_speed;
        std::vector<long double> sph_compress_memory;
        std::vector<long double> reference_compress_memory;
        std::vector<long double> sph_compress_code;
        std::vector<long double> reference_compress_code;
        std::vector<long double> sph_decompress_speed;
        std::vector<long double> reference_decompress_speed;
        std::vector<long double> sph_decompress_memory;
        std::vector<long double> reference_decompress_memory;
        std::vector<long double> sph_decompress_code;
        std::vector<long double> reference_decompress_code;

        for (auto const& result : results)
        {
            sph_compressed_size.push_back(kibibytes(result.sph_compress.output_bytes));
            reference_compressed_size.push_back(kibibytes(result.reference_compress.output_bytes));
            sph_compress_speed.push_back(throughput(result.sph_compress.input_bytes, result.sph_compress));
            reference_compress_speed.push_back(throughput(result.reference_compress.input_bytes, result.reference_compress));
            sph_compress_memory.push_back(mebibytes(result.sph_compress.peak_resident_bytes));
            reference_compress_memory.push_back(mebibytes(result.reference_compress.peak_resident_bytes));
            sph_compress_code.push_back(kibibytes(result.sph_compress_code));
            reference_compress_code.push_back(kibibytes(result.reference_compress_code));
            sph_decompress_speed.push_back(throughput(result.sph_decompress.output_bytes, result.sph_decompress));
            reference_decompress_speed.push_back(throughput(result.reference_decompress.output_bytes, result.reference_decompress));
            sph_decompress_memory.push_back(mebibytes(result.sph_decompress.peak_resident_bytes));
            reference_decompress_memory.push_back(mebibytes(result.reference_decompress.peak_resident_bytes));
            sph_decompress_code.push_back(kibibytes(result.sph_decompress_code));
            reference_decompress_code.push_back(kibibytes(result.reference_decompress_code));
        }

        std::ostringstream output;
        output << std::fixed << std::setprecision(2);
        output << report_begin << "\n\n## Current evaluation\n\n"
            << "Generated by `zstdpp_eval` using **" << SPH_EVAL_COMPILER << "**, "
            << SPH_EVAL_BUILD_TYPE << ", " << trials << " alternating trials of " << iterations
            << " timed iterations each, and a deterministic " << SPH_EVAL_CORPUS_SIZE << "-byte "
            << "mixed corpus. **Key: `C++` is sph-zstd++; `Reference` is reference zstd.** Code size is the complete worker "
            << "executable; memory is peak resident set "
            << "size, so both intentionally include runtime overhead. Each percentage is C++ / Reference * 100, "
            << "computed before display rounding. Higher is better for throughput; lower is better for memory and code size.\n\n"
            << "Throughput is derived from median elapsed time; memory is the median of process peak RSS. "
            << "For even trial counts, central values are averaged with integer rounding down. "
            << "Each trial alternates which implementation runs first, for both operations. "
            << "All measurements are preserved in `trials.csv` and per-trial worker CSVs; `metrics.csv` contains aggregates.\n\n"
            << "Compression workers receive identical input. Decompression workers both consume the same "
            << "reference-zstd-generated frame at each level, so their throughput and memory results use "
            << "identical compressed bytes.\n\n"
            << "### Compression\n\n"
            << "| Level | C++ size (KiB) | Reference size (KiB) | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |\n"
            << "|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
        for (std::size_t index{}; index < results.size(); ++index)
        {
            output << "| " << results[index].level
                << " | " << sph_compressed_size[index]
                << " | " << reference_compressed_size[index]
                << " | " << sph_compress_speed[index]
                << " | " << reference_compress_speed[index]
                << " | " << percentage_of_reference(sph_compress_speed[index], reference_compress_speed[index])
                << " | " << sph_compress_memory[index]
                << " | " << reference_compress_memory[index]
                << " | " << percentage_of_reference(sph_compress_memory[index], reference_compress_memory[index])
                << " | " << sph_compress_code[index]
                << " | " << reference_compress_code[index]
                << " | " << percentage_of_reference(sph_compress_code[index], reference_compress_code[index]) << " |\n";
        }
        output << "\n### Decompression\n\n"
            << "| Level | C++ throughput (MiB/s) | Reference throughput (MiB/s) | % of Reference | C++ memory (MiB) | Reference memory (MiB) | % of Reference | C++ code (KiB) | Reference code (KiB) | % of Reference |\n"
            << "|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
        for (std::size_t index{}; index < results.size(); ++index)
        {
            output << "| " << results[index].level
                << " | " << sph_decompress_speed[index]
                << " | " << reference_decompress_speed[index]
                << " | " << percentage_of_reference(sph_decompress_speed[index], reference_decompress_speed[index])
                << " | " << sph_decompress_memory[index]
                << " | " << reference_decompress_memory[index]
                << " | " << percentage_of_reference(sph_decompress_memory[index], reference_decompress_memory[index])
                << " | " << sph_decompress_code[index]
                << " | " << reference_decompress_code[index]
                << " | " << percentage_of_reference(sph_decompress_code[index], reference_decompress_code[index]) << " |\n";
        }
        output << "\n### Trial variability\n\n"
            << "| Level | Operation | C++ min–max (MiB/s) | Reference min–max (MiB/s) | C++ paired wins / trials |\n"
            << "|---:|---|---:|---:|---:|\n";
        for (auto const& result : results)
        {
            for (bool compress : {true, false})
            {
                long double cpp_min{std::numeric_limits<long double>::max()}, cpp_max{};
                long double reference_min{cpp_min}, reference_max{};
                std::uint64_t wins{};
                for (auto const& sample : raw)
                {
                    if (sample.level != result.level) continue;
                    auto const& cpp{compress ? sample.sph_compress : sample.sph_decompress};
                    auto const& reference{compress ? sample.reference_compress : sample.reference_decompress};
                    auto const cpp_rate{throughput(compress ? cpp.input_bytes : cpp.output_bytes, cpp)};
                    auto const reference_rate{throughput(compress ? reference.input_bytes : reference.output_bytes, reference)};
                    cpp_min = std::min(cpp_min, cpp_rate); cpp_max = std::max(cpp_max, cpp_rate);
                    reference_min = std::min(reference_min, reference_rate);
                    reference_max = std::max(reference_max, reference_rate);
                    if (cpp_rate > reference_rate) ++wins;
                }
                output << "| " << result.level << " | " << (compress ? "Compression" : "Decompression")
                    << " | " << cpp_min << "–" << cpp_max << " | " << reference_min << "–" << reference_max
                    << " | " << wins << " / " << trials << " |\n";
            }
        }
        output << '\n';
        output << report_end << '\n';
        return output.str();
    }

    auto read_text(std::filesystem::path const& path) -> std::string
    {
        std::ifstream input{path};
        if (!input) throw std::runtime_error{"cannot open benchmark report: " + path.string()};
        std::ostringstream contents;
        contents << input.rdbuf();
        if (input.bad() || contents.bad())
            throw std::runtime_error{"cannot read benchmark report: " + path.string()};
        return contents.str();
    }

    void update_report(std::filesystem::path const& path, std::string const& report)
    {
        auto contents{read_text(path)};
        auto const begin{contents.find(report_begin)};
        auto const end{contents.find(report_end)};
        if ((begin == std::string::npos) != (end == std::string::npos))
        {
            throw std::runtime_error{"benchmark evaluation markers are unbalanced"};
        }
        if (begin == std::string::npos)
        {
            if (!contents.empty() && contents.back() != '\n') contents += '\n';
            contents += '\n';
            contents += report;
        }
        else
        {
            contents.replace(begin, end + report_end.size() - begin, report);
        }
        while (!contents.empty() && (contents.back() == '\n' || contents.back() == '\r'))
        {
            contents.pop_back();
        }
        contents.push_back('\n');
        std::ofstream output{path, std::ios::trunc};
        if (!output) throw std::runtime_error{"cannot update benchmark report: " + path.string()};
        output << contents;
    }

    void write_metrics_markdown(std::filesystem::path const& path, std::string const& report)
    {
        auto const begin{report.find(report_begin)};
        auto const end{report.find(report_end)};
        if (begin == std::string::npos || end == std::string::npos || end < begin)
        {
            throw std::runtime_error{"generated evaluation report has invalid markers"};
        }
        auto content_begin{begin + report_begin.size()};
        while (content_begin < report.size() && (report[content_begin] == '\r' || report[content_begin] == '\n'))
        {
            ++content_begin;
        }

        std::ofstream output{path, std::ios::trunc};
        if (!output)
        {
            throw std::runtime_error{"cannot write evaluation Markdown: " + path.string()};
        }
        output << "# sph-zstd++ evaluation metrics\n\n"
            << "The raw values used by these tables are available in [metrics.csv](metrics.csv).\n\n"
            << report.substr(content_begin, end - content_begin);
    }

    void write_csv(std::filesystem::path const& path, std::vector<level_results> const& results, bool raw = false)
    {
        std::ofstream output{path, std::ios::trunc};
        if (!output) throw std::runtime_error{"cannot write evaluation CSV: " + path.string()};
        if (raw) output << "trial,cpp_first,";
        output << "level,operation,implementation,elapsed_ns,iterations,input_bytes,output_bytes,peak_bytes,code_bytes\n";
        for (auto const& result : results)
        {
            auto write = [&output, &result, raw](std::string_view operation, std::string_view implementation,
                sph::zstd::eval::worker_result const& metrics, std::uint64_t code_size)
            {
                if (raw) output << result.trial << ',' << (result.cpp_first ? 1 : 0) << ',';
                output << result.level << ',' << operation << ',' << implementation << ','
                    << metrics.elapsed_nanoseconds << ',' << metrics.iterations << ',' << metrics.input_bytes << ','
                    << metrics.output_bytes << ',' << metrics.peak_resident_bytes << ',' << code_size << '\n';
            };
            write("compress", "sph-zstd++", result.sph_compress, result.sph_compress_code);
            write("compress", "reference-zstd", result.reference_compress, result.reference_compress_code);
            write("decompress", "sph-zstd++", result.sph_decompress, result.sph_decompress_code);
            write("decompress", "reference-zstd", result.reference_decompress, result.reference_decompress_code);
        }
        if (!output) throw std::runtime_error{"cannot write evaluation CSV: " + path.string()};
    }
}

int main(int argc, char** argv)
{
    try
    {
        auto const configuration{parse_options(argc, argv)};
        if (std::filesystem::exists(configuration.output_directory) &&
            !std::filesystem::is_empty(configuration.output_directory))
            throw std::invalid_argument{"evaluation output directory must be empty to preserve raw trials"};
        std::filesystem::create_directories(configuration.output_directory);
        auto const corpus{sph::zstd::eval::make_mixed_corpus(SPH_EVAL_CORPUS_SIZE)};
        auto const corpus_path{configuration.output_directory / "corpus.bin"};
        sph::zstd::eval::write_binary(corpus_path, corpus);

        auto const controller_path{std::filesystem::absolute(argv[0])};
        auto const executable_directory{controller_path.parent_path()};
        auto const executable_extension{controller_path.extension().string()};
        auto const iterations_text{std::to_string(configuration.iterations)};
        std::vector<level_results> results;
        std::vector<level_results> raw;

        for (auto level : levels)
        {
            auto const suffix{"_l" + std::to_string(level)};
            auto const sph_compress_executable{executable_path(executable_directory, "sph_compress" + suffix,
                executable_extension)};
            auto const reference_compress_executable{executable_path(executable_directory,
                "reference_compress" + suffix, executable_extension)};
            auto const sph_decompress_executable{executable_path(executable_directory,
                "sph_decompress" + suffix, executable_extension)};
            auto const reference_decompress_executable{executable_path(executable_directory,
                "reference_decompress" + suffix, executable_extension)};

            auto const sph_frame{configuration.output_directory / ("sph" + suffix + ".zst")};
            auto const reference_frame{configuration.output_directory / ("reference" + suffix + ".zst")};
            std::vector<level_results> samples;
            for (std::uint64_t trial{}; trial < configuration.trials; ++trial)
            {
                auto const trial_suffix{suffix + "_trial" + std::to_string(trial + 1U)};
                auto const sph_compress_metrics{configuration.output_directory / ("sph_compress" + trial_suffix + ".csv")};
                auto const reference_compress_metrics{configuration.output_directory /
                    ("reference_compress" + trial_suffix + ".csv")};
                auto const sph_decompress_metrics{configuration.output_directory / ("sph_decompress" + trial_suffix + ".csv")};
                auto const reference_decompress_metrics{configuration.output_directory /
                    ("reference_decompress" + trial_suffix + ".csv")};

                auto const cpp_first{(trial + results.size()) % 2U == 0U};
                auto run_compress = [&](bool cpp) {
                    run_worker(cpp ? sph_compress_executable : reference_compress_executable,
                        {corpus_path, cpp ? sph_frame : reference_frame, iterations_text,
                            cpp ? sph_compress_metrics : reference_compress_metrics});
                };
                run_compress(cpp_first);
                run_compress(!cpp_first);
                if (sph::zstd::eval::read_binary(sph_frame) !=
                    sph::zstd::eval::read_binary(reference_frame))
                {
                    throw std::runtime_error{"compression byte parity failed at level " +
                        std::to_string(level) + "; benchmark report was not updated"};
                }
                auto run_decompress = [&](bool cpp) {
                    run_worker(cpp ? sph_decompress_executable : reference_decompress_executable,
                        {reference_frame, corpus_path, iterations_text,
                            cpp ? sph_decompress_metrics : reference_decompress_metrics});
                };
                run_decompress(cpp_first);
                run_decompress(!cpp_first);

                samples.push_back({
                    .level = level,
                    .trial = trial + 1U,
                    .cpp_first = cpp_first,
                    .sph_compress = sph::zstd::eval::read_result(sph_compress_metrics),
                    .reference_compress = sph::zstd::eval::read_result(reference_compress_metrics),
                    .sph_decompress = sph::zstd::eval::read_result(sph_decompress_metrics),
                    .reference_decompress = sph::zstd::eval::read_result(reference_decompress_metrics),
                    .sph_compress_code = executable_size(sph_compress_executable),
                    .reference_compress_code = executable_size(reference_compress_executable),
                    .sph_decompress_code = executable_size(sph_decompress_executable),
                    .reference_decompress_code = executable_size(reference_decompress_executable)
                });
                raw.push_back(samples.back());
                write_csv(configuration.output_directory / "trials.csv", raw, true);
            }
            auto aggregate = [&samples](sph::zstd::eval::worker_result level_results::* member) {
                std::vector<sph::zstd::eval::worker_result> workers;
                for (auto const& sample : samples) workers.push_back(sample.*member);
                return sph::zstd::eval::summarize_trials(workers);
            };
            auto summary{samples.front()};
            summary.trial = 0U;
            summary.sph_compress = aggregate(&level_results::sph_compress);
            summary.reference_compress = aggregate(&level_results::reference_compress);
            summary.sph_decompress = aggregate(&level_results::sph_decompress);
            summary.reference_decompress = aggregate(&level_results::reference_decompress);
            sph::zstd::eval::write_result(configuration.output_directory / ("sph_compress" + suffix + ".csv"), summary.sph_compress);
            sph::zstd::eval::write_result(configuration.output_directory / ("reference_compress" + suffix + ".csv"), summary.reference_compress);
            sph::zstd::eval::write_result(configuration.output_directory / ("sph_decompress" + suffix + ".csv"), summary.sph_decompress);
            sph::zstd::eval::write_result(configuration.output_directory / ("reference_decompress" + suffix + ".csv"), summary.reference_decompress);
            results.push_back(summary);
        }

        auto const report{make_report(results, configuration.iterations, raw, configuration.trials)};
        write_csv(configuration.output_directory / "metrics.csv", results);
        write_metrics_markdown(configuration.output_directory / "METRICS.md", report);
        update_report(configuration.report, report);
        std::cout << "Updated " << configuration.report.string() << '\n';
        std::cout << "Raw metrics: " << (configuration.output_directory / "metrics.csv").string() << '\n';
        std::cout << "Metrics report: " << (configuration.output_directory / "METRICS.md").string() << '\n';
        return 0;
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
