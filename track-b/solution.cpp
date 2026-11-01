#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <string_view>
#include <thread>
#include <barrier>
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
    if (val == 0) { *p++ = '0'; return p; }
    char buf[10]; int len = 0;
    while (val > 0) { buf[len++] = '0' + (val % 10); val /= 10; }
    for (int i = len - 1; i >= 0; --i) *p++ = buf[i];
    return p;
}

inline char* fast_u64(char* p, uint64_t val) {
    if (val == 0) { *p++ = '0'; return p; }
    char buf[20]; int len = 0;
    while (val > 0) { buf[len++] = '0' + (val % 10); val /= 10; }
    for (int i = len - 1; i >= 0; --i) *p++ = buf[i];
    return p;
}

// Compact 64K hash table that fits in CPU cache
struct FastTermTable {
    static constexpr uint32_t CAPACITY = 65536;
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
        if (mask != 0) return p + i + __builtin_ctz(mask);
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
        const char* p_match = find_char_pair_avx2(text + pos, rem - len_first + 1, c0_low, c0_up);
        if (!p_match) return false;

        size_t m_pos = p_match - text;
        if (m_pos > 0 && CHAR_MAP[(uint8_t)text[m_pos - 1]] != 0) {
            pos = m_pos + 1;
            continue;
        }

        bool match = true;
        for (size_t i = 1; i < len_first; ++i) {
            if (to_lower_char(text[m_pos + i]) != first.data[i]) {
                match = false;
                break;
            }
        }
        if (!match) {
            pos = m_pos + 1;
            continue;
        }

        size_t after_first = m_pos + len_first;
        if (terms.size() == 1) {
            if (after_first == L || CHAR_MAP[(uint8_t)text[after_first]] == 0) return true;
            pos = m_pos + 1;
            continue;
        }

        if (after_first == L || CHAR_MAP[(uint8_t)text[after_first]] != 0) {
            pos = m_pos + 1;
            continue;
        }

        size_t cur = after_first;
        bool all_matched = true;
        for (size_t t = 1; t < terms.size(); ++t) {
            while (cur < L && CHAR_MAP[(uint8_t)text[cur]] == 0) cur++;
            if (cur >= L) { all_matched = false; break; }

            const TermView& tv = terms[t];
            size_t tlen = tv.len;
            if (cur + tlen > L) { all_matched = false; break; }

            bool t_match = true;
            for (size_t i = 0; i < tlen; ++i) {
                if (to_lower_char(text[cur + i]) != tv.data[i]) {
                    t_match = false;
                    break;
                }
            }
            if (!t_match) { all_matched = false; break; }

            cur += tlen;
            if (cur < L && CHAR_MAP[(uint8_t)text[cur]] != 0) {
                all_matched = false;
                break;
            }
        }

        if (all_matched) return true;
        pos = m_pos + 1;
    }
    return false;
}

#if defined(__AVX2__)
inline void intersect_slices_avx2(const uint32_t* __restrict p1, size_t n1,
                                  const uint32_t* __restrict p2, size_t n2,
                                  std::vector<uint32_t>& out) {
    out.clear();
    out.reserve(std::min(n1, n2));
    size_t i = 0, j = 0;
    while (i < n1 && j < n2) {
        if (i + 8 <= n1 && j + 8 <= n2) {
            uint32_t v1_last = p1[i + 7];
            uint32_t v2_first = p2[j];
            if (v1_last < v2_first) { i += 8; continue; }
            uint32_t v2_last = p2[j + 7];
            uint32_t v1_first = p1[i];
            if (v2_last < v1_first) { j += 8; continue; }
        }
        uint32_t a = p1[i], b = p2[j];
        if (a == b) { out.push_back(a); i++; j++; }
        else if (a < b) i++;
        else j++;
    }
}
#define intersect_slices intersect_slices_avx2
#else
inline void intersect_slices(const uint32_t* __restrict p1, size_t n1,
                             const uint32_t* __restrict p2, size_t n2,
                             std::vector<uint32_t>& out) {
    out.clear();
    out.reserve(std::min(n1, n2));
    size_t i = 0, j = 0;
    while (i < n1 && j < n2) {
        uint32_t a = p1[i], b = p2[j];
        if (a == b) { out.push_back(a); i++; j++; }
        else if (a < b) i++;
        else j++;
    }
}
#endif

