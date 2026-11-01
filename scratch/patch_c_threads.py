with open('track-c/solution.cpp', 'rb') as f:
    content = f.read()

old_threads = b"""    const size_t num_threads = 12;
    std::vector<size_t> split_points(num_threads + 1, 0);
    split_points[0] = 0;
    split_points[num_threads] = mf.size;

    // Parallel split points calculation
    std::vector<std::thread> split_threads;
    split_threads.reserve(num_threads);
    for (size_t i = 1; i < num_threads; ++i) {
        split_threads.emplace_back([&, i]() {
            size_t target = i * (mf.size / num_threads);
            split_points[i] = find_fast_split(mf.data, mf.size, target);
        });
    }
    for (auto& th : split_threads) th.join();

    std::vector<std::vector<CountryStats>> thread_stats(num_threads, std::vector<CountryStats>(num_countries));
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            size_t start = split_points[t];
            size_t end = split_points[t + 1];
            if (start >= end) return;

            auto& stats = thread_stats[t];
            for (auto& cs : stats) {
                cs.name_hashes.reserve(128);
            }

            const char* p = mf.data + start;
            const char* limit = mf.data + end;

            if (t == 0) {
                if (limit - p >= 3 && (unsigned char)p[0] == 0xef && (unsigned char)p[1] == 0xbb && (unsigned char)p[2] == 0xbf) {
                    p += 3;
                }
                while (p < limit && *p != '\\n') p++;
                if (p < limit && *p == '\\n') p++;
            }

            while (p < limit) {
                if (*p == '\\r' || *p == '\\n') { p++; continue; }
                int64_t rec_id = parse_field_int(p, limit);
                uint64_t name_hash = 0;
                size_t name_len = 0;
                parse_field_name_hash(p, limit, name_hash, name_len);
                size_t note_len = parse_field_len_only(p, limit);
                int32_t reg_id = parse_field_int32(p, limit);
                int64_t cents = parse_field_cents(p, limit);

                uint16_t cid = (reg_id >= 0 && (size_t)reg_id < reg_table_size && region_to_country[reg_id] >= 0)
                               ? (uint16_t)region_to_country[reg_id] : unmatched_id;

                auto& cs = stats[cid];
                cs.count++;
                cs.sum_rec_id += rec_id;
                cs.sum_cents += cents;
                if (!cs.has_cents) {
                    cs.min_cents = cents;
                    cs.max_cents = cents;
                    cs.has_cents = true;
                } else {
                    if (cents < cs.min_cents) cs.min_cents = cents;
                    if (cents > cs.max_cents) cs.max_cents = cents;
                }
                cs.sum_name_len += name_len;
                cs.sum_note_len += note_len;
                cs.name_hashes.push_back(name_hash);
            }

            for (auto& cs : stats) {
                if (!cs.name_hashes.empty()) {
                    std::sort(cs.name_hashes.begin(), cs.name_hashes.end());
                    cs.name_hashes.erase(std::unique(cs.name_hashes.begin(), cs.name_hashes.end()), cs.name_hashes.end());
                }
            }
        });
    }

    for (auto& th : threads) th.join();
    mf.close(); // Immediately unmap records.csv to drop page cache

    // Phase 2: Parallel country reduction across 12 threads directly producing output strings
    std::vector<std::string> thread_outputs(num_threads);
    threads.clear();
    size_t c_chunk = (num_countries + num_threads - 1) / num_threads;

    for (size_t t = 0; t < num_threads; ++t) {
        size_t c_start = t * c_chunk;
        size_t c_end = std::min(num_countries, c_start + c_chunk);
        threads.emplace_back([&, t, c_start, c_end]() {
            if (c_start >= c_end) return;
            std::string& out = thread_outputs[t];
            out.reserve((c_end - c_start) * 256);
            char line_buf[2048];
            std::vector<uint64_t> all_hashes;
            all_hashes.reserve(4096);

            for (size_t cid = c_start; cid < c_end; ++cid) {
                CountryStats combined;
                for (size_t pt = 0; pt < num_threads; ++pt) {
                    const auto& ts = thread_stats[pt][cid];
                    if (ts.count == 0) continue;
                    combined.count += ts.count;
                    combined.sum_rec_id += ts.sum_rec_id;
                    combined.sum_cents += ts.sum_cents;
                    if (!combined.has_cents) {
                        combined.min_cents = ts.min_cents;
                        combined.max_cents = ts.max_cents;
                        combined.has_cents = true;
                    } else {
                        if (ts.min_cents < combined.min_cents) combined.min_cents = ts.min_cents;
                        if (ts.max_cents > combined.max_cents) combined.max_cents = ts.max_cents;
                    }
                    combined.sum_name_len += ts.sum_name_len;
                    combined.sum_note_len += ts.sum_note_len;
                }

                if (combined.count == 0) continue;

                // Robust Phase 2 deduplication
                size_t unique_names = 0;
                if (combined.count == 1) {
                    unique_names = 1;
                } else {
                    all_hashes.clear();
                    for (size_t pt = 0; pt < num_threads; ++pt) {
                        const auto& nh = thread_stats[pt][cid].name_hashes;
                        all_hashes.insert(all_hashes.end(), nh.begin(), nh.end());
                    }
                    std::sort(all_hashes.begin(), all_hashes.end());
                    all_hashes.erase(std::unique(all_hashes.begin(), all_hashes.end()), all_hashes.end());
                    unique_names = all_hashes.size();
                }

                const std::string& cname = country_names[cid];
                char* lp = line_buf;
                std::memcpy(lp, cname.data(), cname.size());
                lp += cname.size();
                *lp++ = ' ';
                lp = fast_i64(lp, combined.count);
                *lp++ = ' ';
                lp = fast_i64(lp, combined.sum_rec_id);
                *lp++ = ' ';
                lp = fast_format_cents(lp, combined.sum_cents);
                *lp++ = ' ';
                lp = fast_format_cents(lp, combined.min_cents);
                *lp++ = ' ';
                lp = fast_format_cents(lp, combined.max_cents);
                *lp++ = ' ';
                lp = fast_i64(lp, unique_names);
                *lp++ = ' ';
                lp = fast_i64(lp, combined.sum_name_len);
                *lp++ = ' ';
                lp = fast_i64(lp, combined.sum_note_len);
                *lp++ = '\\n';

                out.append(line_buf, lp - line_buf);
            }
        });
    }

    for (auto& th : threads) th.join();"""

