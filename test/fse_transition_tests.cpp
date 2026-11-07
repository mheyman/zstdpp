#include <sph/zstd++/detail/zstd_compression.h>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace sph::zstd::detail;

void compare_runs(fse_compression_table const& table, unsigned alphabet, unsigned initial)
{
    fse_compression_state checked{table}, batched{table};
    checked.initialize(initial); batched.initialize(initial);
    forward_bit_writer expected, actual;
    unsigned random{12345U};
    for (unsigned length : {0U, 1U, 7U, 8U, 127U, 128U, 511U, 512U, 1024U, 2048U}) {
        random = random * 1664525U + 1013904223U;
        unsigned const symbol = table.run_length_symbol ? *table.run_length_symbol :
            ((length & 1U) == 0U ? initial : random % alphabet);
        for (unsigned i{}; i < length; ++i) {
            auto const emitted = checked.encode_bits(symbol);
            expected.append_unchecked(emitted.value, emitted.count);
        }
        batched.encode_zero_extra_run(symbol, length, actual);
    }
    checked.flush(expected); batched.flush(actual);
    if (expected.finish() != actual.finish()) throw std::runtime_error{"batched FSE payload or final state mismatch"};
}

void compare(fse_compression_table const& table, unsigned alphabet, unsigned initial)
{
    fse_compression_state checked{table}, validated{table};
    checked.initialize(initial);
    validated.initialize(initial);
    unsigned random{12345U};
    for (unsigned i{}; i < 4096U; ++i)
    {
        random = random * 1664525U + 1013904223U;
        auto const symbol{table.run_length_symbol ? *table.run_length_symbol : random % alphabet};
        auto const expected{checked.encode_bits(symbol)};
        auto const actual{validated.encode_bits<true>(symbol)};
        if (expected.count != actual.count || expected.value != actual.value)
            throw std::runtime_error{"FSE transition mismatch"};
    }
    forward_bit_writer left, right;
    checked.flush(left);
    validated.flush(right);
    if (left.finish() != right.finish()) throw std::runtime_error{"FSE final state mismatch"};
    compare_runs(table, alphabet, initial);
}

void compare_run_streams()
{
    std::size_t cases{};
    for (unsigned table_kind{}; table_kind < 3U; ++table_kind)
        for (unsigned count : {1U, 2U, 7U, 8U, 9U, 15U, 16U, 17U, 31U, 127U, 128U, 129U, 511U, 512U, 513U, 1024U, 4096U})
            for (unsigned pattern{}; pattern < (table_kind == 2U ? 1U : 4U); ++pattern)
                for (std::size_t prefix : {0U, 1U, 7U, 255U})
                {
                    auto const ll_table = make_run_length_fse_table(1U);
                    auto const of_table = make_run_length_fse_table(0U);
                    normalized_counts counts{};
                    counts.table_log = 9U;
                    counts.maximum_symbol = 35U;
                    counts.values[12] = 511;
                    counts.values[35] = -1;
                    auto const ml_table = table_kind == 0U ? build_fse_compression_table(counts) :
                        table_kind == 1U ? build_fse_compression_table(make_default_normalized_counts(match_length_default_norm, 6U)) :
                        make_run_length_fse_table(12U);
                    selected_sequence_table const ll{sequence_table_mode::run_length, &ll_table};
                    selected_sequence_table const of{sequence_table_mode::run_length, &of_table};
                    selected_sequence_table const ml{table_kind == 2U ? sequence_table_mode::run_length : sequence_table_mode::compressed, &ml_table};
                    compression_workspace workspace;
                    workspace.reserve(131072U, count);
                    parsed_block parsed;
                    for (unsigned i{}; i < count; ++i)
                    {
                        auto const exceptional = pattern == 1U ? i % 17U == 0U :
                            pattern == 2U ? (i & 1U) != 0U : pattern == 3U && (i == 0U || i + 2U == count);
                        auto const code = static_cast<std::uint8_t>(exceptional ? 35U : 12U);
                        parsed.sequences.emplace_back(1U, match_length_base[code] + (exceptional ? (i & 1U) : 0U), 1U, 1U);
                        workspace.literal_codes[i] = 1U;
                        workspace.offset_codes[i] = 0U;
                        workspace.match_codes[i] = code;
                    }
                    std::vector<std::uint8_t> expected(prefix, 0xA5U), actual(prefix, 0xA5U);
                    write_sequence_bitstream<false, false>(parsed, workspace, ll, of, ml, expected);
                    write_sequence_bitstream<true, true>(parsed, workspace, ll, of, ml, actual);
                    if (expected != actual) throw std::runtime_error{"batched sequence stream mismatch"};
                    ++cases;
                }
    std::cout << cases << " batched sequence stream boundary cases passed\n";
}

