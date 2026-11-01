with open('track-b/solution.cpp', 'rb') as f:
    content = f.read()

old_block = b"""    struct DocInfo {
        uint32_t doc_id;
        uint32_t offset;
        uint32_t len;
    };

    struct Posting {
        uint32_t term_id;
        uint32_t doc_id;
    };

    struct ThreadIndex {
        std::vector<DocInfo> docs;
        std::vector<uint32_t> term_counts;
        std::vector<Posting> postings;
    };

    std::vector<ThreadIndex> thread_indices(num_threads);
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    // Pass 1: Streaming pread chunk parser (zero mmap of 254MB docs.txt!)
    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            size_t start = split_points[t];
            size_t end = split_points[t + 1];
            auto& t_idx = thread_indices[t];
            t_idx.term_counts.assign(num_terms, 0);
            t_idx.postings.reserve(262144);
            t_idx.docs.reserve(32768);

            std::vector<uint32_t> term_seen(num_terms, 0);
            uint32_t cur_epoch = 1;

            static constexpr size_t BUF_CAP = 1048576; // 1 MB
            std::vector<char> io_buf(BUF_CAP + 65536);
            size_t cur_file_pos = start;
            size_t leftover = 0;

            while (cur_file_pos < end || leftover > 0) {
                if (cur_file_pos < end) {
                    size_t read_bytes = std::min(BUF_CAP, end - cur_file_pos);
                    ssize_t n = rf.pread_bytes(io_buf.data() + leftover, read_bytes, cur_file_pos);
                    if (n <= 0 && leftover == 0) break;
                    if (n > 0) {
                        cur_file_pos += n;
                        leftover += n;
                    }
                }

                const char* buf_start = io_buf.data();
                const char* buf_limit = io_buf.data() + leftover;
                const char* p = buf_start;

                const char* parse_limit = buf_limit;
                if (cur_file_pos < end) {
                    const char* last_nl = nullptr;
                    for (const char* q = buf_limit - 1; q >= p; --q) {
                        if (*q == '\\n') { last_nl = q; break; }
                    }
                    if (last_nl) parse_limit = last_nl + 1;
                    else break;
                }

                while (p < parse_limit) {
                    const char* nl = (const char*)std::memchr(p, '\\n', parse_limit - p);
                    const char* line_end = nl ? nl : parse_limit;
                    if (line_end > p && *(line_end - 1) == '\\r') line_end--;

                    const char* tab = (const char*)std::memchr(p, '\\t', line_end - p);
                    if (tab) {
                        uint32_t doc_id = 0;
                        const char* cur = p;
                        while (cur < tab && *cur >= '0' && *cur <= '9') {
                            doc_id = doc_id * 10 + (*cur - '0');
                            cur++;
                        }

                        const char* text_start = tab + 1;
                        uint32_t text_len = (uint32_t)(line_end - text_start);
                        uint32_t offset = (uint32_t)((cur_file_pos - leftover) + (text_start - buf_start));

                        t_idx.docs.push_back({doc_id, offset, text_len});

                        if (cur_epoch == 0xFFFFFFF0) {
                            std::fill(term_seen.begin(), term_seen.end(), 0);
                            cur_epoch = 1;
                        }
                        const char* tp = text_start;
                        while (tp < line_end) {
                            while (tp < line_end && CHAR_MAP[(uint8_t)*tp] == 0) tp++;
                            if (tp >= line_end) break;
                            const char* w_start = tp;
                            uint64_t h = 0xcbf29ce484222325ULL;
                            while (tp < line_end) {
                                uint8_t ch = CHAR_MAP[(uint8_t)*tp];
                                if (ch == 0) break;
                                h = (h ^ ch) * 0x100000001b3ULL;
                                tp++;
                            }
                            size_t w_len = tp - w_start;

                            uint32_t bit_idx = (uint32_t)(h ^ (h >> 32)) & 262143;
                            if ((TERM_FILTER[bit_idx >> 6] & (1ULL << (bit_idx & 63))) != 0) {
                                int term_id = term_table.find(w_start, w_len, h);
                                if (term_id != -1) {
                                    if (term_seen[term_id] != cur_epoch) {
                                        term_seen[term_id] = cur_epoch;
                                        t_idx.term_counts[term_id]++;
                                        t_idx.postings.push_back({(uint32_t)term_id, doc_id});
                                    }
                                }
                            }
                        }
                        cur_epoch++;
                    }
                    p = nl ? nl + 1 : parse_limit;
                }

                size_t consumed = p - buf_start;
                leftover -= consumed;
                if (leftover > 0 && consumed > 0) {
                    std::memmove(io_buf.data(), buf_start + consumed, leftover);
                }
            }
        });
    }

    for (auto& th : threads) th.join();

    // Compute global term offsets and thread partition offsets
    std::vector<uint32_t> term_offsets(num_terms + 1, 0);
    std::vector<uint32_t> thread_start(num_threads * num_terms, 0);

    for (size_t term_id = 0; term_id < num_terms; ++term_id) {
        uint32_t cur = term_offsets[term_id];
        for (size_t t = 0; t < num_threads; ++t) {
            thread_start[t * num_terms + term_id] = cur;
            cur += thread_indices[t].term_counts[term_id];
        }
        term_offsets[term_id + 1] = cur;
    }

    // Free thread term_counts now
    for (size_t t = 0; t < num_threads; ++t) {
        thread_indices[t].term_counts.clear();
        thread_indices[t].term_counts.shrink_to_fit();
    }

    uint32_t total_postings = term_offsets[num_terms];
    std::vector<uint32_t> postings_data(total_postings);

    // Direct in-memory scatter into postings_data (no Pass 2 re-reading docs.txt!)
    threads.clear();
    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            uint32_t* starts = &thread_start[t * num_terms];
            const auto& my_postings = thread_indices[t].postings;
            for (const auto& p : my_postings) {
                uint32_t slot = starts[p.term_id]++;
                postings_data[slot] = p.doc_id;
            }
        });
    }
    for (auto& th : threads) th.join();

    // Free thread postings immediately before query processing
    for (size_t t = 0; t < num_threads; ++t) {
        thread_indices[t].postings.clear();
        thread_indices[t].postings.shrink_to_fit();
    }
    thread_start.clear();
    thread_start.shrink_to_fit();"""

