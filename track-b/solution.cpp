#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <string_view>
#include <thread>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

struct MappedFile {
    const char* data = nullptr;
    size_t size = 0;
#ifdef _WIN32
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hMap = NULL;
#else
    int fd = -1;
#endif

    bool open(const char* path) {
#ifdef _WIN32
        hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER li;
        GetFileSizeEx(hFile, &li);
        size = li.QuadPart;
        if (size == 0) return true;
        hMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
        if (!hMap) { CloseHandle(hFile); return false; }
        data = (const char*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, size);
        return data != nullptr;
#else
        fd = ::open(path, O_RDONLY);
        if (fd < 0) return false;
        struct stat st;
        if (fstat(fd, &st) < 0) { ::close(fd); return false; }
        size = st.st_size;
        if (size == 0) return true;
        data = (const char*)mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        return data != MAP_FAILED;
#endif
    }

    void close() {
#ifdef _WIN32
        if (data) { UnmapViewOfFile(data); data = nullptr; }
        if (hMap) { CloseHandle(hMap); hMap = NULL; }
        if (hFile != INVALID_HANDLE_VALUE) { CloseHandle(hFile); hFile = INVALID_HANDLE_VALUE; }
#else
        if (data && data != MAP_FAILED) { munmap((void*)data, size); data = nullptr; }
        if (fd >= 0) { ::close(fd); fd = -1; }
#endif
        size = 0;
    }

    ~MappedFile() {
        close();
    }
};

static uint8_t IS_ALNUM[256];
static void init_tables() {
    static bool inited = false;
    if (inited) return;
    std::memset(IS_ALNUM, 0, 256);
    for (char c = 'a'; c <= 'z'; ++c) IS_ALNUM[(uint8_t)c] = 1;
    for (char c = '0'; c <= '9'; ++c) IS_ALNUM[(uint8_t)c] = 1;
    inited = true;
}