void run_tests()
{
    for (unsigned symbol{}; symbol < 256U; ++symbol)
    {
        auto const table{make_run_length_fse_table(symbol)};
        compare(table, 256U, symbol);
        fse_compression_state checked{table};
        checked.initialize(symbol);
        bool rejected{};
        try { (void)checked.encode_bits((symbol + 1U) % 256U); }
        catch (entropy_error const&) { rejected = true; }
        if (!rejected) throw std::runtime_error{"checked API lost RLE validation"};
        forward_bit_writer run_bits;
        rejected = false;
        try { checked.encode_zero_extra_run((symbol + 1U) % 256U, 1U, run_bits); }
        catch (entropy_error const&) { rejected = true; }
        if (!rejected) throw std::runtime_error{"batched API lost RLE validation"};
    }
    for (unsigned log{5U}; log <= 9U; ++log)
        for (unsigned alphabet : {2U, 7U, 17U, 29U})
        {
            normalized_counts counts{};
            counts.table_log = log;
            counts.maximum_symbol = alphabet - 1U;
            for (unsigned s{}; s < alphabet; ++s) counts.values[s] = 1;
            counts.values[0] = static_cast<std::int16_t>((1U << log) - alphabet + 1U);
            auto table{build_fse_compression_table(counts)};
            for (unsigned symbol{}; symbol < alphabet; ++symbol)
                for (std::uint32_t value{1U << log}; value < (2U << log); ++value)
                {
                    auto const transform = table.transforms[symbol];
                    auto const bits = (value + transform.delta_number_bits) >> 16U;
                    auto const wide = static_cast<std::int64_t>(value >> bits) + transform.delta_find_state;
                    auto const narrow = (value >> bits) + static_cast<std::uint32_t>(transform.delta_find_state);
                    if (wide < 0 || wide >= (1LL << log) || static_cast<std::uint64_t>(wide) != narrow)
                        throw std::runtime_error("index proof mismatch");
                }
            for (unsigned initial{}; initial < alphabet; ++initial) compare(table, alphabet, initial);
            table = make_run_length_fse_table(alphabet - 1U);
            compare(table, alphabet, alphabet - 1U);
        }
    std::uint32_t rng{0xDEADBEEFU};
    std::size_t verified{};
    for (unsigned log{5U}; log <= 9U; ++log)
        for (unsigned seed{}; seed < 32U; ++seed)
        {
            auto const table_size = 1U << log;
            rng = rng * 1664525U + 1013904223U;
            auto const alphabet = seed == 0U ? std::min(256U, table_size) :
                2U + rng % (std::min(256U, table_size) - 1U);
            normalized_counts counts{};
            counts.table_log = log;
            counts.maximum_symbol = alphabet - 1U;
            for (unsigned symbol{}; symbol < alphabet; ++symbol) counts.values[symbol] = 1;
            for (unsigned unit{alphabet}; unit < table_size; ++unit)
            {
                rng = rng * 1664525U + 1013904223U;
                ++counts.values[rng % alphabet];
            }
            // -1 is the low-probability normalized count; it still consumes
            // one state, but the table builder places it differently.
            for (unsigned symbol{}; symbol < alphabet; ++symbol)
                if (counts.values[symbol] == 1 && ((symbol + seed) & 1U) != 0U)
                    counts.values[symbol] = -1;
            auto const table = build_fse_compression_table(counts);
            for (unsigned symbol{}; symbol < alphabet; ++symbol)
            {
                for (std::uint32_t value{table_size}; value < 2U * table_size; ++value)
                {
                    auto const transform = table.transforms[symbol];
                    auto const bits = (value + transform.delta_number_bits) >> 16U;
                    auto const wide = static_cast<std::int64_t>(value >> bits) + transform.delta_find_state;
                    auto const narrow = (value >> bits) + static_cast<std::uint32_t>(transform.delta_find_state);
                    if (wide < 0 || wide >= table_size || static_cast<std::uint64_t>(wide) != narrow)
                        throw std::runtime_error("random normalized index proof mismatch");
                    ++verified;
                }
                compare(table, alphabet, symbol);
            }
        }
    std::cout << verified << " randomized/negative-one state-index cases passed\n";
    std::cout << "Validated FSE transitions, RLE reuse, and checked rejection pass\n";
}


}

int main()
{
    try { run_tests(); compare_run_streams(); }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
