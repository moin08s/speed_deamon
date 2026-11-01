with open('track-b/solution.cpp', 'r', encoding='utf-8') as f:
    text = f.read()

# 1. Update mallopt M_ARENA_MAX
text = text.replace('mallopt(M_ARENA_MAX, 1);', 'mallopt(M_ARENA_MAX, 2);')

# 2. Table replace
old_table = """static uint8_t IS_ALNUM[256];
static void init_tables() {
    static bool inited = false;
    if (inited) return;
    std::memset(IS_ALNUM, 0, 256);
    for (char c = 'a'; c <= 'z'; ++c) IS_ALNUM[(uint8_t)c] = 1;
    for (char c = '0'; c <= '9'; ++c) IS_ALNUM[(uint8_t)c] = 1;
    inited = true;
}"""

new_table = """static uint8_t CHAR_MAP[256];
static void init_tables() {
    static bool inited = false;
    if (inited) return;
    std::memset(CHAR_MAP, 0, 256);
    for (char c = '0'; c <= '9'; ++c) CHAR_MAP[(uint8_t)c] = (uint8_t)c;
    for (char c = 'a'; c <= 'z'; ++c) CHAR_MAP[(uint8_t)c] = (uint8_t)c;
    for (char c = 'A'; c <= 'Z'; ++c) CHAR_MAP[(uint8_t)c] = (uint8_t)(c + 32);
    inited = true;
}"""

assert old_table in text
text = text.replace(old_table, new_table)

# 3. Fast phrase char check
text = text.replace("IS_ALNUM[(uint8_t)to_lower_char(text[idx - 1])]", "CHAR_MAP[(uint8_t)text[idx - 1]]")
text = text.replace("IS_ALNUM[(uint8_t)to_lower_char(text[end_idx])]", "CHAR_MAP[(uint8_t)text[end_idx]]")
text = text.replace("!IS_ALNUM[(uint8_t)to_lower_char(text[cur_pos])]", "CHAR_MAP[(uint8_t)text[cur_pos]] == 0")
text = text.replace("IS_ALNUM[(uint8_t)to_lower_char(text[after_t])]", "CHAR_MAP[(uint8_t)text[after_t]]")
text = text.replace("!IS_ALNUM[(uint8_t)to_lower_char(*p)]", "CHAR_MAP[(uint8_t)*p] == 0")
text = text.replace("IS_ALNUM[(uint8_t)to_lower_char(*p)]", "CHAR_MAP[(uint8_t)*p]")

# 4. Inverted index single pass
lines = text.splitlines(keepends=True)
p_start = -1
p_end = -1
for i, line in enumerate(lines):
    if 'struct ThreadIndex {' in line:
        p_start = i
    if p_start != -1 and 'for (auto& th : threads) th.join();' in line and i > p_start + 100:
        p_end = i + 1
        break

print(f"p_start: {p_start}, p_end: {p_end}")
assert p_start != -1 and p_end != -1

single_pass_code = """    struct ThreadIndex {
        std::vector<DocInfo> docs;
        std::vector<std::vector<uint32_t>> postings;
    };

    std::vector<ThreadIndex> thread_indices(num_threads);
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    // Single Pass: Parse docs and collect postings directly
    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            const char* p = mf.data + split_points[t];
            const char* limit = mf.data + split_points[t + 1];
            auto& t_idx = thread_indices[t];
            t_idx.postings.resize(num_terms);

            std::vector<uint32_t> term_seen(num_terms, 0);
            uint32_t cur_epoch = 1;

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
                        int term_id = term_table.find(w_start, w_len, h);
                        if (term_id != -1) {
                            if (term_seen[term_id] != cur_epoch) {
                                term_seen[term_id] = cur_epoch;
                                t_idx.postings[term_id].push_back(doc_id);
                            }
                        }
                    }
                    cur_epoch++;
                }
                p = nl ? nl + 1 : limit;
            }
        });
    }

    for (auto& th : threads) th.join();

    // Assemble unified postings_data across monotonic thread ranges
    std::vector<uint32_t> term_offsets(num_terms + 1, 0);
    for (size_t term_id = 0; term_id < num_terms; ++term_id) {
        uint32_t cnt = 0;
        for (size_t t = 0; t < num_threads; ++t) {
            cnt += (uint32_t)thread_indices[t].postings[term_id].size();
        }
        term_offsets[term_id + 1] = term_offsets[term_id] + cnt;
    }

    uint32_t total_postings = term_offsets[num_terms];
    std::vector<uint32_t> postings_data(total_postings);

    for (size_t term_id = 0; term_id < num_terms; ++term_id) {
        uint32_t dest = term_offsets[term_id];
        for (size_t t = 0; t < num_threads; ++t) {
            const auto& post = thread_indices[t].postings[term_id];
            if (!post.empty()) {
                std::memcpy(&postings_data[dest], post.data(), post.size() * sizeof(uint32_t));
                dest += (uint32_t)post.size();
            }
        }
    }
"""

lines[p_start:p_end] = [single_pass_code]

with open('scratch/test_track_b_fast.cpp', 'w', encoding='utf-8') as f:
    f.writelines(lines)
print("Wrote scratch/test_track_b_fast.cpp successfully")