new_block = b"""    struct DocInfo {
        uint32_t doc_id;
        uint32_t offset;
        uint32_t len;
    };

    struct ThreadIndex {
        std::vector<DocInfo> docs;
        std::vector<uint32_t> term_counts;
    };

    std::vector<ThreadIndex> thread_indices(num_threads);
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    // Pass 1: Streaming pread chunk parser (zero posting storage - only counts!)
    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            size_t start = split_points[t];
            size_t end = split_points[t + 1];
            auto& t_idx = thread_indices[t];
            t_idx.term_counts.assign(num_terms, 0);
            t_idx.docs.reserve(32768);

            std::vector<uint32_t> term_seen(num_terms, 0);
            uint32_t cur_epoch = 1;

            static constexpr size_t BUF_CAP = 1048576; // 1 MB
            std::vector<char> io_buf(BUF_CAP + 65536);
            size_t cur_file_pos = start;
            size_t leftover = 0;

            while (cur_file_pos < end || leftover > 0) {
                if (cur_file_pos < end) {
                    size_t read_bytes = std::min(BUF_CAP, end - cur_file_pos);
                    ssize_t n = rf.pread_bytes(io_buf.data() + leftover, read_bytes, cur_file_pos);
                    if (n <= 0 && leftover == 0) break;
                    if (n > 0) {
                        cur_file_pos += n;
                        leftover += n;
                    }
                }

                const char* buf_start = io_buf.data();
                const char* buf_limit = io_buf.data() + leftover;
                const char* p = buf_start;

                const char* parse_limit = buf_limit;
                if (cur_file_pos < end) {
                    const char* last_nl = nullptr;
                    for (const char* q = buf_limit - 1; q >= p; --q) {
                        if (*q == '\\n') { last_nl = q; break; }
                    }
                    if (last_nl) parse_limit = last_nl + 1;
                    else break;
                }

                while (p < parse_limit) {
                    const char* nl = (const char*)std::memchr(p, '\\n', parse_limit - p);
                    const char* line_end = nl ? nl : parse_limit;
                    if (line_end > p && *(line_end - 1) == '\\r') line_end--;

                    const char* tab = (const char*)std::memchr(p, '\\t', line_end - p);
                    if (tab) {
                        uint32_t doc_id = 0;
                        const char* cur = p;
                        while (cur < tab && *cur >= '0' && *cur <= '9') {
                            doc_id = doc_id * 10 + (*cur - '0');
                            cur++;
                        }

                        const char* text_start = tab + 1;
                        uint32_t text_len = (uint32_t)(line_end - text_start);
                        uint32_t offset = (uint32_t)((cur_file_pos - leftover) + (text_start - buf_start));

                        t_idx.docs.push_back({doc_id, offset, text_len});

                        if (cur_epoch == 0xFFFFFFF0) {
                            std::fill(term_seen.begin(), term_seen.end(), 0);
                            cur_epoch = 1;
                        }
                        const char* tp = text_start;
                        while (tp < line_end) {
                            while (tp < line_end && CHAR_MAP[(uint8_t)*tp] == 0) tp++;
                            if (tp >= line_end) break;
                            const char* w_start = tp;
                            uint64_t h = 0xcbf29ce484222325ULL;
                            while (tp < line_end) {
                                uint8_t ch = CHAR_MAP[(uint8_t)*tp];
                                if (ch == 0) break;
                                h = (h ^ ch) * 0x100000001b3ULL;
                                tp++;
                            }
                            size_t w_len = tp - w_start;

                            uint32_t bit_idx = (uint32_t)(h ^ (h >> 32)) & 262143;
                            if ((TERM_FILTER[bit_idx >> 6] & (1ULL << (bit_idx & 63))) != 0) {
                                int term_id = term_table.find(w_start, w_len, h);
                                if (term_id != -1) {
                                    if (term_seen[term_id] != cur_epoch) {
                                        term_seen[term_id] = cur_epoch;
                                        t_idx.term_counts[term_id]++;
                                    }
                                }
                            }
                        }
                        cur_epoch++;
                    }
                    p = nl ? nl + 1 : parse_limit;
                }

                size_t consumed = p - buf_start;
                leftover -= consumed;
                if (leftover > 0 && consumed > 0) {
                    std::memmove(io_buf.data(), buf_start + consumed, leftover);
                }
            }
        });
    }

    for (auto& th : threads) th.join();

    // Compute global term offsets and thread partition offsets
    std::vector<uint32_t> term_offsets(num_terms + 1, 0);
    std::vector<uint32_t> thread_start(num_threads * num_terms, 0);

    for (size_t term_id = 0; term_id < num_terms; ++term_id) {
        uint32_t cur = term_offsets[term_id];
        for (size_t t = 0; t < num_threads; ++t) {
            thread_start[t * num_terms + term_id] = cur;
            cur += thread_indices[t].term_counts[term_id];
        }
        term_offsets[term_id + 1] = cur;
    }

    // Free thread term_counts now
    for (size_t t = 0; t < num_threads; ++t) {
        thread_indices[t].term_counts.clear();
        thread_indices[t].term_counts.shrink_to_fit();
    }

    uint32_t total_postings = term_offsets[num_terms];
    std::vector<uint32_t> postings_data(total_postings);

    // Pass 2: Direct streaming scatter into postings_data (zero in-memory postings vector overhead!)
    threads.clear();
    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            uint32_t* starts = &thread_start[t * num_terms];
            const auto& my_docs = thread_indices[t].docs;

            std::vector<uint32_t> term_seen(num_terms, 0);
            uint32_t cur_epoch = 1;
            char doc_buf[65536];

            for (const auto& d : my_docs) {
                uint32_t doc_id = d.doc_id;
                const char* tp = nullptr;
                const char* line_end = nullptr;
                std::vector<char> big_buf;

                if (d.len <= sizeof(doc_buf)) {
                    rf.pread_bytes(doc_buf, d.len, d.offset);
                    tp = doc_buf;
                    line_end = doc_buf + d.len;
                } else {
                    big_buf.resize(d.len);
                    rf.pread_bytes(big_buf.data(), d.len, d.offset);
                    tp = big_buf.data();
                    line_end = big_buf.data() + d.len;
                }

                if (cur_epoch == 0xFFFFFFF0) {
                    std::fill(term_seen.begin(), term_seen.end(), 0);
                    cur_epoch = 1;
                }
                while (tp < line_end) {
                    while (tp < line_end && CHAR_MAP[(uint8_t)*tp] == 0) tp++;
                    if (tp >= line_end) break;
                    const char* w_start = tp;
                    uint64_t h = 0xcbf29ce484222325ULL;
                    while (tp < line_end) {
                        uint8_t ch = CHAR_MAP[(uint8_t)*tp];
                        if (ch == 0) break;
                        h = (h ^ ch) * 0x100000001b3ULL;
                        tp++;
                    }
                    size_t w_len = tp - w_start;

                    uint32_t bit_idx = (uint32_t)(h ^ (h >> 32)) & 262143;
                    if ((TERM_FILTER[bit_idx >> 6] & (1ULL << (bit_idx & 63))) != 0) {
                        int term_id = term_table.find(w_start, w_len, h);
                        if (term_id != -1) {
                            if (term_seen[term_id] != cur_epoch) {
                                term_seen[term_id] = cur_epoch;
                                uint32_t slot = starts[term_id]++;
                                postings_data[slot] = doc_id;
                            }
                        }
                    }
                }
                cur_epoch++;
            }
        });
    }
    for (auto& th : threads) th.join();

    thread_start.clear();
    thread_start.shrink_to_fit();"""

if old_block in content:
    content = content.replace(old_block, new_block)
    print("Replaced LF")
elif old_block.replace(b'\n', b'\r\n') in content:
    content = content.replace(old_block.replace(b'\n', b'\r\n'), new_block.replace(b'\n', b'\r\n'))
    print("Replaced CRLF")
else:
    print("NOT found")

with open('track-b/solution.cpp', 'wb') as f:
    f.write(content)
print("Updated track-b/solution.cpp successfully!")
