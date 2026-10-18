#include <sph/zstd++/detail/zstd_compression.h>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace sph::zstd::detail;

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
    try { run_tests(); }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
