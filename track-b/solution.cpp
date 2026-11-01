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
#include <malloc.h>
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
        data = (const char*)mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
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
    ~MappedFile() { close(); }
};
static uint8_t CHAR_MAP[256];
static uint8_t LOWER_MAP[256];
static void init_tables() {
    static bool inited = false;
    if (inited) return;
    std::memset(CHAR_MAP, 0, 256);
    for (char c = '0'; c <= '9'; ++c) CHAR_MAP[(uint8_t)c] = (uint8_t)c;
    for (char c = 'a'; c <= 'z'; ++c) CHAR_MAP[(uint8_t)c] = (uint8_t)c;
    for (char c = 'A'; c <= 'Z'; ++c) CHAR_MAP[(uint8_t)c] = (uint8_t)(c + 32);
    for (int i = 0; i < 256; ++i) {
        if (i >= 'A' && i <= 'Z') LOWER_MAP[i] = (uint8_t)(i + 32);
        else LOWER_MAP[i] = (uint8_t)i;
    }
    inited = true;
}
inline char to_lower_char(char c) {
    return (char)LOWER_MAP[(uint8_t)c];
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
        const char* ptr = nullptr;
        int id = -1;
    };
    std::vector<Entry> table;
    int count = 0;
    FastTermTable() : table(CAPACITY) {}
    static inline uint64_t hash_term(const char* p, size_t len) {
        uint64_t h = 0xcbf29ce484222325ULL;
        for (size_t i = 0; i < len; ++i) {
            h = (h ^ (unsigned char)to_lower_char(p[i])) * 0x100000001b3ULL;
        }
        return h ? h : 1;
    }
    int insert(const char* p, size_t len) {
        uint64_t h = hash_term(p, len);
        uint32_t idx = (uint32_t)h & MASK;
        while (table[idx].hash != 0) {
            if (table[idx].hash == h && table[idx].len == len) {
                bool eq = true;
                for (size_t i = 0; i < len; ++i) {
                    if (to_lower_char(table[idx].ptr[i]) != to_lower_char(p[i])) {
                        eq = false;
                        break;
                    }
                }
                if (eq) return table[idx].id;
            }
            idx = (idx + 1) & MASK;
        }
        table[idx].hash = h;
        table[idx].len = (uint32_t)len;
        table[idx].ptr = p;
        table[idx].id = count++;
        return table[idx].id;
    }
    inline int find(const char* p, size_t len, uint64_t h) const {
        uint32_t idx = (uint32_t)h & MASK;
        while (table[idx].hash != 0) {
            if (table[idx].hash == h && table[idx].len == len) {
                bool eq = true;
                const char* tw = table[idx].ptr;
                for (size_t i = 0; i < len; ++i) {
                    if (to_lower_char(p[i]) != to_lower_char(tw[i])) {
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
#if defined(__AVX2__)
#include <immintrin.h>
inline const char* find_char_pair_avx2(const char* p, size_t len, char c1, char c2) {
    if (len == 0) return nullptr;
    if (c1 == c2) return (const char*)std::memchr(p, c1, len);
    __m256i vc1 = _mm256_set1_epi8(c1);
    __m256i vc2 = _mm256_set1_epi8(c2);
    size_t i = 0;
    while (i + 32 <= len) {
        __m256i chunk = _mm256_loadu_si256((const __m256i*)(p + i));
        __m256i m1 = _mm256_cmpeq_epi8(chunk, vc1);
        __m256i m2 = _mm256_cmpeq_epi8(chunk, vc2);
        uint32_t mask = (uint32_t)_mm256_movemask_epi8(_mm256_or_si256(m1, m2));
        if (mask != 0) {
            uint32_t match_idx = __builtin_ctz(mask);
            return p + i + match_idx;
        }
        i += 32;
    }
    while (i < len) {
        if (p[i] == c1 || p[i] == c2) return p + i;
        i++;
    }
    return nullptr;
}
#else
inline const char* find_char_pair_avx2(const char* p, size_t len, char c1, char c2) {
    if (c1 == c2) return (const char*)std::memchr(p, c1, len);
    const char* p1 = (const char*)std::memchr(p, c1, len);
    const char* p2 = (const char*)std::memchr(p, c2, len);
    if (!p1) return p2;
    if (!p2) return p1;
    return std::min(p1, p2);
}
#endif
struct TermView {
    const char* data;
    uint32_t len;
};
bool match_phrase_fast(const char* text, size_t text_len, const std::vector<TermView>& terms) {
    if (terms.empty()) return false;
    size_t L = text_len;
    const TermView& first = terms[0];
    size_t len_first = first.len;
    if (len_first > L) return false;
    char c0_low = first.data[0];
    char c0_up = (c0_low >= 'a' && c0_low <= 'z') ? (char)(c0_low - ('a' - 'A')) : c0_low;
    size_t pos = 0;
    while (pos + len_first <= L) {
        size_t rem = L - pos;
        if (rem < len_first) return false;
        size_t scan_len = rem - len_first + 1;
        const char* p_match = find_char_pair_avx2(text + pos, scan_len, c0_low, c0_up);
        if (!p_match) return false;
        size_t idx = p_match - text;
        if (idx > 0 && CHAR_MAP[(uint8_t)text[idx - 1]]) {
            pos = idx + 1;
            continue;
        }
        size_t end_idx = idx + len_first;
        if (end_idx < L && CHAR_MAP[(uint8_t)text[end_idx]]) {
            pos = idx + 1;
            continue;
        }
        bool eq = true;
        for (size_t k = 1; k < len_first; ++k) {
            if (to_lower_char(text[idx + k]) != first.data[k]) {
                eq = false;
                break;
            }
        }
        if (!eq) {
            pos = idx + 1;
            continue;
        }
        size_t cur_pos = end_idx;
        bool matched_all = true;
        for (size_t t = 1; t < terms.size(); ++t) {
            const TermView& term = terms[t];
            size_t len_t = term.len;
            while (cur_pos < L && CHAR_MAP[(uint8_t)text[cur_pos]] == 0) {
                cur_pos++;
            }
            if (cur_pos + len_t > L) {
                matched_all = false;
                break;
            }
            size_t after_t = cur_pos + len_t;
            if (after_t < L && CHAR_MAP[(uint8_t)text[after_t]]) {
                matched_all = false;
                break;
            }
            bool eq_t = true;
            for (size_t k = 0; k < len_t; ++k) {
                if (to_lower_char(text[cur_pos + k]) != term.data[k]) {
                    eq_t = false;
                    break;
                }
            }
            if (!eq_t) {
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
template <typename T>
inline void intersect_slices(const T* a, size_t a_len, const T* b, size_t b_len, std::vector<T>& out) {
    out.clear();
    if (a_len == 0 || b_len == 0) return;
    if (a_len > b_len) {
        std::swap(a, b);
        std::swap(a_len, b_len);
    }
    out.reserve(a_len);
    if (b_len > (a_len << 3)) {
        const T* b_end = b + b_len;
        const T* cur = b;
        for (size_t i = 0; i < a_len; ++i) {
            T x = a[i];
            size_t step = 1;
            while (cur + step < b_end && *(cur + step) < x) {
                cur += step;
                step <<= 1;
            }
            const T* high = (cur + step + 1 < b_end) ? (cur + step + 1) : b_end;
            cur = std::lower_bound(cur, high, x);
            if (cur == b_end) break;
            if (*cur == x) {
                out.push_back(x);
                cur++;
            }
        }
    } else {
        std::set_intersection(a, a + a_len, b, b + b_len, std::back_inserter(out));
    }
}
template <typename T>
inline void intersect_adaptive(const std::vector<T>& small, const T* big, size_t big_len, std::vector<T>& out) {
    intersect_slices(small.data(), small.size(), big, big_len, out);
}
template <typename T>
inline void difference_adaptive(const std::vector<T>& left, const T* right, size_t right_len, std::vector<T>& out) {
    out.clear();
    if (left.empty()) return;
    if (right_len == 0) {
        out = left;
        return;
    }
    if (right_len > (left.size() << 3)) {
        out.reserve(left.size());
        const T* right_end = right + right_len;
        const T* cur = right;
        for (T x : left) {
            if (cur < right_end && *cur < x) {
                size_t step = 1;
                while (cur + step < right_end && *(cur + step) < x) {
                    cur += step;
                    step <<= 1;
                }
                const T* high = (cur + step + 1 < right_end) ? (cur + step + 1) : right_end;
                cur = std::lower_bound(cur, high, x);
            }
            if (cur == right_end || *cur != x) {
                out.push_back(x);
            }
        }
    } else {
        out.reserve(left.size());
        std::set_difference(left.begin(), left.end(),
                            right, right + right_len,
                            std::back_inserter(out));
    }
}
struct Query {
    QueryType type;
    std::vector<int> bool_terms;
    std::vector<BoolOp> bool_ops;
    std::vector<int> phrase_term_ids;
    std::vector<TermView> phrase_terms;
};
int main(int argc, char** argv) {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);
    init_tables();
#ifndef _WIN32
    mallopt(M_ARENA_MAX, 2);
#endif
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
            bool is_op = false;
            while (p < q_line_end) {
                while (p < q_line_end && *p == ' ') p++;
                if (p >= q_line_end) break;
                const char* w_start = p;
                while (p < q_line_end && *p != ' ') p++;
                size_t w_len = p - w_start;
                if (!is_op) {
                    q.bool_terms.push_back(term_table.insert(w_start, w_len));
                    is_op = true;
                } else {
                    if (w_len == 3 && std::memcmp(w_start, "AND", 3) == 0) q.bool_ops.push_back(OP_AND);
                    else if (w_len == 2 && std::memcmp(w_start, "OR", 2) == 0) q.bool_ops.push_back(OP_OR);
                    else if (w_len == 3 && std::memcmp(w_start, "NOT", 3) == 0) q.bool_ops.push_back(OP_NOT);
                    is_op = false;
                }
            }
        } else if (line_len >= 7 && std::memcmp(q_line_start, "PHRASE ", 7) == 0) {
            q.type = QUERY_PHRASE;
            const char* p = q_line_start + 7;
            while (p < q_line_end) {
                while (p < q_line_end && CHAR_MAP[(uint8_t)*p] == 0) p++;
                if (p >= q_line_end) break;
                const char* w_start = p;
                while (p < q_line_end && CHAR_MAP[(uint8_t)*p]) p++;
                size_t w_len = p - w_start;
                q.phrase_term_ids.push_back(term_table.insert(w_start, w_len));
                q.phrase_terms.push_back({w_start, (uint32_t)w_len});
            }
        }
        queries.push_back(std::move(q));
    }
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
        std::vector<uint32_t> term_ids;
        std::vector<uint32_t> doc_ends;
    };
    std::vector<ThreadIndex> thread_indices(num_threads);
    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            const char* p = mf.data + split_points[t];
            const char* limit = mf.data + split_points[t + 1];
            auto& t_idx = thread_indices[t];
            t_idx.term_counts.assign(num_terms, 0);
            size_t chunk_len = split_points[t + 1] - split_points[t];
            t_idx.docs.reserve(chunk_len / 500 + 1024);
            t_idx.term_ids.reserve(chunk_len / 16 + 4096);
            t_idx.doc_ends.reserve(chunk_len / 500 + 1024);
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
                                t_idx.term_counts[term_id]++;
                                t_idx.term_ids.push_back((uint32_t)term_id);
                            }
                        }
                    }
                    t_idx.doc_ends.push_back((uint32_t)t_idx.term_ids.size());
                    cur_epoch++;
                }
                p = nl ? nl + 1 : limit;
            }
        });
    }
    for (auto& th : threads) th.join();
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
    for (size_t t = 0; t < num_threads; ++t) {
        thread_indices[t].term_counts.clear();
        thread_indices[t].term_counts.shrink_to_fit();
    }
    uint32_t total_postings = term_offsets[num_terms];
    std::vector<uint32_t> postings_data(total_postings);
    threads.clear();
    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            auto& starts = thread_start[t];
            auto& t_idx = thread_indices[t];
            size_t n_docs = t_idx.docs.size();
            uint32_t cur_st = 0;
            for (size_t di = 0; di < n_docs; ++di) {
                uint32_t doc_id = t_idx.docs[di].doc_id;
                uint32_t end_st = t_idx.doc_ends[di];
                for (uint32_t k = cur_st; k < end_st; ++k) {
                    uint32_t tid = t_idx.term_ids[k];
                    postings_data[starts[tid]++] = doc_id;
                }
                cur_st = end_st;
            }
            std::vector<uint32_t>().swap(t_idx.term_ids);
            std::vector<uint32_t>().swap(t_idx.doc_ends);
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
    thread_indices.clear();
    thread_indices.shrink_to_fit();
    thread_start.clear();
    thread_start.shrink_to_fit();