inline void difference_adaptive(const std::vector<uint32_t>& cur,
                                const uint32_t* rhs, size_t n_rhs,
                                std::vector<uint32_t>& out) {
    out.clear();
    size_t n_cur = cur.size();
    if (n_cur == 0) return;
    if (n_rhs == 0) { out = cur; return; }
    out.reserve(n_cur);

    if (n_rhs * 8 < n_cur) {
        for (uint32_t doc_id : cur) {
            if (!std::binary_search(rhs, rhs + n_rhs, doc_id)) out.push_back(doc_id);
        }
    } else {
        size_t i = 0, j = 0;
        const uint32_t* p1 = cur.data();
        while (i < n_cur && j < n_rhs) {
            uint32_t a = p1[i], b = rhs[j];
            if (a == b) { i++; j++; }
            else if (a < b) { out.push_back(a); i++; }
            else j++;
        }
        while (i < n_cur) out.push_back(p1[i++]);
    }
}

enum QueryType { QUERY_BOOL, QUERY_PHRASE };
enum BoolOp { OP_AND, OP_OR, OP_NOT };

struct Query {
    QueryType type;
    std::vector<int> bool_terms;
    std::vector<BoolOp> bool_ops;
    std::vector<int> phrase_term_ids;
    std::vector<TermView> phrase_terms;
};

struct PostingItem {
    uint32_t term_id;
    uint32_t doc_id;
    bool operator<(const PostingItem& o) const {
        if (term_id != o.term_id) return term_id < o.term_id;
        return doc_id < o.doc_id;
    }
};

int main(int argc, char** argv) {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);
    init_tables();

