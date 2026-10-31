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

inline uint32_t parse_uint(const char*& p, const char* limit) {
    while (p < limit && (*p == ' ' || *p == '\r' || *p == '\n')) p++;
    uint32_t val = 0;
    while (p < limit && *p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        p++;
    }
    return val;
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

    std::vector<uint32_t> u_arr(g.m);
    std::vector<uint32_t> v_arr(g.m);
    std::vector<uint32_t> w_arr(g.m);

    g.fwd_head.assign(g.n + 1, 0);
    g.bwd_head.assign(g.n + 1, 0);

    for (uint32_t i = 0; i < g.m; ++i) {
        uint32_t u = parse_uint(p, limit);
        uint32_t v = parse_uint(p, limit);
        uint32_t w = parse_uint(p, limit);
        u_arr[i] = u;
        v_arr[i] = v;
        w_arr[i] = w;
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

    for (uint32_t i = 0; i < g.m; ++i) {
        uint32_t u = u_arr[i];
        uint32_t v = v_arr[i];
        uint32_t w = w_arr[i];

        uint32_t pf = fwd_cur[u]++;
        g.fwd_dst[pf] = v;
        g.fwd_w[pf] = w;

        uint32_t pb = bwd_cur[v]++;
        g.bwd_dst[pb] = u;
        g.bwd_w[pb] = w;
    }
}

using Element = std::pair<uint64_t, uint32_t>; // (distance, node)

struct WorkerState {
    std::vector<uint64_t> dist_f;
    std::vector<uint64_t> dist_b;
    std::vector<uint32_t> tag_f;
    std::vector<uint32_t> tag_b;
    std::vector<Element> hf;
    std::vector<Element> hb;
    uint32_t cur_tag = 1;

    void init(size_t n) {
        dist_f.resize(n);
        dist_b.resize(n);
        tag_f.assign(n, 0);
        tag_b.assign(n, 0);
        hf.reserve(4096);
        hb.reserve(4096);
        cur_tag = 1;
    }
};

int64_t solve_bidir(const CSRGraph& g, WorkerState& ws, uint32_t s, uint32_t t) {
    if (s == t) return 0;
    if (s >= g.n || t >= g.n) return -1;

    if (ws.cur_tag >= 0xFFFFFFF0) {
        std::fill(ws.tag_f.begin(), ws.tag_f.end(), 0);
        std::fill(ws.tag_b.begin(), ws.tag_b.end(), 0);
        ws.cur_tag = 1;
    }
    uint32_t tag = ws.cur_tag++;

    ws.tag_f[s] = tag;
    ws.dist_f[s] = 0;

    ws.tag_b[t] = tag;
    ws.dist_b[t] = 0;

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

            if (d > ws.dist_f[u]) continue;

            uint32_t st = g.fwd_head[u];
            uint32_t en = g.fwd_head[u + 1];
            for (uint32_t i = st; i < en; ++i) {
                uint64_t nd = d + g.fwd_w[i];
                if (nd < best) {
                    uint32_t v = g.fwd_dst[i];
                    if (ws.tag_f[v] != tag || nd < ws.dist_f[v]) {
                        ws.tag_f[v] = tag;
                        ws.dist_f[v] = nd;
                        ws.hf.push_back({nd, v});
                        std::push_heap(ws.hf.begin(), ws.hf.end(), std::greater<Element>());
                        if (ws.tag_b[v] == tag) {
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

            if (d > ws.dist_b[u]) continue;

            uint32_t st = g.bwd_head[u];
            uint32_t en = g.bwd_head[u + 1];
            for (uint32_t i = st; i < en; ++i) {
                uint64_t nd = d + g.bwd_w[i];
                if (nd < best) {
                    uint32_t v = g.bwd_dst[i];
                    if (ws.tag_b[v] != tag || nd < ws.dist_b[v]) {
                        ws.tag_b[v] = tag;
                        ws.dist_b[v] = nd;
                        ws.hb.push_back({nd, v});
                        std::push_heap(ws.hb.begin(), ws.hb.end(), std::greater<Element>());
                        if (ws.tag_f[v] == tag) {
                            uint64_t cand = nd + ws.dist_f[v];
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
    }

    size_t total_q = queries.size();
    const size_t num_threads = 6;
    std::vector<std::vector<int64_t>> thread_results(num_threads);
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
            thread_results[t].resize(end - start);
            for (size_t i = start; i < end; ++i) {
                thread_results[t][i - start] = solve_bidir(g, ws, queries[i].first, queries[i].second);
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

    char num_str[32];
    for (size_t t = 0; t < num_threads; ++t) {
        for (int64_t ans : thread_results[t]) {
            int len = snprintf(num_str, sizeof(num_str), "%lld\n", (long long)ans);
            fwrite(num_str, 1, len, fp);
        }
    }
    fclose(fp);

    return 0;
}
