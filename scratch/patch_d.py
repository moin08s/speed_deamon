with open('track-d/solution.cpp', 'rb') as f:
    content = f.read()

old_code = b"""struct CSRGraph {
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

    const size_t num_threads = 8;

    // Advance p to first edge line
    while (p < limit && *p != '\\n') p++;
    if (p < limit && *p == '\\n') p++;

    const char* edges_start = p;
    size_t edges_size = limit - edges_start;

    std::vector<size_t> split_points(num_threads + 1, 0);
    split_points[0] = 0;
    for (size_t i = 1; i < num_threads; ++i) {
        size_t target = i * (edges_size / num_threads);
        const char* nl = (const char*)std::memchr(edges_start + target, '\\n', edges_size - target);
        if (nl) split_points[i] = (nl - edges_start) + 1;
        else split_points[i] = edges_size;
    }
    split_points[num_threads] = edges_size;

    struct ThreadDeg {
        std::vector<uint32_t> fwd;
        std::vector<uint32_t> bwd;
    };
    std::vector<ThreadDeg> t_deg(num_threads);
    std::vector<std::thread> ths;
    ths.reserve(num_threads);

    // Pass 1: Parallel degree counting
    for (size_t t = 0; t < num_threads; ++t) {
        ths.emplace_back([&, t]() {
            const char* cur = edges_start + split_points[t];
            const char* chunk_lim = edges_start + split_points[t + 1];
            auto& td = t_deg[t];
            td.fwd.assign(g.n, 0);
            td.bwd.assign(g.n, 0);

            while (cur < chunk_lim) {
                while (cur < chunk_lim && (unsigned char)*cur <= ' ') cur++;
                if (cur >= chunk_lim) break;
                uint32_t u = parse_uint(cur, chunk_lim);
                uint32_t v = parse_uint(cur, chunk_lim);
                parse_uint(cur, chunk_lim); // skip w in pass 1
                if (u < g.n) td.fwd[u]++;
                if (v < g.n) td.bwd[v]++;
            }
        });
    }
    for (auto& th : ths) th.join();

    // Compute heads and per-thread starts
    g.fwd_head.assign(g.n + 1, 0);
    g.bwd_head.assign(g.n + 1, 0);

    std::vector<std::vector<uint32_t>> fwd_starts(num_threads, std::vector<uint32_t>(g.n));
    std::vector<std::vector<uint32_t>> bwd_starts(num_threads, std::vector<uint32_t>(g.n));

    uint32_t cur_f = 0, cur_b = 0;
    for (uint32_t i = 0; i < g.n; ++i) {
        g.fwd_head[i] = cur_f;
        for (size_t t = 0; t < num_threads; ++t) {
            fwd_starts[t][i] = cur_f;
            cur_f += t_deg[t].fwd[i];
        }

        g.bwd_head[i] = cur_b;
        for (size_t t = 0; t < num_threads; ++t) {
            bwd_starts[t][i] = cur_b;
            cur_b += t_deg[t].bwd[i];
        }
    }
    g.fwd_head[g.n] = cur_f;
    g.bwd_head[g.n] = cur_b;

    // Free t_deg now
    t_deg.clear();
    t_deg.shrink_to_fit();

    g.fwd_dst.resize(cur_f);
    g.fwd_w.resize(cur_f);
    g.bwd_dst.resize(cur_b);
    g.bwd_w.resize(cur_b);

    // Pass 2: Parallel direct scatter into CSR arrays
    ths.clear();
    for (size_t t = 0; t < num_threads; ++t) {
        ths.emplace_back([&, t]() {
            const char* cur = edges_start + split_points[t];
            const char* chunk_lim = edges_start + split_points[t + 1];
            auto& f_start = fwd_starts[t];
            auto& b_start = bwd_starts[t];

            while (cur < chunk_lim) {
                while (cur < chunk_lim && (unsigned char)*cur <= ' ') cur++;
                if (cur >= chunk_lim) break;
                uint32_t u = parse_uint(cur, chunk_lim);
                uint32_t v = parse_uint(cur, chunk_lim);
                uint32_t w = parse_uint(cur, chunk_lim);

                if (u < g.n) {
                    uint32_t pf = f_start[u]++;
                    g.fwd_dst[pf] = v;
                    g.fwd_w[pf] = w;
                }
                if (v < g.n) {
                    uint32_t pb = b_start[v]++;
                    g.bwd_dst[pb] = u;
                    g.bwd_w[pb] = w;
                }
            }
        });
    }
    for (auto& th : ths) th.join();

    mf.close(); // Unmap graph.txt immediately
#ifndef _WIN32
    malloc_trim(0);
#endif
}"""