new_threads = b"""    const size_t num_threads = 12;
    std::vector<size_t> split_points(num_threads + 1, 0);
    split_points[0] = 0;
    split_points[num_threads] = mf.size;

    // Fast sequential split points calculation (zero thread spawning overhead!)
    for (size_t i = 1; i < num_threads; ++i) {
        size_t target = i * (mf.size / num_threads);
        split_points[i] = find_fast_split(mf.data, mf.size, target);
    }

    std::barrier sync_point(num_threads);
    std::vector<std::string> thread_outputs(num_threads);
    std::vector<std::vector<CountryStats>> thread_stats(num_threads, std::vector<CountryStats>(num_countries));
    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    size_t c_chunk = (num_countries + num_threads - 1) / num_threads;

    // Single unified thread execution: Phase 1 (parse) + Barrier + Phase 2 (reduce)
    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            size_t start = split_points[t];
            size_t end = split_points[t + 1];
            if (start < end) {
                auto& stats = thread_stats[t];
                for (auto& cs : stats) {
                    cs.name_hashes.reserve(128);
                }

                const char* p = mf.data + start;
                const char* limit = mf.data + end;

                if (t == 0) {
                    if (limit - p >= 3 && (unsigned char)p[0] == 0xef && (unsigned char)p[1] == 0xbb && (unsigned char)p[2] == 0xbf) {
                        p += 3;
                    }
                    while (p < limit && *p != '\\n') p++;
                    if (p < limit && *p == '\\n') p++;
                }

                while (p < limit) {
                    if (*p == '\\r' || *p == '\\n') { p++; continue; }
                    int64_t rec_id = parse_field_int(p, limit);
                    uint64_t name_hash = 0;
                    size_t name_len = 0;
                    parse_field_name_hash(p, limit, name_hash, name_len);
                    size_t note_len = parse_field_len_only(p, limit);
                    int32_t reg_id = parse_field_int32(p, limit);
                    int64_t cents = parse_field_cents(p, limit);

                    uint16_t cid = (reg_id >= 0 && (size_t)reg_id < reg_table_size && region_to_country[reg_id] >= 0)
                                   ? (uint16_t)region_to_country[reg_id] : unmatched_id;

                    auto& cs = stats[cid];
                    cs.count++;
                    cs.sum_rec_id += rec_id;
                    cs.sum_cents += cents;
                    if (!cs.has_cents) {
                        cs.min_cents = cents;
                        cs.max_cents = cents;
                        cs.has_cents = true;
                    } else {
                        if (cents < cs.min_cents) cs.min_cents = cents;
                        if (cents > cs.max_cents) cs.max_cents = cents;
                    }
                    cs.sum_name_len += name_len;
                    cs.sum_note_len += note_len;
                    cs.name_hashes.push_back(name_hash);
                }

                for (auto& cs : stats) {
                    if (!cs.name_hashes.empty()) {
                        std::sort(cs.name_hashes.begin(), cs.name_hashes.end());
                        cs.name_hashes.erase(std::unique(cs.name_hashes.begin(), cs.name_hashes.end()), cs.name_hashes.end());
                    }
                }
            }

            // High-speed lock-free spin barrier
            sync_point.arrive_and_wait();

            // Phase 2: Parallel country reduction directly in the same thread
            size_t c_start = t * c_chunk;
            size_t c_end = std::min(num_countries, c_start + c_chunk);
            if (c_start < c_end) {
                std::string& out = thread_outputs[t];
                out.reserve((c_end - c_start) * 256);
                char line_buf[2048];
                std::vector<uint64_t> all_hashes;
                all_hashes.reserve(4096);

                for (size_t cid = c_start; cid < c_end; ++cid) {
                    CountryStats combined;
                    for (size_t pt = 0; pt < num_threads; ++pt) {
                        const auto& ts = thread_stats[pt][cid];
                        if (ts.count == 0) continue;
                        combined.count += ts.count;
                        combined.sum_rec_id += ts.sum_rec_id;
                        combined.sum_cents += ts.sum_cents;
                        if (!combined.has_cents) {
                            combined.min_cents = ts.min_cents;
                            combined.max_cents = ts.max_cents;
                            combined.has_cents = true;
                        } else {
                            if (ts.min_cents < combined.min_cents) combined.min_cents = ts.min_cents;
                            if (ts.max_cents > combined.max_cents) combined.max_cents = ts.max_cents;
                        }
                        combined.sum_name_len += ts.sum_name_len;
                        combined.sum_note_len += ts.sum_note_len;
                    }

                    if (combined.count == 0) continue;

                    size_t unique_names = 0;
                    if (combined.count == 1) {
                        unique_names = 1;
                    } else {
                        all_hashes.clear();
                        for (size_t pt = 0; pt < num_threads; ++pt) {
                            const auto& nh = thread_stats[pt][cid].name_hashes;
                            all_hashes.insert(all_hashes.end(), nh.begin(), nh.end());
                        }
                        std::sort(all_hashes.begin(), all_hashes.end());
                        all_hashes.erase(std::unique(all_hashes.begin(), all_hashes.end()), all_hashes.end());
                        unique_names = all_hashes.size();
                    }

                    const std::string& cname = country_names[cid];
                    char* lp = line_buf;
                    std::memcpy(lp, cname.data(), cname.size());
                    lp += cname.size();
                    *lp++ = ' ';
                    lp = fast_i64(lp, combined.count);
                    *lp++ = ' ';
                    lp = fast_i64(lp, combined.sum_rec_id);
                    *lp++ = ' ';
                    lp = fast_format_cents(lp, combined.sum_cents);
                    *lp++ = ' ';
                    lp = fast_format_cents(lp, combined.min_cents);
                    *lp++ = ' ';
                    lp = fast_format_cents(lp, combined.max_cents);
                    *lp++ = ' ';
                    lp = fast_i64(lp, unique_names);
                    *lp++ = ' ';
                    lp = fast_i64(lp, combined.sum_name_len);
                    *lp++ = ' ';
                    lp = fast_i64(lp, combined.sum_note_len);
                    *lp++ = '\\n';

                    out.append(line_buf, lp - line_buf);
                }
            }
        });
    }

    for (auto& th : threads) th.join();
    mf.close();"""

if old_threads in content:
    content = content.replace(old_threads, new_threads)
    print("Replaced old_threads (LF)")
elif old_threads.replace(b'\n', b'\r\n') in content:
    content = content.replace(old_threads.replace(b'\n', b'\r\n'), new_threads.replace(b'\n', b'\r\n'))
    print("Replaced old_threads (CRLF)")
else:
    print("old_threads NOT found")

with open('track-c/solution.cpp', 'wb') as f:
    f.write(content)
print("Updated thread pipeline in track-c/solution.cpp")
