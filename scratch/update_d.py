new_solution = r"""#include <iostream>
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
    while (p < limit && (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t')) p++;
    uint32_t val = 0;
    while (p < limit && *p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        p++;
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

struct CSRGraph {
    uint32_t n = 0;
    uint32_t m = 0;
    std::vector<uint32_t> fwd_head;
    std::vector<uint32_t> fwd_dst;
    std::vector<uint32_t> fwd_w;
    std::vector<uint32_t> bwd_head;
    std::vector<uint32_t> bwd_dst;
    std::vector<uint32_t> bwd_w;
};

void load_graph(const char* path, CSRGraph& g) {
    MappedFile mf;
    if (!mf.open(path) || mf.size == 0) return;

    const char* p = mf.data;
    const char* limit = mf.data + mf.size;

    g.n = parse_uint(p, limit);
    g.m = parse_uint(p, limit);

    g.fwd_head.assign(g.n + 1, 0);
    g.bwd_head.assign(g.n + 1, 0);

    const char* edge_data_start = p;

    // Pass 1: Count degrees directly without allocating intermediate edges
    for (uint32_t i = 0; i < g.m; ++i) {
        uint32_t u = parse_uint(p, limit);
        uint32_t v = parse_uint(p, limit);
        parse_uint(p, limit); // skip weight in pass 1
        if (u + 1 <= g.n) g.fwd_head[u + 1]++;
        if (v + 1 <= g.n) g.bwd_head[v + 1]++;
    }

    for (uint32_t i = 0; i < g.n; ++i) {
        g.fwd_head[i + 1] += g.fwd_head[i];
        g.bwd_head[i + 1] += g.bwd_head[i];
    }

    g.fwd_dst.resize(g.m);
    g.fwd_w.resize(g.m);
    std::vector<uint32_t> fwd_cur = g.fwd_head;

    g.bwd_dst.resize(g.m);
    g.bwd_w.resize(g.m);
    std::vector<uint32_t> bwd_cur = g.bwd_head;

    // Pass 2: Scatter directly into CSR arrays
    p = edge_data_start;
    for (uint32_t i = 0; i < g.m; ++i) {
        uint32_t u = parse_uint(p, limit);
        uint32_t v = parse_uint(p, limit);
        uint32_t w = parse_uint(p, limit);

        uint32_t pf = fwd_cur[u]++;
        g.fwd_dst[pf] = v;
        g.fwd_w[pf] = w;

        uint32_t pb = bwd_cur[v]++;
        g.bwd_dst[pb] = u;
        g.bwd_w[pb] = w;
    }

    mf.close(); // Unmap graph.txt immediately
}

using Element = std::pair<uint64_t, uint32_t>; // (distance, node)

struct WorkerState {
    std::vector<uint64_t> dist_f;
    std::vector<uint64_t> dist_b;
    std::vector<uint32_t> touched_f;
    std::vector<uint32_t> touched_b;
    std::vector<Element> hf;
    std::vector<Element> hb;

    void init(size_t n) {
        dist_f.assign(n, UINT64_MAX);
        dist_b.assign(n, UINT64_MAX);
        touched_f.reserve(4096);
        touched_b.reserve(4096);
        hf.reserve(4096);
        hb.reserve(4096);
    }
};

int64_t solve_bidir(const CSRGraph& g, WorkerState& ws, uint32_t s, uint32_t t) {
    if (s == t) return 0;
    if (s >= g.n || t >= g.n) return -1;

    ws.dist_f[s] = 0;
    ws.touched_f.push_back(s);

    ws.dist_b[t] = 0;
    ws.touched_b.push_back(t);

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

            if (d > ws.dist_f[u] || d >= best) continue;

            uint32_t st = g.fwd_head[u];
            uint32_t en = g.fwd_head[u + 1];
            for (uint32_t i = st; i < en; ++i) {
                uint64_t nd = d + g.fwd_w[i];
                if (nd < best) {
                    uint32_t v = g.fwd_dst[i];
                    if (ws.dist_f[v] == UINT64_MAX) {
                        ws.touched_f.push_back(v);
                        ws.dist_f[v] = nd;
                        ws.hf.push_back({nd, v});
                        std::push_heap(ws.hf.begin(), ws.hf.end(), std::greater<Element>());
                        if (ws.dist_b[v] != UINT64_MAX) {
                            uint64_t cand = nd + ws.dist_b[v];
                            if (cand < best) best = cand;
                        }
                    } else if (nd < ws.dist_f[v]) {
                        ws.dist_f[v] = nd;
                        ws.hf.push_back({nd, v});
                        std::push_heap(ws.hf.begin(), ws.hf.end(), std::greater<Element>());
                        if (ws.dist_b[v] != UINT64_MAX) {
                            uint64_t cand = nd + ws.dist_b[v];
                            if (cand < best) best = cand;
                        }
                    }
                }
            }
        } else {
            std::pop_heap(ws.hb.begin(), ws.hb.end(), std::greater<Element>());
            auto [d, u] = ws.hb.back();
            ws.hb.pop_back();

            if (d > ws.dist_b[u] || d >= best) continue;

            uint32_t st = g.bwd_head[u];
            uint32_t en = g.bwd_head[u + 1];
            for (uint32_t i = st; i < en; ++i) {
                uint64_t nd = d + g.bwd_w[i];
                if (nd < best) {
                    uint32_t v = g.bwd_dst[i];
                    if (ws.dist_b[v] == UINT64_MAX) {
                        ws.touched_b.push_back(v);
                        ws.dist_b[v] = nd;
                        ws.hb.push_back({nd, v});
                        std::push_heap(ws.hb.begin(), ws.hb.end(), std::greater<Element>());
                        if (ws.dist_f[v] != UINT64_MAX) {
                            uint64_t cand = nd + ws.dist_f[v];
                            if (cand < best) best = cand;
                        }
                    } else if (nd < ws.dist_b[v]) {
                        ws.dist_b[v] = nd;
                        ws.hb.push_back({nd, v});
                        std::push_heap(ws.hb.begin(), ws.hb.end(), std::greater<Element>());
                        if (ws.dist_f[v] != UINT64_MAX) {
                            uint64_t cand = nd + ws.dist_f[v];
                            if (cand < best) best = cand;
                        }
                    }
                }
            }
        }
    }

    // Reset only touched nodes to UINT64_MAX for next query
    for (uint32_t v : ws.touched_f) ws.dist_f[v] = UINT64_MAX;
    for (uint32_t v : ws.touched_b) ws.dist_b[v] = UINT64_MAX;
    ws.touched_f.clear();
    ws.touched_b.clear();

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
    const size_t num_threads = 6;
    std::vector<std::string> thread_outputs(num_threads);
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    size_t chunk_size = (total_q + num_threads - 1) / num_threads;

    for (size_t t = 0; t < num_threads; ++t) {
        size_t start = t * chunk_size;
        size_t end = std::min(total_q, start + chunk_size);
        threads.emplace_back([&, t, start, end]() {
            if (start >= end) return;
            WorkerState ws;
            ws.init(g.n);
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
"""

with open('track-d/solution.cpp', 'w', newline='\n') as f:
    f.write(new_solution.strip() + '\n')
print("Successfully written 64-bit track-d/solution.cpp with LF line endings")
