// Included for each fingerprint storage order inside chunk_split_size().
            record(past, 0U);
            auto penalty{3};
            for (auto position{chunk_size}; position <= bytes.size() - chunk_size; position += chunk_size)
            {
                record(recent, position);
                if (different(past, recent, penalty))
                {
                    return position;
                }
                merge(past, recent);
                if (penalty > 0)
                {
                    --penalty;
                }
            }
            return bytes.size();
