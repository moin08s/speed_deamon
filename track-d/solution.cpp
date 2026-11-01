#include <iostream>
#include <fstream>
#include <vector>
#include <string>
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
        if (data && data != MAP_FAILED) {
            munmap((void*)data, size);
            data = nullptr;
        }
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
#endif
    }

    ~MappedFile() { close(); }
};

inline uint32_t parse_uint(const char*& p, const char* limit) {
    while (p < limit && (unsigned char)*p <= ' ') p++;
    uint32_t val = 0;
    while (p < limit && *p >= '0' && *p <= '9') {
        val = val * 10 + (*p++ - '0');
    }
    return val;
}

inline char* fast_i64(char* p, int64_t val) {
    if (val < 0) {
        *p++ = '-';
        *p++ = '1';
        return p;
    }
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

struct Edge {
    uint32_t dst;
    uint32_t w;
};

struct CSRGraph {
    uint32_t n = 0;
    uint32_t m = 0;
    std::vector<uint32_t> fwd_head;
    std::vector<Edge> fwd_edges;
    std::vector<uint32_t> bwd_head;
    std::vector<Edge> bwd_edges;
};

void load_graph(const char* path, CSRGraph& g) {
    MappedFile mf;
    if (!mf.open(path) || mf.size == 0) return;

    const char* p = mf.data;
    const char* limit = mf.data + mf.size;

    g.n = parse_uint(p, limit);
    g.m = parse_uint(p, limit);

    // Advance p to first edge line
    while (p < limit && *p != '\n') p++;
    if (p < limit && *p == '\n') p++;

    const char* edges_start = p;

    // Single pass 1: direct degree counting into head arrays (zero extra memory!)
    g.fwd_head.assign(g.n + 2, 0);
    g.bwd_head.assign(g.n + 2, 0);

    const char* cur = edges_start;
    while (cur < limit) {
        while (cur < limit && (unsigned char)*cur <= ' ') cur++;
        if (cur >= limit) break;
        uint32_t u = parse_uint(cur, limit);
        uint32_t v = parse_uint(cur, limit);
        while (cur < limit && *cur != '\n') cur++;
        if (cur < limit && *cur == '\n') cur++;
        if (u < g.n) g.fwd_head[u + 1]++;
        if (v < g.n) g.bwd_head[v + 1]++;
    }

    for (size_t i = 1; i <= g.n; ++i) {
        g.fwd_head[i] += g.fwd_head[i - 1];
        g.bwd_head[i] += g.bwd_head[i - 1];
    }
    g.fwd_head[g.n + 1] = g.fwd_head[g.n];
    g.bwd_head[g.n + 1] = g.bwd_head[g.n];

    g.fwd_edges.resize(g.fwd_head[g.n]);
    g.bwd_edges.resize(g.bwd_head[g.n]);

    std::vector<uint32_t> fwd_cur(g.fwd_head.begin(), g.fwd_head.begin() + g.n + 1);
    std::vector<uint32_t> bwd_cur(g.bwd_head.begin(), g.bwd_head.begin() + g.n + 1);

    // Pass 2: sequential direct placement of edges
    cur = edges_start;
    while (cur < limit) {
        while (cur < limit && (unsigned char)*cur <= ' ') cur++;
        if (cur >= limit) break;
        uint32_t u = parse_uint(cur, limit);
        uint32_t v = parse_uint(cur, limit);
        uint32_t w = parse_uint(cur, limit);

        if (u < g.n) {
            uint32_t pf = fwd_cur[u]++;
            g.fwd_edges[pf] = {v, w};
        }
        if (v < g.n) {
            uint32_t pb = bwd_cur[v]++;
            g.bwd_edges[pb] = {u, w};
        }
    }

    mf.close(); // Unmap graph.txt immediately
#ifndef _WIN32
    malloc_trim(0);
#endif
}

struct FastDistMap {
    static constexpr size_t CAP = 131072;
    static constexpr size_t MASK = CAP - 1;
    struct Entry {
        uint32_t tag = 0;
        uint32_t node = 0;
        uint64_t dist = 0;
    };
    std::vector<Entry> table;
    uint32_t cur_tag = 1;

    void init() {
        table.assign(CAP, Entry{});
        cur_tag = 1;
    }

    inline void clear() {
        cur_tag++;
        if (__builtin_expect(cur_tag >= 2000000000, 0)) {
            std::memset(table.data(), 0, CAP * sizeof(Entry));
            cur_tag = 1;
        }
    }

    inline uint64_t get(uint32_t u) const {
        size_t idx = ((size_t)u * 2654435761ULL) & MASK;
        uint32_t tag = cur_tag;
        while (table[idx].tag == tag) {
            if (table[idx].node == u) return table[idx].dist;
            idx = (idx + 1) & MASK;
        }
        return UINT64_MAX;
    }

    inline void set(uint32_t u, uint64_t d) {
        size_t idx = ((size_t)u * 2654435761ULL) & MASK;
        uint32_t tag = cur_tag;
        while (table[idx].tag == tag) {
            if (table[idx].node == u) {
                table[idx].dist = d;
                return;
            }
            idx = (idx + 1) & MASK;
        }
        table[idx].tag = tag;
        table[idx].node = u;
        table[idx].dist = d;
    }
};

using Element = std::pair<uint64_t, uint32_t>; // (distance, node)

struct WorkerState {
    FastDistMap dist_f;
    FastDistMap dist_b;
    std::vector<Element> hf;
    std::vector<Element> hb;

    void init() {
        dist_f.init();
        dist_b.init();
        hf.reserve(4096);
        hb.reserve(4096);
    }
};

int64_t solve_bidir(const CSRGraph& g, WorkerState& ws, uint32_t s, uint32_t t) {
    if (s == t) return 0;
    if (s >= g.n || t >= g.n) return -1;

    ws.dist_f.clear();
    ws.dist_b.clear();

    ws.dist_f.set(s, 0);
    ws.dist_b.set(t, 0);

    ws.hf.clear();
    ws.hb.clear();

    ws.hf.push_back({0, s});
    ws.hb.push_back({0, t});

    uint64_t best = UINT64_MAX;

    while (!ws.hf.empty() && !ws.hb.empty()) {
        if (ws.hf.front().first + ws.hb.front().first >= best) {
            break;
        }

        if (ws.hf.size() <= ws.hb.size()) {
            std::pop_heap(ws.hf.begin(), ws.hf.end(), std::greater<Element>());
            auto [d, u] = ws.hf.back();
            ws.hf.pop_back();

            if (d > ws.dist_f.get(u) || d >= best) continue;

            uint64_t db = ws.dist_b.get(u);
            if (db != UINT64_MAX) {
                uint64_t cand = d + db;
                if (cand < best) best = cand;
            }

            uint32_t st = g.fwd_head[u];
            uint32_t en = g.fwd_head[u + 1];
            for (uint32_t i = st; i < en; ++i) {
                const auto& edge = g.fwd_edges[i];
                uint64_t nd = d + edge.w;
                if (nd < best) {
                    uint32_t v = edge.dst;
                    uint64_t cur_df = ws.dist_f.get(v);
                    if (nd < cur_df) {
                        ws.dist_f.set(v, nd);
                        ws.hf.push_back({nd, v});
                        std::push_heap(ws.hf.begin(), ws.hf.end(), std::greater<Element>());
                        uint64_t db_v = ws.dist_b.get(v);
                        if (db_v != UINT64_MAX) {
                            uint64_t cand = nd + db_v;
                            if (cand < best) best = cand;
                        }
                    }
                }
            }
        } else {
            std::pop_heap(ws.hb.begin(), ws.hb.end(), std::greater<Element>());
            auto [d, u] = ws.hb.back();
            ws.hb.pop_back();

            if (d > ws.dist_b.get(u) || d >= best) continue;

            uint64_t df = ws.dist_f.get(u);
            if (df != UINT64_MAX) {
                uint64_t cand = d + df;
                if (cand < best) best = cand;
            }

            uint32_t st = g.bwd_head[u];
            uint32_t en = g.bwd_head[u + 1];
            for (uint32_t i = st; i < en; ++i) {
                const auto& edge = g.bwd_edges[i];
                uint64_t nd = d + edge.w;
                if (nd < best) {
                    uint32_t v = edge.dst;
                    uint64_t cur_db = ws.dist_b.get(v);
                    if (nd < cur_db) {
                        ws.dist_b.set(v, nd);
                        ws.hb.push_back({nd, v});
                        std::push_heap(ws.hb.begin(), ws.hb.end(), std::greater<Element>());
                        uint64_t df_v = ws.dist_f.get(v);
                        if (df_v != UINT64_MAX) {
                            uint64_t cand = nd + df_v;
                            if (cand < best) best = cand;
                        }
                    }
                }
            }
        }
    }

    return (best == UINT64_MAX) ? -1 : (int64_t)best;
}

int main(int argc, char** argv) {
#ifndef _WIN32
    mallopt(M_ARENA_MAX, 1);
    mallopt(M_TRIM_THRESHOLD, 64 * 1024);
#endif
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);

    std::string data_dir = (argc > 1) ? argv[1] : "/data";
    std::string out_dir = (argc > 2) ? argv[2] : "/out";

    std::string graph_path = data_dir + "/graph.txt";
    CSRGraph g;
    load_graph(graph_path.c_str(), g);

    std::string queries_path = data_dir + "/queries.txt";
    MappedFile qmf;
    std::vector<std::pair<uint32_t, uint32_t>> queries;
    if (qmf.open(queries_path.c_str()) && qmf.size > 0) {
        const char* p = qmf.data;
        const char* limit = qmf.data + qmf.size;
        while (p < limit) {
            while (p < limit && (*p == ' ' || *p == '\r' || *p == '\n')) p++;
            if (p >= limit) break;
            uint32_t s = parse_uint(p, limit);
            uint32_t t = parse_uint(p, limit);
            queries.emplace_back(s, t);
        }
        qmf.close(); // Unmap queries.txt immediately
    }

    size_t total_q = queries.size();
    const size_t num_threads = 12;
    std::vector<WorkerState> worker_states(num_threads);
    for (size_t t = 0; t < num_threads; ++t) {
        worker_states[t].init();
    }

    std::vector<std::string> thread_outputs(num_threads);
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    size_t chunk_size = (total_q + num_threads - 1) / num_threads;

    for (size_t t = 0; t < num_threads; ++t) {
        size_t start = t * chunk_size;
        size_t end = std::min(total_q, start + chunk_size);
        threads.emplace_back([&, t, start, end]() {
            if (start >= end) return;
            auto& ws = worker_states[t];
            std::string& out_str = thread_outputs[t];
            out_str.reserve((end - start) * 20);
            char line_buf[32];

            for (size_t i = start; i < end; ++i) {
                int64_t ans = solve_bidir(g, ws, queries[i].first, queries[i].second);
                char* p = fast_i64(line_buf, ans);
                *p++ = '\n';
                out_str.append(line_buf, p - line_buf);
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    std::string out_path = out_dir + "/distances.txt";
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
