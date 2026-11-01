with open('track-b/solution.cpp', 'rb') as f:
    content = f.read()

old_pass1 = b"""    std::string docs_path = data_dir + "/docs.txt";
    MappedFile mf;
    if (!mf.open(docs_path.c_str())) return 1;

    const size_t num_threads = 12;
    std::vector<size_t> split_points;
    split_points.push_back(0);

    for (size_t i = 1; i < num_threads; ++i) {
        size_t target = i * (mf.size / num_threads);
        const char* nl = (const char*)memchr(mf.data + target, '\\n', mf.size - target);
        if (nl) split_points.push_back((nl - mf.data) + 1);
        else split_points.push_back(mf.size);
    }
    split_points.push_back(mf.size);

    struct DocInfo {
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

    // Pass 1: Parse docs and collect postings and term counts in a single pass
    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            const char* p = mf.data + split_points[t];
            const char* limit = mf.data + split_points[t + 1];
            auto& t_idx = thread_indices[t];
            t_idx.term_counts.assign(num_terms, 0);
            t_idx.postings.reserve(450000);
            t_idx.docs.reserve(32768);

            std::vector<uint32_t> term_seen(num_terms, 0);
            uint32_t cur_epoch = 1;
            const char* last_madv_page = p;

            while (p < limit) {
                const char* nl = (const char*)memchr(p, '\\n', limit - p);
                const char* line_end = nl ? nl : limit;
                if (line_end > p && *(line_end - 1) == '\\r') line_end--;

                const char* tab = (const char*)memchr(p, '\\t', line_end - p);
                if (tab) {
                    uint32_t doc_id = 0;
                    const char* cur = p;
                    while (cur < tab && *cur >= '0' && *cur <= '9') {
                        doc_id = doc_id * 10 + (*cur - '0');
                        cur++;
                    }

                    const char* text_start = tab + 1;
                    uint32_t text_len = (uint32_t)(line_end - text_start);
                    uint32_t offset = (uint32_t)(text_start - mf.data);

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

                        // Fast 1-cycle L1 cache filter test (filters 95.8% of words!)
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
                p = nl ? nl + 1 : limit;
            }

#ifndef _WIN32
            uintptr_t page_start = (uintptr_t)(mf.data + split_points[t]) & ~4095ULL;
            uintptr_t page_end = ((uintptr_t)(mf.data + split_points[t + 1]) + 4095ULL) & ~4095ULL;
            madvise((void*)page_start, page_end - page_start, MADV_DONTNEED);
#endif
        });
    }"""

new_pass1 = b"""    std::string docs_path = data_dir + "/docs.txt";
    RawFile rf;
    if (!rf.open(docs_path.c_str())) return 1;

    const size_t num_threads = 12;
    std::vector<size_t> split_points(num_threads + 1, 0);
    split_points[0] = 0;
    split_points[num_threads] = rf.size;

    for (size_t i = 1; i < num_threads; ++i) {
        size_t target = i * (rf.size / num_threads);
        char probe[4096];
        size_t to_probe = std::min((size_t)4096, rf.size - target);
        ssize_t n = rf.pread_bytes(probe, to_probe, target);
        if (n > 0) {
            const char* nl = (const char*)std::memchr(probe, '\\n', n);
            if (nl) split_points[i] = target + (nl - probe) + 1;
            else split_points[i] = rf.size;
        } else {
            split_points[i] = rf.size;
        }
    }

    struct DocInfo {
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
    }"""