inline char to_lower_char(char c) {
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

inline char* fast_u32(char* p, uint32_t val) {
    if (val == 0) {
        *p++ = '0';
        return p;
    }
    char buf[10];
    int len = 0;
    while (val > 0) {
        buf[len++] = '0' + (val % 10);
        val /= 10;
    }
    for (int i = len - 1; i >= 0; --i) {
        *p++ = buf[i];
    }
    return p;
}

inline char* fast_u64(char* p, uint64_t val) {
    if (val == 0) {
        *p++ = '0';
        return p;
    }
    char buf[20];
    int len = 0;
    while (val > 0) {
        buf[len++] = '0' + (val % 10);
        val /= 10;
    }
    for (int i = len - 1; i >= 0; --i) {
        *p++ = buf[i];
    }
    return p;
}

struct FastTermTable {
    static constexpr uint32_t CAPACITY = 524288;
    static constexpr uint32_t MASK = CAPACITY - 1;

    struct Entry {
        uint64_t hash = 0;
        uint32_t len = 0;
        std::string word;
        int id = -1;
    };

    std::vector<Entry> table;
    int count = 0;

    FastTermTable() : table(CAPACITY) {}

    static inline uint64_t hash_term(const char* p, size_t len) {
        uint64_t h = 0xcbf29ce484222325ULL;
        for (size_t i = 0; i < len; ++i) {
            h = (h ^ (unsigned char)p[i]) * 0x100000001b3ULL;
        }
        return h ? h : 1;
    }

    int insert(const std::string& s) {
        uint64_t h = hash_term(s.data(), s.size());
        uint32_t idx = (uint32_t)h & MASK;
        while (table[idx].hash != 0) {
            if (table[idx].hash == h && table[idx].len == s.size() && table[idx].word == s) {
                return table[idx].id;
            }
            idx = (idx + 1) & MASK;
        }
        table[idx].hash = h;
        table[idx].len = (uint32_t)s.size();
        table[idx].word = s;
        table[idx].id = count++;
        return table[idx].id;
    }

    inline int find(const char* p, size_t len, uint64_t h) const {
        uint32_t idx = (uint32_t)h & MASK;
        while (table[idx].hash != 0) {
            if (table[idx].hash == h && table[idx].len == len) {
                bool eq = true;
                const char* tw = table[idx].word.data();
                for (size_t i = 0; i < len; ++i) {
                    if (to_lower_char(p[i]) != tw[i]) {
                        eq = false;
                        break;
                    }
                }
                if (eq) return table[idx].id;
            }
            idx = (idx + 1) & MASK;
        }
        return -1;
    }
};

bool match_phrase_fast(const char* text, size_t text_len, const std::vector<std::string>& terms) {
    if (terms.empty()) return false;
    size_t L = text_len;
    const std::string& first = terms[0];
    size_t len_first = first.size();
    if (len_first > L) return false;

    char c0_low = first[0];
    char c0_up = (c0_low >= 'a' && c0_low <= 'z') ? (char)(c0_low - ('a' - 'A')) : c0_low;

    size_t pos = 0;
    while (pos + len_first <= L) {
        size_t idx = pos;
        while (idx + len_first <= L && text[idx] != c0_low && text[idx] != c0_up) {
            idx++;
        }
        if (idx + len_first > L) return false;

        bool eq = true;
        for (size_t k = 1; k < len_first; ++k) {
            if (to_lower_char(text[idx + k]) != first[k]) {
                eq = false;
                break;
            }
        }
        if (!eq) {
            pos = idx + 1;
            continue;
        }

        if (idx > 0 && IS_ALNUM[(uint8_t)to_lower_char(text[idx - 1])]) {
            pos = idx + 1;
            continue;
        }

        size_t end_idx = idx + len_first;
        if (end_idx < L && IS_ALNUM[(uint8_t)to_lower_char(text[end_idx])]) {
            pos = idx + 1;
            continue;
        }

        size_t cur_pos = end_idx;
        bool matched_all = true;

        for (size_t t = 1; t < terms.size(); ++t) {
            const std::string& term = terms[t];
            size_t len_t = term.size();

            while (cur_pos < L && !IS_ALNUM[(uint8_t)to_lower_char(text[cur_pos])]) {
                cur_pos++;
            }
            if (cur_pos + len_t > L) {
                matched_all = false;
                break;
            }
            bool eq_t = true;
            for (size_t k = 0; k < len_t; ++k) {
                if (to_lower_char(text[cur_pos + k]) != term[k]) {
                    eq_t = false;
                    break;
                }
            }
            if (!eq_t) {
                matched_all = false;
                break;
            }
            size_t after_t = cur_pos + len_t;
            if (after_t < L && IS_ALNUM[(uint8_t)to_lower_char(text[after_t])]) {
                matched_all = false;
                break;
            }
            cur_pos = after_t;
        }

        if (matched_all) return true;
        pos = idx + 1;
    }
    return false;
}

enum QueryType { QUERY_BOOL, QUERY_PHRASE };
enum BoolOp { OP_AND, OP_OR, OP_NOT };

struct Query {
    QueryType type;
    std::vector<int> bool_terms;
    std::vector<BoolOp> bool_ops;
    std::vector<int> phrase_term_ids;
    std::vector<std::string> phrase_terms;
};



int main(int argc, char** argv) {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);
    init_tables();

    std::string data_dir = (argc > 1) ? argv[1] : "/data";
    std::string out_dir = (argc > 2) ? argv[2] : "/out";

    std::string queries_path = data_dir + "/queries.txt";
    MappedFile qmf;
    if (!qmf.open(queries_path.c_str())) return 1;

    FastTermTable term_table;

    std::vector<Query> queries;
    queries.reserve(65536);

    const char* qp = qmf.data;
    const char* q_limit = qmf.data + qmf.size;

    while (qp < q_limit) {
        while (qp < q_limit && (*qp == '\r' || *qp == '\n')) qp++;
        if (qp >= q_limit) break;

        const char* q_line_start = qp;
        while (qp < q_limit && *qp != '\r' && *qp != '\n') qp++;
        const char* q_line_end = qp;

        size_t line_len = q_line_end - q_line_start;
        if (line_len == 0) continue;

        Query q;
        if (line_len >= 5 && std::memcmp(q_line_start, "BOOL ", 5) == 0) {
            q.type = QUERY_BOOL;
            const char* p = q_line_start + 5;
            std::string cur;
            bool is_op = false;
            while (p < q_line_end) {
                while (p < q_line_end && *p == ' ') p++;
                if (p >= q_line_end) break;
                cur.clear();
                while (p < q_line_end && *p != ' ') {
                    cur.push_back(*p);
                    p++;
                }
                if (!is_op) {
                    for (char& c : cur) c = to_lower_char(c);
                    q.bool_terms.push_back(term_table.insert(cur));
                    is_op = true;
                } else {
                    if (cur == "AND") q.bool_ops.push_back(OP_AND);
                    else if (cur == "OR") q.bool_ops.push_back(OP_OR);
                    else if (cur == "NOT") q.bool_ops.push_back(OP_NOT);
                    is_op = false;
                }
            }
        } else if (line_len >= 7 && std::memcmp(q_line_start, "PHRASE ", 7) == 0) {
            q.type = QUERY_PHRASE;
            const char* p = q_line_start + 7;
            std::string cur;
            while (p < q_line_end) {
                while (p < q_line_end && !IS_ALNUM[(uint8_t)to_lower_char(*p)]) p++;
                if (p >= q_line_end) break;
                cur.clear();
                while (p < q_line_end && IS_ALNUM[(uint8_t)to_lower_char(*p)]) {
                    cur.push_back(to_lower_char(*p));
                    p++;
                }
                q.phrase_terms.push_back(cur);
                q.phrase_term_ids.push_back(term_table.insert(cur));
            }
        }
        queries.push_back(std::move(q));
    }
    qmf.close(); // Unmap queries.txt immediately

    size_t num_terms = term_table.count;

    std::string docs_path = data_dir + "/docs.txt";
    MappedFile mf;
    if (!mf.open(docs_path.c_str())) return 1;

    const size_t num_threads = 12;
    std::vector<size_t> split_points;
    split_points.push_back(0);

    for (size_t i = 1; i < num_threads; ++i) {
        size_t target = i * (mf.size / num_threads);
        const char* nl = (const char*)memchr(mf.data + target, '\n', mf.size - target);
        if (nl) split_points.push_back((nl - mf.data) + 1);
        else split_points.push_back(mf.size);
    }
    split_points.push_back(mf.size);

    struct DocInfo {
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

    // Pass 1: Count term frequencies and collect doc info without storing postings
    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            const char* p = mf.data + split_points[t];
            const char* limit = mf.data + split_points[t + 1];
            auto& t_idx = thread_indices[t];
            t_idx.term_counts.assign(num_terms, 0);

            std::vector<uint32_t> term_seen(num_terms, 0);
            uint32_t cur_epoch = 1;

            while (p < limit) {
                const char* nl = (const char*)memchr(p, '\n', limit - p);
                const char* line_end = nl ? nl : limit;
                if (line_end > p && *(line_end - 1) == '\r') line_end--;

                const char* tab = (const char*)memchr(p, '\t', line_end - p);
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
                        while (tp < line_end && !IS_ALNUM[(uint8_t)to_lower_char(*tp)]) tp++;
                        if (tp >= line_end) break;
                        const char* w_start = tp;
                        uint64_t h = 0xcbf29ce484222325ULL;
                        while (tp < line_end && IS_ALNUM[(uint8_t)to_lower_char(*tp)]) {
                            h = (h ^ (unsigned char)to_lower_char(*tp)) * 0x100000001b3ULL;
                            tp++;
                        }
                        size_t w_len = tp - w_start;
                        int term_id = term_table.find(w_start, w_len, h);
                        if (term_id != -1) {
                            if (term_seen[term_id] != cur_epoch) {
                                term_seen[term_id] = cur_epoch;
                                t_idx.term_counts[term_id]++;
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

    // Compute global term offsets and thread partition offsets
    std::vector<uint32_t> term_offsets(num_terms + 1, 0);
    std::vector<std::vector<uint32_t>> thread_start(num_threads, std::vector<uint32_t>(num_terms));

    for (size_t term_id = 0; term_id < num_terms; ++term_id) {
        uint32_t cur = term_offsets[term_id];
        for (size_t t = 0; t < num_threads; ++t) {
            thread_start[t][term_id] = cur;
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

    // Pass 2: Directly scatter into postings_data from memory-mapped file
    threads.clear();
    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            const char* p = mf.data + split_points[t];
            const char* limit = mf.data + split_points[t + 1];
            auto& starts = thread_start[t];

            std::vector<uint32_t> term_seen(num_terms, 0);
            uint32_t cur_epoch = 1;

            while (p < limit) {
                const char* nl = (const char*)memchr(p, '\n', limit - p);
                const char* line_end = nl ? nl : limit;
                if (line_end > p && *(line_end - 1) == '\r') line_end--;

                const char* tab = (const char*)memchr(p, '\t', line_end - p);
                if (tab) {
                    uint32_t doc_id = 0;
                    const char* cur = p;
                    while (cur < tab && *cur >= '0' && *cur <= '9') {
                        doc_id = doc_id * 10 + (*cur - '0');
                        cur++;
                    }

                    const char* text_start = tab + 1;
                    if (cur_epoch == 0xFFFFFFF0) {
                        std::fill(term_seen.begin(), term_seen.end(), 0);
                        cur_epoch = 1;
                    }
                    const char* tp = text_start;
                    while (tp < line_end) {
                        while (tp < line_end && !IS_ALNUM[(uint8_t)to_lower_char(*tp)]) tp++;
                        if (tp >= line_end) break;
                        const char* w_start = tp;
                        uint64_t h = 0xcbf29ce484222325ULL;
                        while (tp < line_end && IS_ALNUM[(uint8_t)to_lower_char(*tp)]) {
                            h = (h ^ (unsigned char)to_lower_char(*tp)) * 0x100000001b3ULL;
                            tp++;
                        }
                        size_t w_len = tp - w_start;
                        int term_id = term_table.find(w_start, w_len, h);
                        if (term_id != -1) {
                            if (term_seen[term_id] != cur_epoch) {
                                term_seen[term_id] = cur_epoch;
                                uint32_t slot = starts[term_id]++;
                                postings_data[slot] = doc_id;
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

    uint32_t max_doc_id = 0;
    for (size_t t = 0; t < num_threads; ++t) {
        for (const auto& d : thread_indices[t].docs) {
            if (d.doc_id > max_doc_id) max_doc_id = d.doc_id;
        }
    }

    std::vector<uint32_t> doc_offsets(max_doc_id + 1, 0);
    std::vector<uint32_t> doc_lens(max_doc_id + 1, 0);
    for (size_t t = 0; t < num_threads; ++t) {
        for (const auto& d : thread_indices[t].docs) {
            doc_offsets[d.doc_id] = d.offset;
            doc_lens[d.doc_id] = d.len;
        }
    }

    // Free thread indices immediately to minimize peak memory
    thread_indices.clear();
    thread_indices.shrink_to_fit();
    thread_start.clear();
    thread_start.shrink_to_fit();

    // Query helper: get postings slice for a term
    auto get_postings = [&](int term_id) -> std::pair<const uint32_t*, size_t> {
        if (term_id < 0 || (size_t)term_id >= num_terms) return {nullptr, 0};
        uint32_t st = term_offsets[term_id];
        uint32_t en = term_offsets[term_id + 1];
        if (st >= en) return {nullptr, 0};
        return {&postings_data[st], en - st};
    };

    // Evaluate queries in parallel directly to thread buffers
    size_t total_queries = queries.size();
    std::vector<std::string> thread_outputs(num_threads);

    size_t chunk_size = (total_queries + num_threads - 1) / num_threads;
    threads.clear();

    for (size_t t = 0; t < num_threads; ++t) {
        size_t start = t * chunk_size;
        size_t end = std::min(total_queries, start + chunk_size);
        threads.emplace_back([&, t, start, end]() {
            std::vector<uint32_t> cur_res;
            std::vector<uint32_t> temp;
            std::string& out_str = thread_outputs[t];
            out_str.reserve((end - start) * 64);
            char line_buf[512];

            for (size_t q_idx = start; q_idx < end; ++q_idx) {
                const auto& q = queries[q_idx];
                cur_res.clear();

                if (q.type == QUERY_BOOL) {
                    if (!q.bool_terms.empty()) {
                        bool all_and = true;
                        for (BoolOp op : q.bool_ops) {
                            if (op != OP_AND) { all_and = false; break; }
                        }

                        if (all_and && q.bool_terms.size() > 1) {
                            // Sort terms by postings length ascending
                            std::vector<int> sorted_terms = q.bool_terms;
                            std::sort(sorted_terms.begin(), sorted_terms.end(), [&](int a, int b) {
                                auto [pa, la] = get_postings(a);
                                auto [pb, lb] = get_postings(b);
                                return la < lb;
                            });

                            auto [ptr0, len0] = get_postings(sorted_terms[0]);
                            if (len0 > 0) {
                                cur_res.assign(ptr0, ptr0 + len0);
                                for (size_t k = 1; k < sorted_terms.size(); ++k) {
                                    if (cur_res.empty()) break;
                                    auto [ptr_k, len_k] = get_postings(sorted_terms[k]);
                                    if (len_k == 0) { cur_res.clear(); break; }
                                    temp.clear();
                                    std::set_intersection(cur_res.begin(), cur_res.end(),
                                                          ptr_k, ptr_k + len_k,
                                                          std::back_inserter(temp));
                                    cur_res = std::move(temp);
                                }
                            }
                        } else {
                            auto [ptr0, len0] = get_postings(q.bool_terms[0]);
                            if (len0 > 0) {
                                cur_res.assign(ptr0, ptr0 + len0);
                            }

                            for (size_t op_i = 0; op_i < q.bool_ops.size(); ++op_i) {
                                BoolOp op = q.bool_ops[op_i];
                                auto [ptr_rhs, len_rhs] = get_postings(q.bool_terms[op_i + 1]);

                                if (op == OP_AND) {
                                    if (len_rhs == 0 || cur_res.empty()) {
                                        cur_res.clear();
                                    } else {
                                        temp.clear();
                                        std::set_intersection(cur_res.begin(), cur_res.end(),
                                                              ptr_rhs, ptr_rhs + len_rhs,
                                                              std::back_inserter(temp));
                                        cur_res = std::move(temp);
                                    }
                                } else if (op == OP_OR) {
                                    if (len_rhs > 0) {
                                        if (cur_res.empty()) {
                                            cur_res.assign(ptr_rhs, ptr_rhs + len_rhs);
                                        } else {
                                            temp.clear();
                                            std::set_union(cur_res.begin(), cur_res.end(),
                                                           ptr_rhs, ptr_rhs + len_rhs,
                                                           std::back_inserter(temp));
                                            cur_res = std::move(temp);
                                        }
                                    }
                                } else if (op == OP_NOT) {
                                    if (len_rhs > 0 && !cur_res.empty()) {
                                        temp.clear();
                                        std::set_difference(cur_res.begin(), cur_res.end(),
                                                            ptr_rhs, ptr_rhs + len_rhs,
                                                            std::back_inserter(temp));
                                        cur_res = std::move(temp);
                                    }
                                }
                            }
                        }
                    }
                } else if (q.type == QUERY_PHRASE) {
                    if (!q.phrase_term_ids.empty()) {
                        // Find term with smallest postings list first
                        size_t min_idx = 0;
                        size_t min_len = (size_t)-1;
                        for (size_t k = 0; k < q.phrase_term_ids.size(); ++k) {
                            auto [p, len] = get_postings(q.phrase_term_ids[k]);
                            if (len < min_len) {
                                min_len = len;
                                min_idx = k;
                            }
                        }

                        if (min_len == 0) {
                            cur_res.clear();
                        } else {
                            auto [ptr_min, len_min] = get_postings(q.phrase_term_ids[min_idx]);
                            cur_res.assign(ptr_min, ptr_min + len_min);

                            for (size_t k = 0; k < q.phrase_term_ids.size(); ++k) {
                                if (k == min_idx) continue;
                                if (cur_res.empty()) break;
                                auto [ptr_k, len_k] = get_postings(q.phrase_term_ids[k]);
                                if (len_k == 0) {
                                    cur_res.clear();
                                    break;
                                }
                                temp.clear();
                                std::set_intersection(cur_res.begin(), cur_res.end(),
                                                      ptr_k, ptr_k + len_k,
                                                      std::back_inserter(temp));
                                cur_res = std::move(temp);
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
                                cur_res = std::move(temp);
                            }
                        }
                    }
                }

                if (cur_res.empty()) {
                    out_str.append("0 0\n", 4);
                } else {
                    size_t cnt = cur_res.size();
                    uint64_t sum = 0;
                    for (uint32_t d : cur_res) sum += d;
                    size_t m = std::min((size_t)10, cnt);

                    char* p = line_buf;
                    p = fast_u64(p, cnt);
                    *p++ = ' ';
                    p = fast_u64(p, sum);
                    for (size_t i = 0; i < m; ++i) {
                        *p++ = ' ';
                        p = fast_u32(p, cur_res[i]);
                    }
                    *p++ = '\n';
                    out_str.append(line_buf, p - line_buf);
                }
            }
        });
    }

    for (auto& th : threads) th.join();

    std::string out_path = out_dir + "/results.txt";
    FILE* fp = fopen(out_path.c_str(), "wb");
    if (!fp) return 1;

    char out_buf[1048576];
    setvbuf(fp, out_buf, _IOFBF, sizeof(out_buf));

    for (size_t t = 0; t < num_threads; ++t) {
        if (!thread_outputs[t].empty()) {
            fwrite(thread_outputs[t].data(), 1, thread_outputs[t].size(), fp);
        }
    }
    fclose(fp);

    return 0;
}