#ifndef _WIN32
    malloc_trim(0);
#endif
    auto get_postings = [&](int term_id) -> std::pair<const uint32_t*, size_t> {
        if (term_id < 0 || (size_t)term_id >= num_terms) return {nullptr, 0};
        uint32_t st = term_offsets[term_id];
        uint32_t en = term_offsets[term_id + 1];
        if (st >= en) return {nullptr, 0};
        return {&postings_data[st], en - st};
    };
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
                    if (q.bool_terms.empty()) {
                        out_str.append("0 0\n", 4);
                        continue;
                    }
                    if (q.bool_terms.size() == 1) {
                        auto [ptr0, len0] = get_postings(q.bool_terms[0]);
                        if (len0 == 0) {
                            out_str.append("0 0\n", 4);
                        } else {
                            uint64_t sum = 0;
                            for (size_t i = 0; i < len0; ++i) sum += ptr0[i];
                            size_t m = std::min((size_t)10, len0);
                            char* p = line_buf;
                            p = fast_u64(p, len0);
                            *p++ = ' ';
                            p = fast_u64(p, sum);
                            for (size_t i = 0; i < m; ++i) {
                                *p++ = ' ';
                                p = fast_u32(p, ptr0[i]);
                            }
                            *p++ = '\n';
                            out_str.append(line_buf, p - line_buf);
                        }
                        continue;
                    }
                    bool all_and = true;
                    for (BoolOp op : q.bool_ops) {
                        if (op != OP_AND) { all_and = false; break; }
                    }
                    if (all_and) {
                        size_t num_bt = q.bool_terms.size();
                        int sorted_terms[16];
                        for (size_t i = 0; i < num_bt; ++i) sorted_terms[i] = q.bool_terms[i];
                        std::sort(sorted_terms, sorted_terms + num_bt, [&](int a, int b) {
                            auto [pa, la] = get_postings(a);
                            auto [pb, lb] = get_postings(b);
                            return la < lb;
                        });
                        auto [ptr0, len0] = get_postings(sorted_terms[0]);
                        auto [ptr1, len1] = get_postings(sorted_terms[1]);
                        if (len0 == 0 || len1 == 0) {
                            out_str.append("0 0\n", 4);
                            continue;
                        }
                        intersect_slices(ptr0, len0, ptr1, len1, cur_res);
                        for (size_t k = 2; k < num_bt; ++k) {
                            if (cur_res.empty()) break;
                            auto [ptr_k, len_k] = get_postings(sorted_terms[k]);
                            if (len_k == 0) { cur_res.clear(); break; }
                            intersect_slices(cur_res.data(), cur_res.size(), ptr_k, len_k, temp);
                            cur_res.swap(temp);
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
                                    intersect_slices(cur_res.data(), cur_res.size(), ptr_rhs, len_rhs, temp);
                                    cur_res.swap(temp);
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
                                        cur_res.swap(temp);
                                    }
                                }
                            } else if (op == OP_NOT) {
                                if (len_rhs > 0 && !cur_res.empty()) {
                                    difference_adaptive(cur_res, ptr_rhs, len_rhs, temp);
                                    cur_res.swap(temp);
                                }
                            }
                        }
                    }
                } else if (q.type == QUERY_PHRASE) {
                    if (q.phrase_term_ids.empty()) {
                        out_str.append("0 0\n", 4);
                        continue;
                    }
                    size_t num_pt = q.phrase_term_ids.size();
                    int sorted_pterms[16];
                    for (size_t i = 0; i < num_pt; ++i) sorted_pterms[i] = q.phrase_term_ids[i];
                    std::sort(sorted_pterms, sorted_pterms + num_pt, [&](int a, int b) {
                        auto [pa, la] = get_postings(a);
                        auto [pb, lb] = get_postings(b);
                        return la < lb;
                    });
                    auto [ptr0, len0] = get_postings(sorted_pterms[0]);
                    if (len0 == 0) {
                        out_str.append("0 0\n", 4);
                        continue;
                    }
                    if (num_pt == 1) {
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
                            out_str.append("0 0\n", 4);
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
