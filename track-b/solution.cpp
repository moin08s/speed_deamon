#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>
#include <thread>
#include <algorithm>
#include <cstdint>
#include <cstring>

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

    ~MappedFile() {
#ifdef _WIN32
        if (data) UnmapViewOfFile(data);
        if (hMap) CloseHandle(hMap);
        if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
#else
        if (data && data != MAP_FAILED) munmap((void*)data, size);
        if (fd >= 0) ::close(fd);
#endif
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

struct WordRef {
    int id = -1;
};

bool match_phrase_fast(const char* text, size_t text_len, const std::vector<std::string>& terms) {
    if (terms.empty()) return false;
    size_t L = text_len;
    const std::string& first = terms[0];
    size_t len_first = first.size();
    if (len_first > L) return false;

    size_t pos = 0;
    while (pos + len_first <= L) {
        // Find first term
        bool found = false;
        size_t idx = pos;
        while (idx + len_first <= L) {
            bool eq = true;
            for (size_t k = 0; k < len_first; ++k) {
                if (to_lower_char(text[idx + k]) != first[k]) {
                    eq = false;
                    break;
                }
            }
            if (eq) {
                found = true;
                break;
            }
            idx++;
        }

        if (!found) return false;

        // Word boundary before
        if (idx > 0 && IS_ALNUM[(uint8_t)to_lower_char(text[idx - 1])]) {
            pos = idx + 1;
            continue;
        }

        // Word boundary after
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
            bool eq = true;
            for (size_t k = 0; k < len_t; ++k) {
                if (to_lower_char(text[cur_pos + k]) != term[k]) {
                    eq = false;
                    break;
                }
            }
            if (!eq) {
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
    std::ifstream qf(queries_path);
    if (!qf) return 1;

    std::unordered_map<std::string, int> term_to_id;
    auto get_term_id = [&](const std::string& s) -> int {
        auto it = term_to_id.find(s);
        if (it != term_to_id.end()) return it->second;
        int id = (int)term_to_id.size();
        term_to_id[s] = id;
        return id;
    };

    std::vector<Query> queries;
    std::string line;
    while (std::getline(qf, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;

        Query q;
        if (line.rfind("BOOL ", 0) == 0) {
            q.type = QUERY_BOOL;
            const char* p = line.c_str() + 5;
            // Tokenize BOOL query: term0 [OP1 term1 OP2 term2 ...]
            std::string cur;
            bool is_op = false;
            while (*p) {
                while (*p == ' ') p++;
                if (!*p) break;
                cur.clear();
                while (*p && *p != ' ') {
                    cur.push_back(*p);
                    p++;
                }
                if (!is_op) {
                    // lower term
                    for (char& c : cur) c = to_lower_char(c);
                    q.bool_terms.push_back(get_term_id(cur));
                    is_op = true;
                } else {
                    if (cur == "AND") q.bool_ops.push_back(OP_AND);
                    else if (cur == "OR") q.bool_ops.push_back(OP_OR);
                    else if (cur == "NOT") q.bool_ops.push_back(OP_NOT);
                    is_op = false;
                }
            }
        } else if (line.rfind("PHRASE ", 0) == 0) {
            q.type = QUERY_PHRASE;
            const char* p = line.c_str() + 7;
            std::string cur;
            while (*p) {
                while (*p && !IS_ALNUM[(uint8_t)to_lower_char(*p)]) p++;
                if (!*p) break;
                cur.clear();
                while (*p && IS_ALNUM[(uint8_t)to_lower_char(*p)]) {
                    cur.push_back(to_lower_char(*p));
                    p++;
                }
                q.phrase_terms.push_back(cur);
                q.phrase_term_ids.push_back(get_term_id(cur));
            }
        }
        queries.push_back(std::move(q));
    }
    qf.close();

    size_t num_terms = term_to_id.size();

    std::string docs_path = data_dir + "/docs.txt";
    MappedFile mf;
    if (!mf.open(docs_path.c_str())) return 1;

    const size_t num_threads = 6;
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
        std::vector<std::vector<uint32_t>> postings;
        std::vector<DocInfo> docs;
    };

    std::vector<ThreadIndex> thread_indices(num_threads);
    for (size_t t = 0; t < num_threads; ++t) {
        thread_indices[t].postings.resize(num_terms);
    }

    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            const char* p = mf.data + split_points[t];
            const char* limit = mf.data + split_points[t + 1];
            auto& t_idx = thread_indices[t];

            std::string tok;
            tok.reserve(64);

            std::vector<int> doc_terms;
            doc_terms.reserve(256);

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

                    doc_terms.clear();
                    const char* tp = text_start;
                    while (tp < line_end) {
                        while (tp < line_end && !IS_ALNUM[(uint8_t)to_lower_char(*tp)]) tp++;
                        if (tp >= line_end) break;
                        tok.clear();
                        while (tp < line_end && IS_ALNUM[(uint8_t)to_lower_char(*tp)]) {
                            tok.push_back(to_lower_char(*tp));
                            tp++;
                        }
                        auto it = term_to_id.find(tok);
                        if (it != term_to_id.end()) {
                            doc_terms.push_back(it->second);
                        }
                    }

                    std::sort(doc_terms.begin(), doc_terms.end());
                    doc_terms.erase(std::unique(doc_terms.begin(), doc_terms.end()), doc_terms.end());

                    for (int term_id : doc_terms) {
                        t_idx.postings[term_id].push_back(doc_id);
                    }
                }
                p = nl ? nl + 1 : limit;
            }
        });
    }

    for (auto& th : threads) th.join();

    // Merge postings and doc table
    std::vector<std::vector<uint32_t>> final_postings(num_terms);
    for (size_t term_id = 0; term_id < num_terms; ++term_id) {
        size_t total_size = 0;
        for (size_t t = 0; t < num_threads; ++t) {
            total_size += thread_indices[t].postings[term_id].size();
        }
        final_postings[term_id].reserve(total_size);
        for (size_t t = 0; t < num_threads; ++t) {
            const auto& src = thread_indices[t].postings[term_id];
            final_postings[term_id].insert(final_postings[term_id].end(), src.begin(), src.end());
        }
    }

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

    // Free thread indices
    thread_indices.clear();

    // Evaluate queries in parallel
    size_t total_queries = queries.size();
    std::vector<std::string> query_results(total_queries);

    size_t chunk_size = (total_queries + num_threads - 1) / num_threads;
    threads.clear();

    for (size_t t = 0; t < num_threads; ++t) {
        size_t start = t * chunk_size;
        size_t end = std::min(total_queries, start + chunk_size);
        threads.emplace_back([&, start, end]() {
            std::vector<uint32_t> cur_res;
            std::vector<uint32_t> temp;

            for (size_t q_idx = start; q_idx < end; ++q_idx) {
                const auto& q = queries[q_idx];
                cur_res.clear();

                if (q.type == QUERY_BOOL) {
                    if (!q.bool_terms.empty()) {
                        cur_res = final_postings[q.bool_terms[0]];
                        for (size_t op_i = 0; op_i < q.bool_ops.size(); ++op_i) {
                            BoolOp op = q.bool_ops[op_i];
                            const auto& rhs = final_postings[q.bool_terms[op_i + 1]];
                            temp.clear();

                            if (op == OP_AND) {
                                std::set_intersection(cur_res.begin(), cur_res.end(),
                                                      rhs.begin(), rhs.end(),
                                                      std::back_inserter(temp));
                            } else if (op == OP_OR) {
                                std::set_union(cur_res.begin(), cur_res.end(),
                                               rhs.begin(), rhs.end(),
                                               std::back_inserter(temp));
                            } else if (op == OP_NOT) {
                                std::set_difference(cur_res.begin(), cur_res.end(),
                                                    rhs.begin(), rhs.end(),
                                                    std::back_inserter(temp));
                            }
                            cur_res = std::move(temp);
                        }
                    }
                } else if (q.type == QUERY_PHRASE) {
                    if (!q.phrase_term_ids.empty()) {
                        cur_res = final_postings[q.phrase_term_ids[0]];
                        for (size_t k = 1; k < q.phrase_term_ids.size(); ++k) {
                            if (cur_res.empty()) break;
                            temp.clear();
                            const auto& rhs = final_postings[q.phrase_term_ids[k]];
                            std::set_intersection(cur_res.begin(), cur_res.end(),
                                                  rhs.begin(), rhs.end(),
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

                if (cur_res.empty()) {
                    query_results[q_idx] = "0 0\n";
                } else {
                    size_t cnt = cur_res.size();
                    uint64_t sum = 0;
                    for (uint32_t d : cur_res) sum += d;
                    size_t m = std::min((size_t)10, cnt);
                    std::string out = std::to_string(cnt) + " " + std::to_string(sum);
                    for (size_t i = 0; i < m; ++i) {
                        out += " " + std::to_string(cur_res[i]);
                    }
                    out += "\n";
                    query_results[q_idx] = std::move(out);
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

    for (const auto& res_line : query_results) {
        fwrite(res_line.data(), 1, res_line.size(), fp);
    }
    fclose(fp);

    return 0;
}