#ifndef _WIN32
    mallopt(M_ARENA_MAX, 1);
    mallopt(M_TRIM_THRESHOLD, 64 * 1024);
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
    std::vector<size_t> split_points(num_threads + 1, 0);
    split_points[num_threads] = mf.size;

    for (size_t i = 1; i < num_threads; ++i) {
        size_t target = i * (mf.size / num_threads);
        const char* nl = (const char*)std::memchr(mf.data + target, '\n', mf.size - target);
        split_points[i] = nl ? (nl - mf.data) + 1 : mf.size;
    }

    struct ThreadLocalData {
        std::vector<PostingItem> postings;
        std::vector<uint32_t> term_start_idx;
        std::string output_str;
    };

    std::vector<ThreadLocalData> tdata(num_threads);
    std::barrier sync_point(num_threads);

    static uint32_t doc_offsets[65536];
    static uint32_t doc_lens[65536];
    std::memset(doc_offsets, 0, sizeof(doc_offsets));
    std::memset(doc_lens, 0, sizeof(doc_lens));

    static std::vector<uint32_t> term_offsets;
    static std::vector<uint32_t> postings_data;
    term_offsets.resize(num_terms + 1, 0);

    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            auto& td = tdata[t];
            td.postings.reserve(120000);

            const char* p = mf.data + split_points[t];
            const char* limit = mf.data + split_points[t + 1];

            std::vector<uint32_t> term_seen(num_terms, 0);
            uint32_t cur_epoch = 1;

            // Single pass over docs.txt
            while (p < limit) {
                const char* nl = (const char*)std::memchr(p, '\n', limit - p);
                const char* line_end = nl ? nl : limit;
                if (line_end > p && *(line_end - 1) == '\r') line_end--;

                const char* tab = (const char*)std::memchr(p, '\t', line_end - p);
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

                    if (doc_id < 65536) {
                        doc_offsets[doc_id] = offset;
                        doc_lens[doc_id] = text_len;
                    }

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
                                td.postings.push_back({(uint32_t)term_id, doc_id});
                            }
                        }
                    }
                    cur_epoch++;
                }
                p = nl ? nl + 1 : limit;
            }

            // Sort local postings by (term_id, doc_id)
            std::sort(td.postings.begin(), td.postings.end());

            // Build index of term offsets within thread
            td.term_start_idx.assign(num_terms + 1, 0);
            uint32_t cur_term = 0;
            uint32_t n_p = (uint32_t)td.postings.size();
            for (uint32_t i = 0; i < n_p; ++i) {
                uint32_t tid = td.postings[i].term_id;
                while (cur_term <= tid) td.term_start_idx[cur_term++] = i;
            }
            while (cur_term <= num_terms) td.term_start_idx[cur_term++] = n_p;

            sync_point.arrive_and_wait();

            if (t == 0) {
                // Compute global term counts and offsets
                uint32_t total = 0;
                for (size_t tid = 0; tid < num_terms; ++tid) {
                    term_offsets[tid] = total;
                    for (size_t i = 0; i < num_threads; ++i) {
                        total += (tdata[i].term_start_idx[tid + 1] - tdata[i].term_start_idx[tid]);
                    }
                }
                term_offsets[num_terms] = total;
                postings_data.resize(total);
            }

            sync_point.arrive_and_wait();

            // Parallel scatter into postings_data
            size_t terms_per_thread = (num_terms + num_threads - 1) / num_threads;
            size_t t_start = t * terms_per_thread;
            size_t t_end = std::min(num_terms, t_start + terms_per_thread);

            for (size_t tid = t_start; tid < t_end; ++tid) {
                uint32_t write_pos = term_offsets[tid];
                for (size_t i = 0; i < num_threads; ++i) {
                    uint32_t s_idx = tdata[i].term_start_idx[tid];
                    uint32_t e_idx = tdata[i].term_start_idx[tid + 1];
                    for (uint32_t k = s_idx; k < e_idx; ++k) {
                        postings_data[write_pos++] = tdata[i].postings[k].doc_id;
                    }
                }
            }

            sync_point.arrive_and_wait();

            // Free thread postings memory
            std::vector<PostingItem>().swap(td.postings);
            std::vector<uint32_t>().swap(td.term_start_idx);

            // Parallel query evaluation
            size_t total_queries = queries.size();
            size_t q_chunk = (total_queries + num_threads - 1) / num_threads;
            size_t q_start = t * q_chunk;
            size_t q_end = std::min(total_queries, q_start + q_chunk);

            if (q_start < q_end) {
                std::vector<uint32_t> cur_res;
                std::vector<uint32_t> temp;
                td.output_str.reserve((q_end - q_start) * 64);
                char line_buf[512];

                auto get_postings = [&](int term_id) -> std::pair<const uint32_t*, size_t> {
                    if (term_id < 0 || (size_t)term_id >= num_terms) return {nullptr, 0};
                    uint32_t st = term_offsets[term_id];
                    uint32_t en = term_offsets[term_id + 1];
                    if (st >= en) return {nullptr, 0};
                    return {&postings_data[st], en - st};
                };

                for (size_t q_idx = q_start; q_idx < q_end; ++q_idx) {
                    const auto& q = queries[q_idx];
                    cur_res.clear();

                    if (q.type == QUERY_BOOL) {
                        if (q.bool_terms.empty()) {
                            td.output_str.append("0 0\n", 4);
                            continue;
                        }
                        if (q.bool_terms.size() == 1) {
                            auto [ptr0, len0] = get_postings(q.bool_terms[0]);
                            if (len0 == 0) {
                                td.output_str.append("0 0\n", 4);
                            } else {
                                uint64_t sum = 0;
                                for (size_t i = 0; i < len0; ++i) sum += ptr0[i];
                                size_t m = std::min((size_t)10, len0);
                                char* p = line_buf;
                                p = fast_u64(p, len0); *p++ = ' ';
                                p = fast_u64(p, sum);
                                for (size_t i = 0; i < m; ++i) {
                                    *p++ = ' ';
                                    p = fast_u32(p, ptr0[i]);
                                }
                                *p++ = '\n';
                                td.output_str.append(line_buf, p - line_buf);
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
                                td.output_str.append("0 0\n", 4);
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
                            if (len0 > 0) cur_res.assign(ptr0, ptr0 + len0);

                            for (size_t op_i = 0; op_i < q.bool_ops.size(); ++op_i) {
                                BoolOp op = q.bool_ops[op_i];
                                auto [ptr_rhs, len_rhs] = get_postings(q.bool_terms[op_i + 1]);

                                if (op == OP_AND) {
                                    if (len_rhs == 0 || cur_res.empty()) cur_res.clear();
                                    else {
                                        intersect_slices(cur_res.data(), cur_res.size(), ptr_rhs, len_rhs, temp);
                                        cur_res.swap(temp);
                                    }
                                } else if (op == OP_OR) {
                                    if (len_rhs > 0) {
                                        if (cur_res.empty()) cur_res.assign(ptr_rhs, ptr_rhs + len_rhs);
                                        else {
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
                            td.output_str.append("0 0\n", 4);
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
                            td.output_str.append("0 0\n", 4);
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
                                td.output_str.append("0 0\n", 4);
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
                        td.output_str.append("0 0\n", 4);
                    } else {
                        uint64_t sum = 0;
                        size_t total = cur_res.size();
                        for (uint32_t doc_id : cur_res) sum += doc_id;
                        size_t m = std::min((size_t)10, total);
                        char* p = line_buf;
                        p = fast_u64(p, total); *p++ = ' ';
                        p = fast_u64(p, sum);
                        for (size_t i = 0; i < m; ++i) {
                            *p++ = ' ';
                            p = fast_u32(p, cur_res[i]);
                        }
                        *p++ = '\n';
                        td.output_str.append(line_buf, p - line_buf);
                    }
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
        if (!tdata[t].output_str.empty()) {
            fwrite(tdata[t].output_str.data(), 1, tdata[t].output_str.size(), fp);
        }
    }
    fclose(fp);

    return 0;
}
