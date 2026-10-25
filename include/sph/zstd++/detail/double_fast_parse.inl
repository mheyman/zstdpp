// This body is instantiated twice inside double_fast_match_state. The ordinary
// instantiation preprocesses to its original tokens, preserving other levels'
// generated parser code; the selected collection path adds literal writes.
        template <bool UseTags, typename Block = parsed_block>
        [[nodiscard]] auto SPH_ZSTDPP_DOUBLE_FAST_PARSE_NAME(std::span<std::uint8_t const> input,
            std::size_t block_begin, std::size_t block_size,
            Block result = {}, std::optional<bool> known_rle = std::nullopt
#if SPH_ZSTDPP_DOUBLE_FAST_COLLECT
            , std::span<std::uint8_t> literal_storage = {}
#endif
            ) -> Block
        {
            result.sequences.clear();
            result.input_begin = block_begin;
            result.trailing_literal_position = block_begin;
            result.trailing_literal_length = block_size;
            if (block_size < hash_read_size || block_begin > input.size() ||
                block_size > input.size() - block_begin)
            {
                return result;
            }

            ensure_index_headroom(block_begin + block_size);

#if SPH_ZSTDPP_DOUBLE_FAST_COLLECT
            if (literal_storage.size() < block_size)
                throw entropy_error{"literal gathering storage is too small"};
            auto* literal_cursor{literal_storage.data()};
            auto const gather_literals{[&](std::size_t start, std::size_t count) {
                auto const remaining{literal_storage.size() -
                    static_cast<std::size_t>(literal_cursor - literal_storage.data())};
                if (count <= 16U && remaining >= 16U && input.size() - start >= 16U)
                    std::memcpy(literal_cursor, input.data() + start, 16U);
                else if (count != 0U)
                    std::memcpy(literal_cursor, input.data() + start, count);
                literal_cursor += count;
            }};
#endif

            if (block_size >= 1024U &&
                (known_rle.has_value() ? *known_rle : is_rle(input.subspan(block_begin, block_size))))
            {
                result.sequences.push_back(parsed_sequence{2U, block_size - 2U, 1U, 1U});
#if SPH_ZSTDPP_DOUBLE_FAST_COLLECT
                gather_literals(block_begin, 2U);
#endif
                result.trailing_literal_position = block_begin + block_size;
                result.trailing_literal_length = 0U;
                frame_extent_ = std::max(frame_extent_, static_cast<std::uint32_t>(block_begin + block_size));
                repeat_offsets_ = {1U, 4U, 8U};
                return result;
            }

            auto const block_end{block_begin + block_size};
            frame_extent_ = std::max(frame_extent_, static_cast<std::uint32_t>(block_end));
            constexpr auto configured_window_log{match_parameter<WindowLog>::resolve(0U)};
            auto const window_log{configured_window_log != 0U ? configured_window_log : window_log_};
            auto const prefix_start{block_end > (std::size_t{1} << window_log) ?
                block_end - (std::size_t{1} << window_log) : 0U};
            auto const frame_base{frame_index_base_};
            auto const local_position = [frame_base](std::uint32_t biased) -> std::size_t
            {
                return biased - frame_base - index_bias;
            };
            auto const lowest_biased{frame_base + prefix_start + index_bias};
            auto const limit{block_end - hash_read_size};
            auto* const long_table{long_table_.data()};
            auto* const short_table{short_table_.data()};
            auto* const long_tags{long_tags_.data()};
            auto* const short_tags{short_tags_.data()};
            auto const* const input_data{input.data()};
            auto anchor{block_begin};
            auto position{block_begin + static_cast<std::size_t>(block_begin == prefix_start)};
            auto repeat_one{repeat_offsets_[0]};
            auto repeat_two{repeat_offsets_[1]};
            std::size_t saved_one{};
            std::size_t saved_two{};
            auto const maximum_repeat{position - prefix_start};
            if (repeat_two > maximum_repeat) saved_two = repeat_two, repeat_two = 0U;
            if (repeat_one > maximum_repeat) saved_one = repeat_one, repeat_one = 0U;

            while (true)
            {
                std::size_t step{1U};
                auto next_step{position + search_step};
                auto next_position{position + step};
                if (next_position > limit)
                {
                    break;
                }

                auto long_hash_zero{long_hash(input_data + position)};
                auto long_index_zero{long_table[long_hash_zero]};
                std::uint32_t current_biased{};
                std::size_t match_length{};
                std::size_t offset{};
                std::size_t match_position{};
                std::size_t long_hash_one{};
                std::uint8_t repeat_code{};
                bool found{};

                do
                {
                    auto const short_hash_zero{short_hash(input_data + position)};
                    auto const short_index_zero{short_table[short_hash_zero]};
                    auto const previous_long_tag{UseTags ? long_tags[long_hash_zero] : std::uint8_t{}};
                    auto const previous_short_tag{UseTags ? short_tags[short_hash_zero] : std::uint8_t{}};
                    current_biased = static_cast<std::uint32_t>(
                        frame_base + position + index_bias);
                    long_table[long_hash_zero] = current_biased;
                    short_table[short_hash_zero] = current_biased;
                    if constexpr (UseTags)
                        long_tags[long_hash_zero] = short_tags[short_hash_zero] = input[position];

                    if (repeat_one != 0U && equal_four(input_data + position + 1U,
                        input_data + position + 1U - repeat_one))
                    {
                        ++position;
                        match_position = position - repeat_one;
                        match_length = count_match(input_data, position, match_position, block_end, 4U);
                        offset = repeat_one;
                        repeat_code = 1U;
                        found = true;
                        break;
                    }

                    long_hash_one = long_hash(input_data + next_position);
#if defined(SPH_ZSTDPP_DOUBLE_FAST_MASKED_TAG_GUARD)
                    if ((valid(long_index_zero, lowest_biased) &
                        (previous_long_tag == input[position])) &&
#else
                    if (valid(long_index_zero, lowest_biased) &&
                        (!UseTags || previous_long_tag == input[position]) &&
#endif
                        equal_eight(input_data + position, input_data + local_position(long_index_zero)))
                    {
                        match_position = local_position(long_index_zero);
                        match_length = count_match(input_data, position, match_position, block_end, 8U);
                        offset = position - match_position;
                        catch_up(input, position, match_position, anchor, prefix_start, match_length);
                        found = true;
                        break;
                    }

                    auto const long_index_one{long_table[long_hash_one]};
#if defined(SPH_ZSTDPP_DOUBLE_FAST_MASKED_TAG_GUARD)
                    if ((valid(short_index_zero, lowest_biased) &
                        (previous_short_tag == input[position])) &&
#else
                    if (valid(short_index_zero, lowest_biased) &&
                        (!UseTags || previous_short_tag == input[position]) &&
#endif
                        equal_four(input_data + position, input_data + local_position(short_index_zero)))
                    {
                        match_position = local_position(short_index_zero);
                        match_length = count_match(input_data, position, match_position, block_end, 4U);
                        offset = position - match_position;
#if defined(SPH_ZSTDPP_DOUBLE_FAST_MASKED_TAG_GUARD)
                        if (((long_index_one > lowest_biased) &
                            (long_tags[long_hash_one] == input[next_position])) &&
#else
                        if (long_index_one > lowest_biased &&
                            (!UseTags || long_tags[long_hash_one] == input[next_position]) &&
#endif
                            equal_eight(input_data + next_position, input_data + local_position(long_index_one)))
                        {
                            auto const next_match_position{local_position(long_index_one)};
                            auto const next_length{count_match(
                                input_data, next_position, next_match_position, block_end, 8U)};
                            if (next_length > match_length)
                            {
                                position = next_position;
                                match_position = next_match_position;
                                match_length = next_length;
                                offset = position - match_position;
                            }
                        }
                        catch_up(input, position, match_position, anchor, prefix_start, match_length);
                        found = true;
                        break;
                    }

                    if (next_position >= next_step)
                    {
                        ++step;
                        next_step += search_step;
                    }
                    position = next_position;
                    next_position += step;
                    long_hash_zero = long_hash_one;
                    long_index_zero = long_index_one;
                }
                while (next_position <= limit);

                if (!found)
                {
                    break;
                }

                if (repeat_code == 0U)
                {
                    repeat_two = repeat_one;
                    repeat_one = offset;
                    if (step < 4U)
                    {
                        long_table[long_hash_one] = static_cast<std::uint32_t>(
                            frame_base + next_position + index_bias);
                        if constexpr (UseTags) long_tags[long_hash_one] = input[next_position];
                    }
                }
#if SPH_ZSTDPP_DOUBLE_FAST_COLLECT
                gather_literals(anchor, position - anchor);
#endif
                result.sequences.push_back(parsed_sequence{
                    position - anchor, match_length, offset, repeat_code});
                position += match_length;
                anchor = position;

                if (position <= limit)
                {
                    auto const insertion_position{local_position(current_biased) + 2U};
                    auto const insertion_long_hash{long_hash(input_data + insertion_position)};
                    auto const insertion_short_hash{short_hash(input_data + insertion_position)};
                    long_table[insertion_long_hash] = short_table[insertion_short_hash] =
                        current_biased + 2U;
                    if constexpr (UseTags)
                        long_tags[insertion_long_hash] = short_tags[insertion_short_hash] =
                            input[insertion_position];
                    auto const position_long_hash{long_hash(input_data + position - 2U)};
                    long_table[position_long_hash] =
                        static_cast<std::uint32_t>(
                            frame_base + position - 2U + index_bias);
                    if constexpr (UseTags) long_tags[position_long_hash] = input[position - 2U];
                    auto const position_short_hash{short_hash(input_data + position - 1U)};
                    short_table[position_short_hash] =
                        static_cast<std::uint32_t>(
                            frame_base + position - 1U + index_bias);
                    if constexpr (UseTags) short_tags[position_short_hash] = input[position - 1U];

                    while (position <= limit && repeat_two != 0U &&
                        equal_four(input_data + position, input_data + position - repeat_two))
                    {
                        auto const repeat_length{count_match(
                            input_data, position, position - repeat_two, block_end, 4U)};
                        std::swap(repeat_one, repeat_two);
                        auto const repeat_long_hash{long_hash(input_data + position)};
                        auto const repeat_short_hash{short_hash(input_data + position)};
                        short_table[repeat_short_hash] = long_table[repeat_long_hash] =
                            static_cast<std::uint32_t>(frame_base + position + index_bias);
                        if constexpr (UseTags)
                            short_tags[repeat_short_hash] = long_tags[repeat_long_hash] = input[position];
                        result.sequences.push_back(parsed_sequence{
                            0U, repeat_length, repeat_one, 1U});
                        position += repeat_length;
                        anchor = position;
                    }
                }
            }

            saved_two = saved_one != 0U && repeat_one != 0U ? saved_one : saved_two;
            repeat_offsets_[0] = repeat_one != 0U ? repeat_one : saved_one;
            repeat_offsets_[1] = repeat_two != 0U ? repeat_two : saved_two;
            result.trailing_literal_position = anchor;
            result.trailing_literal_length = block_end - anchor;
#if SPH_ZSTDPP_DOUBLE_FAST_COLLECT
            if (!result.sequences.empty()) gather_literals(anchor, block_end - anchor);
#endif
            return result;
        }