new_code = b"""struct Edge {
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

    const size_t num_threads = 6;

    // Advance p to first edge line
    while (p < limit && *p != '\\n') p++;
    if (p < limit && *p == '\\n') p++;

    const char* edges_start = p;
    size_t edges_size = limit - edges_start;

    std::vector<size_t> split_points(num_threads + 1, 0);
    split_points[0] = 0;
    for (size_t i = 1; i < num_threads; ++i) {
        size_t target = i * (edges_size / num_threads);
        const char* nl = (const char*)std::memchr(edges_start + target, '\\n', edges_size - target);
        if (nl) split_points[i] = (nl - edges_start) + 1;
        else split_points[i] = edges_size;
    }
    split_points[num_threads] = edges_size;

    struct ThreadDeg {
        std::vector<uint32_t> fwd;
        std::vector<uint32_t> bwd;
    };
    std::vector<ThreadDeg> t_deg(num_threads);
    std::vector<std::thread> ths;
    ths.reserve(num_threads);

    // Pass 1: Parallel degree counting
    for (size_t t = 0; t < num_threads; ++t) {
        ths.emplace_back([&, t]() {
            const char* cur = edges_start + split_points[t];
            const char* chunk_lim = edges_start + split_points[t + 1];
            auto& td = t_deg[t];
            td.fwd.assign(g.n, 0);
            td.bwd.assign(g.n, 0);

            while (cur < chunk_lim) {
                while (cur < chunk_lim && (unsigned char)*cur <= ' ') cur++;
                if (cur >= chunk_lim) break;
                uint32_t u = parse_uint(cur, chunk_lim);
                uint32_t v = parse_uint(cur, chunk_lim);
                parse_uint(cur, chunk_lim); // skip w in pass 1
                if (u < g.n) td.fwd[u]++;
                if (v < g.n) td.bwd[v]++;
            }
        });
    }
    for (auto& th : ths) th.join();

    // Compute heads and convert t_deg in-place into thread start offsets (zero extra memory!)
    g.fwd_head.assign(g.n + 1, 0);
    g.bwd_head.assign(g.n + 1, 0);

    uint32_t cur_f = 0, cur_b = 0;
    for (uint32_t i = 0; i < g.n; ++i) {
        g.fwd_head[i] = cur_f;
        for (size_t t = 0; t < num_threads; ++t) {
            uint32_t cnt = t_deg[t].fwd[i];
            t_deg[t].fwd[i] = cur_f;
            cur_f += cnt;
        }

        g.bwd_head[i] = cur_b;
        for (size_t t = 0; t < num_threads; ++t) {
            uint32_t cnt = t_deg[t].bwd[i];
            t_deg[t].bwd[i] = cur_b;
            cur_b += cnt;
        }
    }
    g.fwd_head[g.n] = cur_f;
    g.bwd_head[g.n] = cur_b;

    g.fwd_edges.resize(cur_f);
    g.bwd_edges.resize(cur_b);

    // Pass 2: Parallel direct scatter into packed 8-byte Edge CSR arrays
    ths.clear();
    for (size_t t = 0; t < num_threads; ++t) {
        ths.emplace_back([&, t]() {
            const char* cur = edges_start + split_points[t];
            const char* chunk_lim = edges_start + split_points[t + 1];
            auto& f_start = t_deg[t].fwd;
            auto& b_start = t_deg[t].bwd;

            while (cur < chunk_lim) {
                while (cur < chunk_lim && (unsigned char)*cur <= ' ') cur++;
                if (cur >= chunk_lim) break;
                uint32_t u = parse_uint(cur, chunk_lim);
                uint32_t v = parse_uint(cur, chunk_lim);
                uint32_t w = parse_uint(cur, chunk_lim);

                if (u < g.n) {
                    uint32_t pf = f_start[u]++;
                    g.fwd_edges[pf] = {v, w};
                }
                if (v < g.n) {
                    uint32_t pb = b_start[v]++;
                    g.bwd_edges[pb] = {u, w};
                }
            }
        });
    }
    for (auto& th : ths) th.join();

    t_deg.clear();
    t_deg.shrink_to_fit();

    mf.close(); // Unmap graph.txt immediately
#ifndef _WIN32
    malloc_trim(0);
#endif
}"""

