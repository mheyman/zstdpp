#include "eval_common.h"

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

namespace
{
    constexpr std::array levels{1, 3, 5, 9, 15};
    constexpr std::string_view report_begin{"<!-- SPH_ZSTDPP_EVAL_BEGIN -->"};
    constexpr std::string_view report_end{"<!-- SPH_ZSTDPP_EVAL_END -->"};

    struct level_results
    {
        int level{};
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
        std::filesystem::path output_directory{std::filesystem::path{SPH_EVAL_BINARY_DIR} / "eval" / "results"};
        std::filesystem::path readme{std::filesystem::path{SPH_EVAL_SOURCE_DIR} / "README.md"};
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
            else if (argument == "--output" && index + 1 < argc)
            {
                result.output_directory = argv[++index];
            }
            else if (argument == "--readme" && index + 1 < argc)
            {
                result.readme = argv[++index];
            }
            else
            {
                throw std::invalid_argument{"usage: zstdpp_eval [--iterations N] [--output DIR] [--readme FILE]"};
            }
        }
        return result;
    }

    auto make_corpus() -> std::vector<std::uint8_t>
    {
        static_assert(SPH_EVAL_CORPUS_SIZE % 4 == 0);
        std::vector<std::uint8_t> corpus(SPH_EVAL_CORPUS_SIZE);
        auto const quarter{corpus.size() / 4U};

        std::fill_n(corpus.begin(), static_cast<std::ptrdiff_t>(quarter), std::uint8_t{});
        constexpr std::string_view phrase{"The quick brown fox jumps over the lazy dog. Zstandard C++ evaluation.\n"};
        for (std::size_t index{quarter}; index < quarter * 2U; ++index)
        {
            corpus[index] = static_cast<std::uint8_t>(phrase[(index - quarter) % phrase.size()]);
        }
        for (std::size_t index{quarter * 2U}; index < quarter * 3U; ++index)
        {
            auto const relative{index - quarter * 2U};
            corpus[index] = static_cast<std::uint8_t>((relative / 16U + relative % 7U) & 0xFFU);
        }
        std::uint32_t random{0xC001D00DU};
        for (std::size_t index{quarter * 3U}; index < corpus.size(); ++index)
        {
            random ^= random << 13U;
            random ^= random >> 17U;
            random ^= random << 5U;
            corpus[index] = static_cast<std::uint8_t>(random);
        }
        return corpus;
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

    auto throughput(std::uint64_t bytes, sph::zstd::eval::worker_result const& result) -> std::uint64_t
    {
        if (result.elapsed_nanoseconds == 0)
        {
            return 0;
        }
        auto const value{static_cast<long double>(bytes) * static_cast<long double>(result.iterations) *
            1'000'000'000.0L / static_cast<long double>(result.elapsed_nanoseconds) / 1'048'576.0L};
        return static_cast<std::uint64_t>(std::llround(value));
    }

    constexpr auto kibibytes(std::uint64_t bytes) -> std::uint64_t { return (bytes + 1023U) / 1024U; }
    constexpr auto mebibytes(std::uint64_t bytes) -> std::uint64_t { return (bytes + 1'048'575U) / 1'048'576U; }

    auto values(std::vector<std::uint64_t> const& items) -> std::string
    {
        std::ostringstream output;
        output << '[';
        for (std::size_t index{}; index < items.size(); ++index)
        {
            if (index != 0) output << ", ";
            output << items[index];
        }
        output << ']';
        return output.str();
    }

    auto chart(std::string_view title, std::string_view unit,
        std::vector<std::uint64_t> const& sph_values,
        std::vector<std::uint64_t> const& reference_values) -> std::string
    {
        if (sph_values.size() != reference_values.size())
        {
            throw std::invalid_argument{"chart series must contain the same number of values"};
        }
        std::vector<std::uint64_t> interleaved_values;
        interleaved_values.reserve(sph_values.size() * 2U);
        for (std::size_t index{}; index < sph_values.size(); ++index)
        {
            interleaved_values.push_back(sph_values[index]);
            interleaved_values.push_back(reference_values[index]);
        }

        auto maximum = std::uint64_t{1};
        for (auto value : interleaved_values) maximum = std::max(maximum, value);
        maximum += maximum / 10U + 1U;

        std::ostringstream output;
        output << "```mermaid\nxychart-beta\n"
            << "    title \"" << title << "\"\n"
            << "    x-axis [L1_CPP, L1_REF, L3_CPP, L3_REF, L5_CPP, L5_REF, L9_CPP, L9_REF, L15_CPP, L15_REF]\n"
            << "    y-axis \"" << unit << "\" 0 --> " << maximum << "\n"
            << "    bar " << values(interleaved_values) << "\n"
            << "```\n\n";
        return output.str();
    }

    auto make_report(std::vector<level_results> const& results, std::uint64_t iterations) -> std::string
    {
        std::vector<std::uint64_t> sph_compressed_size;
        std::vector<std::uint64_t> reference_compressed_size;
        std::vector<std::uint64_t> sph_compress_speed;
        std::vector<std::uint64_t> reference_compress_speed;
        std::vector<std::uint64_t> sph_compress_memory;
        std::vector<std::uint64_t> reference_compress_memory;
        std::vector<std::uint64_t> sph_compress_code;
        std::vector<std::uint64_t> reference_compress_code;
        std::vector<std::uint64_t> sph_decompress_speed;
        std::vector<std::uint64_t> reference_decompress_speed;
        std::vector<std::uint64_t> sph_decompress_memory;
        std::vector<std::uint64_t> reference_decompress_memory;
        std::vector<std::uint64_t> sph_decompress_code;
        std::vector<std::uint64_t> reference_decompress_code;

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
        output << report_begin << "\n\n## Current evaluation\n\n"
            << "Generated by `zstdpp_eval` using **" << SPH_EVAL_COMPILER << "**, "
            << SPH_EVAL_BUILD_TYPE << ", " << iterations << " timed iterations, and a deterministic 1 MiB "
            << "mixed corpus. **Key: `CPP` is sph-zstd++; `REF` is reference zstd.** Each compression-level "
            << "pair is displayed side-by-side with the C++ value first. Code size is the complete worker "
            << "executable; memory is peak resident set "
            << "size, so both intentionally include runtime overhead.\n\n"
            << "Compression workers receive identical input. Decompression workers both consume the same "
            << "sph-zstd++-generated frame at each level because general reference-compressed blocks are not "
            << "supported by the C++ decoder yet.\n\n"
            << "### Compression\n\n";
        output << chart("Compressed size", "KiB", sph_compressed_size, reference_compressed_size);
        output << chart("Compression throughput", "MiB/s", sph_compress_speed, reference_compress_speed);
        output << chart("Compression peak memory", "MiB", sph_compress_memory, reference_compress_memory);
        output << chart("Compression executable size", "KiB", sph_compress_code, reference_compress_code);
        output << "### Decompression\n\n";
        output << chart("Decompression throughput", "MiB/s", sph_decompress_speed, reference_decompress_speed);
        output << chart("Decompression peak memory", "MiB", sph_decompress_memory, reference_decompress_memory);
        output << chart("Decompression executable size", "KiB", sph_decompress_code, reference_decompress_code);
        output << report_end << '\n';
        return output.str();
    }

    auto read_text(std::filesystem::path const& path) -> std::string
    {
        std::ifstream input{path};
        if (!input) throw std::runtime_error{"cannot open README: " + path.string()};
        return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    }

    void update_readme(std::filesystem::path const& path, std::string const& report)
    {
        auto contents{read_text(path)};
        auto const begin{contents.find(report_begin)};
        auto const end{contents.find(report_end)};
        if ((begin == std::string::npos) != (end == std::string::npos))
        {
            throw std::runtime_error{"README evaluation markers are unbalanced"};
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
        if (!output) throw std::runtime_error{"cannot update README: " + path.string()};
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
            << "The raw values used by these charts are available in [metrics.csv](metrics.csv).\n\n"
            << report.substr(content_begin, end - content_begin);
    }

    void write_csv(std::filesystem::path const& path, std::vector<level_results> const& results)
    {
        std::ofstream output{path, std::ios::trunc};
        if (!output) throw std::runtime_error{"cannot write evaluation CSV: " + path.string()};
        output << "level,operation,implementation,elapsed_ns,iterations,input_bytes,output_bytes,peak_bytes,code_bytes\n";
        for (auto const& result : results)
        {
            auto write = [&output, &result](std::string_view operation, std::string_view implementation,
                sph::zstd::eval::worker_result const& metrics, std::uint64_t code_size)
            {
                output << result.level << ',' << operation << ',' << implementation << ','
                    << metrics.elapsed_nanoseconds << ',' << metrics.iterations << ',' << metrics.input_bytes << ','
                    << metrics.output_bytes << ',' << metrics.peak_resident_bytes << ',' << code_size << '\n';
            };
            write("compress", "sph-zstd++", result.sph_compress, result.sph_compress_code);
            write("compress", "reference-zstd", result.reference_compress, result.reference_compress_code);
            write("decompress", "sph-zstd++", result.sph_decompress, result.sph_decompress_code);
            write("decompress", "reference-zstd", result.reference_decompress, result.reference_decompress_code);
        }
    }
}

int main(int argc, char** argv)
{
    try
    {
        auto const configuration{parse_options(argc, argv)};
        std::filesystem::create_directories(configuration.output_directory);
        auto const corpus{make_corpus()};
        auto const corpus_path{configuration.output_directory / "corpus.bin"};
        sph::zstd::eval::write_binary(corpus_path, corpus);

        auto const controller_path{std::filesystem::absolute(argv[0])};
        auto const executable_directory{controller_path.parent_path()};
        auto const executable_extension{controller_path.extension().string()};
        auto const iterations_text{std::to_string(configuration.iterations)};
        std::vector<level_results> results;

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
            auto const sph_compress_metrics{configuration.output_directory / ("sph_compress" + suffix + ".csv")};
            auto const reference_compress_metrics{configuration.output_directory /
                ("reference_compress" + suffix + ".csv")};
            auto const sph_decompress_metrics{configuration.output_directory / ("sph_decompress" + suffix + ".csv")};
            auto const reference_decompress_metrics{configuration.output_directory /
                ("reference_decompress" + suffix + ".csv")};

            run_worker(sph_compress_executable,
                {corpus_path, sph_frame, iterations_text, sph_compress_metrics});
            run_worker(reference_compress_executable,
                {corpus_path, reference_frame, iterations_text, reference_compress_metrics});
            run_worker(sph_decompress_executable,
                {sph_frame, corpus_path, iterations_text, sph_decompress_metrics});
            run_worker(reference_decompress_executable,
                {sph_frame, corpus_path, iterations_text, reference_decompress_metrics});

            results.push_back({
                .level = level,
                .sph_compress = sph::zstd::eval::read_result(sph_compress_metrics),
                .reference_compress = sph::zstd::eval::read_result(reference_compress_metrics),
                .sph_decompress = sph::zstd::eval::read_result(sph_decompress_metrics),
                .reference_decompress = sph::zstd::eval::read_result(reference_decompress_metrics),
                .sph_compress_code = executable_size(sph_compress_executable),
                .reference_compress_code = executable_size(reference_compress_executable),
                .sph_decompress_code = executable_size(sph_decompress_executable),
                .reference_decompress_code = executable_size(reference_decompress_executable)
            });
        }

        auto const report{make_report(results, configuration.iterations)};
        write_csv(configuration.output_directory / "metrics.csv", results);
        write_metrics_markdown(configuration.output_directory / "METRICS.md", report);
        update_readme(configuration.readme, report);
        std::cout << "Updated " << configuration.readme.string() << '\n';
        std::cout << "Raw metrics: " << (configuration.output_directory / "metrics.csv").string() << '\n';
        std::cout << "Metrics charts: " << (configuration.output_directory / "METRICS.md").string() << '\n';
        return 0;
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