old_phrase = b"""                    if (num_pt == 1) {
                        temp.clear();
                        for (size_t di = 0; di < len0; ++di) {
                            uint32_t doc_id = ptr0[di];
                            uint32_t off = doc_offsets[doc_id];
                            uint32_t ln = doc_lens[doc_id];
                            if (match_phrase_fast(mf.data + off, ln, q.phrase_terms)) {
                                temp.push_back(doc_id);
                            }
                        }
                        cur_res.swap(temp);
                    } else {
                        auto [ptr1, len1] = get_postings(sorted_pterms[1]);
                        if (len1 == 0) {
                            out_str.append("0 0\\n", 4);
                            continue;
                        }
                        intersect_slices(ptr0, len0, ptr1, len1, cur_res);
                        for (size_t k = 2; k < num_pt; ++k) {
                            if (cur_res.empty()) break;
                            auto [ptr_k, len_k] = get_postings(sorted_pterms[k]);
                            if (len_k == 0) { cur_res.clear(); break; }
                            intersect_slices(cur_res.data(), cur_res.size(), ptr_k, len_k, temp);
                            cur_res.swap(temp);
                        }

                        if (!cur_res.empty()) {
                            temp.clear();
                            for (uint32_t doc_id : cur_res) {
                                uint32_t off = doc_offsets[doc_id];
                                uint32_t ln = doc_lens[doc_id];
                                if (match_phrase_fast(mf.data + off, ln, q.phrase_terms)) {
                                    temp.push_back(doc_id);
                                }
                            }
                            cur_res.swap(temp);
                        }
                    }"""

new_phrase = b"""                    char phrase_buf[8192];
                    auto check_doc_phrase = [&](uint32_t doc_id) -> bool {
                        uint32_t off = doc_offsets[doc_id];
                        uint32_t ln = doc_lens[doc_id];
                        if (ln <= sizeof(phrase_buf)) {
                            rf.pread_bytes(phrase_buf, ln, off);
                            return match_phrase_fast(phrase_buf, ln, q.phrase_terms);
                        } else {
                            std::vector<char> big_buf(ln);
                            rf.pread_bytes(big_buf.data(), ln, off);
                            return match_phrase_fast(big_buf.data(), ln, q.phrase_terms);
                        }
                    };

                    if (num_pt == 1) {
                        temp.clear();
                        for (size_t di = 0; di < len0; ++di) {
                            uint32_t doc_id = ptr0[di];
                            if (check_doc_phrase(doc_id)) {
                                temp.push_back(doc_id);
                            }
                        }
                        cur_res.swap(temp);
                    } else {
                        auto [ptr1, len1] = get_postings(sorted_pterms[1]);
                        if (len1 == 0) {
                            out_str.append("0 0\\n", 4);
                            continue;
                        }
                        intersect_slices(ptr0, len0, ptr1, len1, cur_res);
                        for (size_t k = 2; k < num_pt; ++k) {
                            if (cur_res.empty()) break;
                            auto [ptr_k, len_k] = get_postings(sorted_pterms[k]);
                            if (len_k == 0) { cur_res.clear(); break; }
                            intersect_slices(cur_res.data(), cur_res.size(), ptr_k, len_k, temp);
                            cur_res.swap(temp);
                        }

                        if (!cur_res.empty()) {
                            temp.clear();
                            for (uint32_t doc_id : cur_res) {
                                if (check_doc_phrase(doc_id)) {
                                    temp.push_back(doc_id);
                                }
                            }
                            cur_res.swap(temp);
                        }
                    }"""

if old_pass1 in content:
    content = content.replace(old_pass1, new_pass1)
    print("Pass 1 replaced (LF)")
elif old_pass1.replace(b'\n', b'\r\n') in content:
    content = content.replace(old_pass1.replace(b'\n', b'\r\n'), new_pass1.replace(b'\n', b'\r\n'))
    print("Pass 1 replaced (CRLF)")
else:
    print("Pass 1 NOT found")

if old_phrase in content:
    content = content.replace(old_phrase, new_phrase)
    print("Phrase replaced (LF)")
elif old_phrase.replace(b'\n', b'\r\n') in content:
    content = content.replace(old_phrase.replace(b'\n', b'\r\n'), new_phrase.replace(b'\n', b'\r\n'))
    print("Phrase replaced (CRLF)")
else:
    print("Phrase NOT found")

with open('track-b/solution.cpp', 'wb') as f:
    f.write(content)
print("Updated track-b/solution.cpp successfully!")