if old_code in content:
    content = content.replace(old_code, new_code)
    print("Graph loader replaced (LF)")
elif old_code.replace(b'\n', b'\r\n') in content:
    content = content.replace(old_code.replace(b'\n', b'\r\n'), new_code.replace(b'\n', b'\r\n'))
    print("Graph loader replaced (CRLF)")
else:
    print("Graph loader NOT found")

# Replace relaxation loops in solve_bidir
old_fwd_loop = b"""            uint32_t st = g.fwd_head[u];
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
            }"""

new_fwd_loop = b"""            uint32_t st = g.fwd_head[u];
            uint32_t en = g.fwd_head[u + 1];
            for (uint32_t i = st; i < en; ++i) {
                const auto& edge = g.fwd_edges[i];
                uint64_t nd = d + edge.w;
                if (nd < best) {
                    uint32_t v = edge.dst;
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
            }"""

if old_fwd_loop in content:
    content = content.replace(old_fwd_loop, new_fwd_loop)
    print("Fwd loop replaced (LF)")
elif old_fwd_loop.replace(b'\n', b'\r\n') in content:
    content = content.replace(old_fwd_loop.replace(b'\n', b'\r\n'), new_fwd_loop.replace(b'\n', b'\r\n'))
    print("Fwd loop replaced (CRLF)")
else:
    print("Fwd loop NOT found")

old_bwd_loop = b"""            uint32_t st = g.bwd_head[u];
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
            }"""

new_bwd_loop = b"""            uint32_t st = g.bwd_head[u];
            uint32_t en = g.bwd_head[u + 1];
            for (uint32_t i = st; i < en; ++i) {
                const auto& edge = g.bwd_edges[i];
                uint64_t nd = d + edge.w;
                if (nd < best) {
                    uint32_t v = edge.dst;
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
            }"""

if old_bwd_loop in content:
    content = content.replace(old_bwd_loop, new_bwd_loop)
    print("Bwd loop replaced (LF)")
elif old_bwd_loop.replace(b'\n', b'\r\n') in content:
    content = content.replace(old_bwd_loop.replace(b'\n', b'\r\n'), new_bwd_loop.replace(b'\n', b'\r\n'))
    print("Bwd loop replaced (CRLF)")
else:
    print("Bwd loop NOT found")

# Replace num_threads = 8 with 6 in main
old_main_threads = b"    const size_t num_threads = 8;"
new_main_threads = b"    const size_t num_threads = 6;"
if old_main_threads in content:
    content = content.replace(old_main_threads, new_main_threads)
    print("Main num_threads replaced (LF)")
elif old_main_threads.replace(b'\n', b'\r\n') in content:
    content = content.replace(old_main_threads.replace(b'\n', b'\r\n'), new_main_threads.replace(b'\n', b'\r\n'))
    print("Main num_threads replaced (CRLF)")
else:
    print("Main num_threads NOT found")

with open('track-d/solution.cpp', 'wb') as f:
    f.write(content)
print("Updated track-d/solution.cpp successfully!")
